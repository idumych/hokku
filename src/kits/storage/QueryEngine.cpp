/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	Queries without a file system that knows them.

	The predicate is parsed and matched by Haiku's QueryParser, so that it means
	what it means on BFS. Haiku's file systems find the candidates in their
	indices; here they are the files of the volume, walked in turn:

	- the volume of the home folder: the settings folder and the home folder,
	- the root volume, if the home folder is not on it: nothing (the system's
	  files have no attributes, and walking them would take minutes),
	- any other volume: all of it.

	Hidden entries are left out (they are mostly caches), links are not followed
	and a volume mounted inside another one is left to its own query. An
	attribute is read from its extended attribute without opening the file: a
	file opened for writing, which is how BNode tries first, looks changed to
	whoever watches it, and opening a FIFO that can only be read blocks.

	A live query walks on a thread of its own, so that Fetch() does not wait for
	it. BFS tells a live query of every change to an index. Here, watching every
	folder is out of the question: a home folder can have 400,000 of them, and
	a user gets about 500,000 inotify watches for all of their programs. So a query
	watches some folders, those that it is most likely to matter in (the ones it
	starts from, the ones that have a match, then the ones nearest the start),
	and hears of a change in those at once. A change anywhere else is found by
	walking again now and then, at a low priority: every kWalkInterval, or if
	walking takes long, so that it stays around 3% of one CPU.
*/


#include <QueryEngine.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <Entry.h>
#include <fs_query.h>
#include <TypeConstants.h>


#ifndef __linux__


namespace BPrivate {
namespace Storage {


class QueryRun {};


status_t
open_query(dev_t, const char*, uint32, port_id, int32, QueryRun**)
{
	return B_NOT_SUPPORTED;
}


void
close_query(QueryRun*)
{
}


status_t
next_query_ref(QueryRun*, entry_ref*)
{
	return B_ENTRY_NOT_FOUND;
}


status_t
rewind_query(QueryRun*)
{
	return B_NOT_SUPPORTED;
}


}	// namespace Storage
}	// namespace BPrivate


#else	// __linux__


#include <mntent.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <FindDirectory.h>
#include <fs_attr.h>
#include <Message.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <OS.h>
#include <Path.h>

#include <AppDefs.h>
#include <MessengerPrivate.h>
#include <TokenSpace.h>

#define QUERY_EXPRESSION_ONLY
#define QUERY_FATAL(message...)		debugger("query parser")
#define QUERY_INFORM(message...)	do {} while (false)
#include <file_systems/QueryParser.h>


namespace fs = std::filesystem;


namespace BPrivate {
namespace Storage {


static const char* const kAttributeTypes = "user.COSMOE:ATTR_TYPES";
static const size_t kAttributePrefixLength = 5;	// "user."

static const size_t kDefaultWatchLimit = 4096;
static const int kWalkInterval = 60;			// in seconds, at the least
static const int kWalkCostFactor = 30;			// of what a walk takes
static const bigtime_t kSendTimeout = 1000000;


/*!	A file as the parser sees it. It stands for both the Entry and the Node of
	a file system.
*/
struct QueryEntry {
	const char*	path;
	const char*	name;
	struct stat	status;
	bool		hasStatus;

	QueryEntry(const char* path, const char* name)
		:
		path(path),
		name(name),
		hasStatus(false)
	{
	}

	const struct stat& Status()
	{
		if (!hasStatus) {
			if (lstat(path, &status) != 0)
				memset(&status, 0, sizeof(status));
			hasStatus = true;
		}
		return status;
	}
};


/*!	The type of an attribute, from where fs_write_attr() keeps it. An attribute
	that comes from elsewhere (setfattr, say) has none, and is taken for the
	string that it almost certainly is.
*/
static int32
attribute_type(const char* path, const char* attribute)
{
	char records[4096];
	const ssize_t size = lgetxattr(path, kAttributeTypes, records,
		sizeof(records));
	const size_t length = strlen(attribute);

	ssize_t offset = 0;
	while (size >= 0 && offset + (ssize_t)(2 * sizeof(uint32)) <= size) {
		uint32 nameLength;
		uint32 type;
		memcpy(&nameLength, records + offset, sizeof(nameLength));
		memcpy(&type, records + offset + sizeof(uint32), sizeof(type));
		offset += 2 * sizeof(uint32);
		if (nameLength > (size_t)(size - offset))
			break;
		if (nameLength == length
			&& memcmp(records + offset, attribute, length) == 0) {
			return (int32)type;
		}
		offset += nameLength;
	}
	return B_STRING_TYPE;
}


static bool
is_string_type(int32 type)
{
	return type == B_STRING_TYPE || type == B_MIME_STRING_TYPE;
}


/*!	What QueryParser needs of a file system. The functions of the indices are
	here only because the classes that call them are instantiated as a whole:
	nothing calls CalculateScore() without QueryParser::Query, which is not
	used.
*/
struct QueryPolicy {
	typedef QueryEntry	Entry;
	typedef QueryEntry	Node;
	typedef void		Context;
	typedef void*		IndexIterator;

	struct Index {};
	struct NodeHolder {
		std::string longValue;
	};

	static const int32 kMaxFileNameLength = B_FILE_NAME_LENGTH;

	static const char* EntryGetNameNoCopy(NodeHolder&, Entry* entry)
	{
		return entry->name;
	}

	static off_t NodeGetSize(Node* node)
	{
		return node->Status().st_size;
	}

	static time_t NodeGetLastModifiedTime(Node* node)
	{
		return node->Status().st_mtime;
	}

	static status_t NodeGetAttribute(NodeHolder& holder, Node* node,
		const char* attribute, uint8*& buffer, size_t* size, int32* type)
	{
		char name[B_ATTR_NAME_LENGTH + kAttributePrefixLength];
		if (snprintf(name, sizeof(name), "user.%s", attribute)
				>= (int)sizeof(name)) {
			return B_NAME_TOO_LONG;
		}

		const size_t capacity = *size - 1;
		ssize_t length = lgetxattr(node->path, name, buffer, capacity);
		if (length < 0 && errno == ERANGE) {
			// As on BFS, a query only sees the start of a long value.
			const ssize_t fullLength = lgetxattr(node->path, name, NULL, 0);
			if (fullLength < 0)
				return B_ENTRY_NOT_FOUND;
			holder.longValue.resize(fullLength);
			length = lgetxattr(node->path, name, &holder.longValue[0],
				fullLength);
			if (length < 0)
				return B_ENTRY_NOT_FOUND;
			length = std::min((size_t)length, capacity);
			memcpy(buffer, holder.longValue.data(), length);
		} else if (length < 0)
			return B_ENTRY_NOT_FOUND;

		buffer[length] = '\0';
		*type = attribute_type(node->path, attribute);
		// The terminator that a string attribute is stored with is not part of
		// the value that is compared.
		*size = is_string_type(*type) ? strlen((const char*)buffer) : length;
		return B_OK;
	}

	static status_t IndexSetTo(Index&, const char*)
	{
		return B_ENTRY_NOT_FOUND;
	}

	static type_code IndexGetType(Index&)
	{
		return B_STRING_TYPE;
	}

	static int32 IndexGetKeySize(Index&)
	{
		return 0;
	}

	static int64 IndexGetSize(Index&)
	{
		return 0;
	}
};


typedef QueryParser::Expression<QueryPolicy> Expression;


/*!	Where a query of a volume looks. */
static std::vector<fs::path>
roots_of_volume(dev_t device)
{
	std::vector<fs::path> roots;

	BPath home;
	struct stat status;
	if (find_directory(B_USER_DIRECTORY, &home) == B_OK
		&& stat(home.Path(), &status) == 0 && status.st_dev == device) {
		BPath settings;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK
			&& stat(settings.Path(), &status) == 0
			&& status.st_dev == device) {
			roots.push_back(settings.Path());
		}
		roots.push_back(home.Path());
		return roots;
	}

	FILE* mounts = setmntent("/proc/mounts", "r");
	if (mounts == NULL)
		return roots;

	struct mntent* mount;
	while ((mount = getmntent(mounts)) != NULL) {
		if (stat(mount->mnt_dir, &status) != 0 || status.st_dev != device)
			continue;
		if (strcmp(mount->mnt_dir, "/") != 0)
			roots.push_back(mount->mnt_dir);
		break;
	}
	endmntent(mounts);
	return roots;
}


/*!	Goes through what is below some folders, leaving out what a query leaves
	out.
*/
class VolumeWalk {
public:
	VolumeWalk(dev_t device, std::vector<fs::path> roots)
		:
		fDevice(device),
		fRoots(std::move(roots)),
		fNextRoot(0),
		fWalking(false)
	{
	}

	void Rewind()
	{
		fNextRoot = 0;
		fWalking = false;
		fEntry = fs::recursive_directory_iterator();
	}

	/*!	\param _isDirectory Set for a real folder, not for a link to one. */
	bool Next(std::string* _path, std::string* _name, bool* _isDirectory)
	{
		while (_NextEntry()) {
			std::error_code error;
			const fs::path path = fEntry->path();
			const std::string name = path.filename().native();
			const bool isDirectory = !fEntry->is_symlink(error)
				&& fEntry->is_directory(error);

			struct stat status;
			const bool isOtherVolume = isDirectory
				&& (lstat(path.c_str(), &status) != 0
					|| status.st_dev != fDevice);
			if (name[0] == '.' || isOtherVolume)
				fEntry.disable_recursion_pending();

			fEntry.increment(error);
			if (error) {
				// A folder that went away meanwhile ends the iterator.
				fWalking = false;
			}

			if (name[0] == '.' || isOtherVolume)
				continue;

			*_path = path.native();
			*_name = name;
			*_isDirectory = isDirectory;
			return true;
		}
		return false;
	}

private:
	bool _NextEntry()
	{
		const fs::recursive_directory_iterator end;
		while (true) {
			if (fWalking && fEntry != end)
				return true;

			if (fNextRoot >= fRoots.size())
				return false;

			std::error_code error;
			fEntry = fs::recursive_directory_iterator(fRoots[fNextRoot++],
				fs::directory_options::skip_permission_denied, error);
			fWalking = !error;
		}
	}

private:
	dev_t		fDevice;
	std::vector<fs::path> fRoots;
	size_t		fNextRoot;
	fs::recursive_directory_iterator fEntry;
	bool		fWalking;
};


static bool
matches(Expression& expression, const char* path, const char* name)
{
	QueryEntry entry(path, name);
	// (A status below 0 is a value that can't be compared with the one in the
	// predicate: a pattern for a number, say. It's no match.)
	return expression.Root()->Match(&entry, &entry) == QueryParser::MATCH_OK;
}


static status_t
init_expression(Expression& expression, const char* predicate)
{
	const char* position = NULL;
	const status_t status = expression.Init(predicate, &position);
	if (status != B_OK)
		return status;
	return expression.Root() != NULL ? B_OK : B_BAD_VALUE;
}


static status_t
set_ref(entry_ref* ref, dev_t device, ino_t directory, const char* path)
{
	ref->device = device;
	ref->directory = directory;
	return ref->set_name(path);
}


class QueryRun {
public:
	virtual ~QueryRun() {}

	virtual status_t Init(const char* predicate) = 0;
	virtual status_t Next(entry_ref* ref) = 0;
	virtual status_t Rewind() = 0;
};


/*!	A query that is over when its entries have been read. It walks only as far
	as it is read.
*/
class WalkQueryRun : public QueryRun {
public:
	WalkQueryRun(dev_t device, std::vector<fs::path> roots)
		:
		fDevice(device),
		fWalk(device, std::move(roots))
	{
	}

	virtual status_t Init(const char* predicate)
	{
		return init_expression(fExpression, predicate);
	}

	virtual status_t Next(entry_ref* ref)
	{
		std::string path;
		std::string name;
		bool isDirectory;
		while (fWalk.Next(&path, &name, &isDirectory)) {
			if (!matches(fExpression, path.c_str(), name.c_str()))
				continue;

			struct stat parent;
			if (stat(fs::path(path).parent_path().c_str(), &parent) != 0)
				continue;
			return set_ref(ref, fDevice, parent.st_ino, path.c_str());
		}
		return B_ENTRY_NOT_FOUND;
	}

	virtual status_t Rewind()
	{
		fWalk.Rewind();
		return B_OK;
	}

private:
	Expression	fExpression;
	dev_t		fDevice;
	VolumeWalk	fWalk;
};


static int
environment_value(const char* name, int fallback)
{
	const char* value = getenv(name);
	return value != NULL && atoi(value) > 0 ? atoi(value) : fallback;
}


/*!	A query that goes on telling its target what starts and stops matching.

	Everything but Next() and Rewind() happens on its thread: nothing else
	touches the expression, the watches or what is known to match.
*/
class LiveQueryRun : public QueryRun {
public:
	LiveQueryRun(dev_t device, std::vector<fs::path> roots, port_id port,
		int32 token)
		:
		fDevice(device),
		fRoots(std::move(roots)),
		fPort(port),
		fToken(token),
		fNotifyFD(-1),
		fWakeFD(-1),
		fStop(false),
		fReadPosition(0),
		fFirstWalkDone(false),
		fWatchLimit(environment_value("HOKKU_QUERY_WATCH_LIMIT",
			kDefaultWatchLimit)),
		fNoMoreWatches(false),
		fWalkAgain(false)
	{
	}

	virtual ~LiveQueryRun()
	{
		if (fThread.joinable()) {
			fStop = true;
			const uint64_t one = 1;
			(void)!write(fWakeFD, &one, sizeof(one));
			fThread.join();
		}
		if (fNotifyFD >= 0)
			close(fNotifyFD);
		if (fWakeFD >= 0)
			close(fWakeFD);
	}

	virtual status_t Init(const char* predicate)
	{
		status_t status = init_expression(fExpression, predicate);
		if (status != B_OK)
			return status;

		fNotifyFD = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
		fWakeFD = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
		if (fNotifyFD < 0 || fWakeFD < 0)
			return B_NO_MORE_FDS;

		try {
			fThread = std::thread(&LiveQueryRun::_Run, this);
		} catch (const std::system_error&) {
			return B_NO_MORE_THREADS;
		}
		return B_OK;
	}

	virtual status_t Next(entry_ref* ref)
	{
		std::unique_lock<std::mutex> lock(fResultsLock);
		fResultsChanged.wait(lock, [this] {
			return fReadPosition < fResults.size() || fFirstWalkDone;
		});
		if (fReadPosition >= fResults.size())
			return B_ENTRY_NOT_FOUND;

		const Result& result = fResults[fReadPosition++];
		return set_ref(ref, fDevice, result.directory, result.path.c_str());
	}

	virtual status_t Rewind()
	{
		// What the first walk found: what came after was sent to the target.
		std::lock_guard<std::mutex> lock(fResultsLock);
		fReadPosition = 0;
		return B_OK;
	}

private:
	struct Result {
		std::string	path;
		ino_t		directory;
	};

	struct Known {
		ino_t		directory;
		ino_t		node;
	};

	struct Candidate {
		size_t		depth;
		std::string	path;

		bool operator<(const Candidate& other) const
		{
			return depth < other.depth;
		}
	};

	void _Run()
	{
		const auto start = std::chrono::steady_clock::now();
		_FirstWalk();
		const auto cost = std::chrono::duration_cast<std::chrono::seconds>(
			std::chrono::steady_clock::now() - start).count();

		// Keeping an eye on the files from now on is background work. (It
		// can't be undone without privileges, and need not be.)
		setpriority(PRIO_PROCESS, gettid(), 10);

		const int interval = environment_value(
			"HOKKU_QUERY_WALK_INTERVAL",
			std::max((int)(cost * kWalkCostFactor), kWalkInterval));
		auto nextWalk = std::chrono::steady_clock::now()
			+ std::chrono::seconds(interval);

		while (!fStop) {
			const auto now = std::chrono::steady_clock::now();
			if (fWalkAgain || now >= nextWalk) {
				fWalkAgain = false;
				_WalkAgain();
				nextWalk = std::chrono::steady_clock::now()
					+ std::chrono::seconds(interval);
				continue;
			}

			pollfd fds[2] = {{fNotifyFD, POLLIN, 0}, {fWakeFD, POLLIN, 0}};
			const int timeout = std::chrono::duration_cast<
				std::chrono::milliseconds>(nextWalk - now).count() + 1;
			if (poll(fds, 2, timeout) > 0 && (fds[0].revents & POLLIN) != 0)
				_ReadEvents();
		}
	}

	void _FirstWalk()
	{
		std::vector<Candidate> candidates;
		std::vector<std::string> folders;
		std::unordered_set<std::string> withMatch;

		for (const fs::path& root : fRoots)
			folders.push_back(root.native());

		VolumeWalk walk(fDevice, fRoots);
		std::string path;
		std::string name;
		bool isDirectory;
		while (!fStop && walk.Next(&path, &name, &isDirectory)) {
			if (isDirectory) {
				// Of the folders only the nearest are kept, so that a home
				// folder's 400,000 aren't all in memory for a moment.
				candidates.push_back({_Depth(path), path});
				if (candidates.size() > 2 * fWatchLimit) {
					std::nth_element(candidates.begin(),
						candidates.begin() + fWatchLimit, candidates.end());
					candidates.resize(fWatchLimit);
				}
			}

			Known known;
			if (!matches(fExpression, path.c_str(), name.c_str())
				|| !_GetKnown(path, &known)) {
				continue;
			}

			fKnown[path] = known;
			withMatch.insert(fs::path(path).parent_path().native());

			std::lock_guard<std::mutex> lock(fResultsLock);
			fResults.push_back({path, known.directory});
			fResultsChanged.notify_one();
		}

		{
			std::lock_guard<std::mutex> lock(fResultsLock);
			fFirstWalkDone = true;
			fResultsChanged.notify_all();
		}

		for (const std::string& folder : folders)
			_Watch(folder);
		for (const std::string& folder : withMatch)
			_Watch(folder);
		std::sort(candidates.begin(), candidates.end());
		for (const Candidate& candidate : candidates)
			_Watch(candidate.path);
	}

	void _WalkAgain()
	{
		std::unordered_set<std::string> seen;
		VolumeWalk walk(fDevice, fRoots);
		std::string path;
		std::string name;
		bool isDirectory;
		while (!fStop && walk.Next(&path, &name, &isDirectory)) {
			if (!matches(fExpression, path.c_str(), name.c_str()))
				continue;
			seen.insert(path);
			if (fKnown.find(path) == fKnown.end())
				_Started(path);
		}
		if (fStop)
			return;

		std::vector<std::string> gone;
		for (const auto& known : fKnown) {
			if (seen.find(known.first) == seen.end())
				gone.push_back(known.first);
		}
		for (const std::string& path : gone)
			_Stopped(path);
	}

	void _ReadEvents()
	{
		alignas(struct inotify_event) char buffer[64 * 1024];
		const ssize_t size = read(fNotifyFD, buffer, sizeof(buffer));
		for (ssize_t offset = 0; offset < size && !fStop;) {
			const struct inotify_event* event
				= (const struct inotify_event*)(buffer + offset);
			offset += sizeof(struct inotify_event) + event->len;
			_HandleEvent(event);
		}
	}

	void _HandleEvent(const struct inotify_event* event)
	{
		if ((event->mask & IN_Q_OVERFLOW) != 0) {
			// Too much happened at once to tell what.
			fWalkAgain = true;
			return;
		}

		const auto folder = fWatches.find(event->wd);
		if (folder == fWatches.end())
			return;

		if ((event->mask & IN_IGNORED) != 0) {
			fWatchedFolders.erase(folder->second);
			fWatches.erase(folder);
			fNoMoreWatches = false;
			return;
		}

		if (event->len == 0) {
			// The folder itself: its own entry in its parent tells of it going
			// away, if that is watched. If not, a walk will.
			if ((event->mask & IN_ATTRIB) != 0)
				_Evaluate(folder->second);
			return;
		}
		if (event->name[0] == '.')
			return;

		const std::string path = folder->second + "/" + event->name;
		const bool isDirectory = (event->mask & IN_ISDIR) != 0;

		if ((event->mask & (IN_DELETE | IN_MOVED_FROM)) != 0) {
			if (isDirectory)
				_FolderGone(path);
			else
				_Stopped(path);
		} else if ((event->mask & (IN_CREATE | IN_MOVED_TO)) != 0) {
			_Evaluate(path);
			if (isDirectory)
				_FolderCame(path);
		} else
			_Evaluate(path);
	}

	void _Evaluate(const std::string& path)
	{
		struct stat status;
		const bool exists = lstat(path.c_str(), &status) == 0;
		const std::string name = fs::path(path).filename().native();
		const bool isKnown = fKnown.find(path) != fKnown.end();
		const bool doesMatch = exists
			&& matches(fExpression, path.c_str(), name.c_str());

		if (doesMatch && !isKnown)
			_Started(path);
		else if (!doesMatch && isKnown)
			_Stopped(path);
	}

	/*!	A folder came, with what is in it: moved here, or made and filled before
		a watch could be on it.
	*/
	void _FolderCame(const std::string& path)
	{
		_Watch(path);

		VolumeWalk walk(fDevice, {fs::path(path)});
		std::string entry;
		std::string name;
		bool isDirectory;
		while (!fStop && walk.Next(&entry, &name, &isDirectory)) {
			if (isDirectory)
				_Watch(entry);
			if (matches(fExpression, entry.c_str(), name.c_str())
				&& fKnown.find(entry) == fKnown.end()) {
				_Started(entry);
			}
		}
	}

	void _FolderGone(const std::string& path)
	{
		const std::string prefix = path + "/";
		_Stopped(path);

		std::vector<std::string> gone;
		for (const auto& known : fKnown) {
			if (known.first.compare(0, prefix.size(), prefix) == 0)
				gone.push_back(known.first);
		}
		for (const std::string& entry : gone)
			_Stopped(entry);

		// A moved folder keeps its watches, under a name that is no more.
		std::vector<int> watches;
		for (const auto& watch : fWatches) {
			if (watch.second == path
				|| watch.second.compare(0, prefix.size(), prefix) == 0) {
				watches.push_back(watch.first);
			}
		}
		for (int watch : watches)
			inotify_rm_watch(fNotifyFD, watch);
	}

	void _Started(const std::string& path)
	{
		Known known;
		if (!_GetKnown(path, &known))
			return;
		fKnown[path] = known;
		_Watch(fs::path(path).parent_path().native());
		_Send(B_ENTRY_CREATED, path, known);
	}

	void _Stopped(const std::string& path)
	{
		const auto known = fKnown.find(path);
		if (known == fKnown.end())
			return;
		const Known gone = known->second;
		fKnown.erase(known);
		_Send(B_ENTRY_REMOVED, path, gone);
	}

	bool _GetKnown(const std::string& path, Known* known)
	{
		struct stat status;
		struct stat parent;
		if (lstat(path.c_str(), &status) != 0
			|| stat(fs::path(path).parent_path().c_str(), &parent) != 0) {
			return false;
		}
		known->directory = parent.st_ino;
		known->node = status.st_ino;
		return true;
	}

	void _Watch(const std::string& folder)
	{
		if (fNoMoreWatches || fWatches.size() >= fWatchLimit
			|| fWatchedFolders.find(folder) != fWatchedFolders.end()) {
			return;
		}

		const int watch = inotify_add_watch(fNotifyFD, folder.c_str(),
			IN_ATTRIB | IN_CLOSE_WRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM
				| IN_MOVED_TO | IN_ONLYDIR | IN_DONT_FOLLOW);
		if (watch < 0) {
			// The user's watches are used up: a walk will find the changes.
			fNoMoreWatches = errno == ENOSPC;
			return;
		}
		fWatches[watch] = folder;
		fWatchedFolders.insert(folder);
	}

	void _Send(int32 opcode, const std::string& path, const Known& known)
	{
		BMessage message(B_QUERY_UPDATE);
		message.AddInt32("opcode", opcode);
		message.AddInt32("device", (int32)fDevice);
		message.AddInt64("directory", (int64)known.directory);
		message.AddInt64("node", (int64)known.node);
		message.AddString("name", path.c_str());

		port_info info;
		if (get_port_info(fPort, &info) != B_OK)
			return;
		BMessenger messenger;
		BMessenger::Private(messenger).SetTo(info.team, fPort,
			fToken == -1 ? B_PREFERRED_TOKEN : fToken);
		messenger.SendMessage(&message, (BHandler*)NULL, kSendTimeout);
	}

	static size_t _Depth(const std::string& path)
	{
		return std::count(path.begin(), path.end(), '/');
	}

private:
	Expression	fExpression;
	dev_t		fDevice;
	std::vector<fs::path> fRoots;
	port_id		fPort;
	int32		fToken;

	std::thread	fThread;
	int			fNotifyFD;
	int			fWakeFD;
	std::atomic<bool> fStop;

	std::mutex	fResultsLock;
	std::condition_variable fResultsChanged;
	std::vector<Result> fResults;
	size_t		fReadPosition;
	bool		fFirstWalkDone;

	size_t		fWatchLimit;
	bool		fNoMoreWatches;
	bool		fWalkAgain;
	std::unordered_map<int, std::string> fWatches;
	std::unordered_set<std::string> fWatchedFolders;
	std::unordered_map<std::string, Known> fKnown;
};


status_t
open_query(dev_t device, const char* predicate, uint32 flags, port_id port,
	int32 token, QueryRun** _run)
{
	if (predicate == NULL || _run == NULL)
		return B_BAD_VALUE;

	QueryRun* run;
	if ((flags & B_LIVE_QUERY) != 0) {
		run = new(std::nothrow) LiveQueryRun(device, roots_of_volume(device),
			port, token);
	} else {
		run = new(std::nothrow) WalkQueryRun(device, roots_of_volume(device));
	}
	if (run == NULL)
		return B_NO_MEMORY;

	const status_t status = run->Init(predicate);
	if (status != B_OK) {
		delete run;
		return status;
	}
	*_run = run;
	return B_OK;
}


void
close_query(QueryRun* run)
{
	delete run;
}


status_t
next_query_ref(QueryRun* run, entry_ref* ref)
{
	return run->Next(ref);
}


status_t
rewind_query(QueryRun* run)
{
	return run->Rewind();
}


}	// namespace Storage
}	// namespace BPrivate


#endif	// __linux__
