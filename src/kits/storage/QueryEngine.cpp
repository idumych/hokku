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

	Most files have no attributes (70,000 of 4.5 million in a home folder that
	was measured), so a list of those that do makes up for the indices: a
	predicate that can only match a file with an attribute looks at those files
	alone. The list has no values, so what it says of a file is read from the
	file, and is never out of date. It is kept in the cache folder, and is what
	a walk finds, plus what BNode::WriteAttr() adds in any program. It can only
	lack what got its attributes from elsewhere (setfattr, cp -a, a restore) or
	was moved by another program, until the next walk: one is made in the
	background when a query finds the list old, or an entry of it gone, by one
	program at a time. B_QUERY_NON_INDEXED looks at every file, for an exact
	answer. A predicate on "name", "size" or "last_modified" alone must always
	walk.

	A live query works on a thread of its own, so that Fetch() does not wait for
	it. BFS tells a live query of every change to an index. Here, watching every
	folder is out of the question: a home folder can have 400,000 of them, and
	a user gets about 500,000 inotify watches for all of their programs. So a query
	watches some folders, those that it is most likely to matter in (the ones it
	starts from, the ones that have a match, then the ones nearest the start),
	and hears of a change in those at once. A change anywhere else is found by
	looking again every kWalkInterval: at the list, or, if a query can't use it,
	by walking, at a low priority, and so that it stays around 3% of one CPU.
*/


#include <QueryEngine.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>
#include <string>
#include <vector>

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


std::vector<std::string>
folders_to_watch(dev_t)
{
	return std::vector<std::string>();
}


void
note_attribute_written(int)
{
}


}	// namespace Storage
}	// namespace BPrivate


#else	// __linux__


#include <fcntl.h>
#include <mntent.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
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
#include <memory>
#include <mutex>
#include <set>
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
#include <MessagePrivate.h>
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

static const char* const kIndexHeader = "hokku-query-index 1";
static const int kIndexMaxAge = 600;			// in seconds, at the least
static const int kIndexCostFactor = 100;		// of what a walk takes
static const int kIndexGoneMaxAge = 60;			// with an entry known to be gone


extern "C" {
ssize_t _cosmoe_attr_overflow_read(const char* filePath, const char* attribute,
	void* buffer, size_t size, uint32* _type);
int _cosmoe_attr_overflow_exists(void);
}

/*!	Whether this user has attributes in the store at all. Without any, a query
	saves looking for each attribute that a file doesn't have.
*/
static bool sOverflowExists = false;


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
		} else if (length < 0) {
			// It can be in the store of what doesn't fit on the file (fs.c).
			uint32 storedType;
			if (errno != ENODATA || !sOverflowExists)
				return B_ENTRY_NOT_FOUND;
			length = _cosmoe_attr_overflow_read(node->path, attribute, buffer,
				capacity, &storedType);
			if (length < 0)
				return B_ENTRY_NOT_FOUND;
			buffer[length] = '\0';
			*type = (int32)storedType;
			*size = is_string_type(*type) ? strlen((const char*)buffer) : length;
			return B_OK;
		}

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


/*!	The folders nearest the start, without a walk of everything: the ones to
	watch, of those where there can be too many.
*/
static std::vector<std::string>
nearest_folders(dev_t device, const std::vector<fs::path>& roots, size_t limit,
	const std::atomic<bool>* stop)
{
	std::vector<std::string> folders;
	std::vector<std::string> level;
	for (const fs::path& root : roots)
		level.push_back(root.native());

	while (!level.empty() && !*stop && folders.size() < limit) {
		std::vector<std::string> next;
		for (const std::string& folder : level) {
			std::error_code error;
			fs::directory_iterator entry(folder,
				fs::directory_options::skip_permission_denied, error);
			const fs::directory_iterator end;
			while (!error && entry != end && folders.size() < limit) {
				const fs::path path = entry->path();
				std::error_code ignored;
				struct stat status;
				if (path.filename().native()[0] != '.'
					&& !entry->is_symlink(ignored)
					&& entry->is_directory(ignored)
					&& lstat(path.c_str(), &status) == 0
					&& status.st_dev == device) {
					folders.push_back(path.native());
					next.push_back(path.native());
				}
				entry.increment(error);
			}
		}
		level.swap(next);
	}
	return folders;
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


static int
environment_value(const char* name, int fallback)
{
	const char* value = getenv(name);
	return value != NULL && atoi(value) > 0 ? atoi(value) : fallback;
}


/*!	Whether only a file with an attribute can match: then the list will do. */
static bool
requires_attribute(QueryParser::Term<QueryPolicy>* term)
{
	const int8 op = term->Op();
	if (op == QueryParser::OP_AND || op == QueryParser::OP_OR) {
		QueryParser::Operator<QueryPolicy>* both
			= static_cast<QueryParser::Operator<QueryPolicy>*>(term);
		const bool left = requires_attribute(both->Left());
		const bool right = requires_attribute(both->Right());
		return op == QueryParser::OP_AND ? left || right : left && right;
	}

	const char* attribute
		= static_cast<QueryParser::Equation<QueryPolicy>*>(term)->Attribute();
	return strcmp(attribute, "name") != 0 && strcmp(attribute, "size") != 0
		&& strcmp(attribute, "last_modified") != 0;
}


/*!	Whether an entry has an attribute. The list's attributes are the user's. */
static bool
has_user_attribute(const char* path)
{
	char names[16 * 1024];
	const ssize_t size = llistxattr(path, names, sizeof(names));
	if (size < 0)
		return errno == ERANGE;
	for (ssize_t offset = 0; offset < size;
			offset += strlen(names + offset) + 1) {
		if (strncmp(names + offset, "user.", kAttributePrefixLength) == 0)
			return true;
	}
	return false;
}


/*!	The file with the list of what has attributes below some folders, which is
	a header line and then the paths, each ending in a null character (a name
	can have a line end in it). It holds nothing that can't be found again.
*/
static std::string
index_path(const std::vector<fs::path>& roots)
{
	if (roots.empty())
		return std::string();

	BPath cache;
	if (find_directory(B_USER_CACHE_DIRECTORY, &cache) != B_OK)
		return std::string();

	const fs::path folder = fs::path(cache.Path()) / "query-index";
	std::error_code error;
	fs::create_directories(folder, error);
	if (error)
		return std::string();
	chmod(folder.c_str(), 0700);

	std::string key;
	for (const fs::path& root : roots)
		key += root.native() + ":";
	char name[32];
	snprintf(name, sizeof(name), "%016zx", std::hash<std::string>()(key));
	return (folder / name).native();
}


/*!	A lock on the index, between programs: writers of entries hold it shared,
	while the one who replaces the file holds it exclusively. The file is
	replaced, not changed.
*/
class IndexLock {
public:
	IndexLock(const std::string& index, int operation)
	{
		fFD = open((index + ".lock").c_str(), O_RDWR | O_CREAT | O_CLOEXEC,
			0600);
		if (fFD >= 0)
			flock(fFD, operation);
	}

	~IndexLock()
	{
		if (fFD >= 0)
			close(fFD);
	}

private:
	int fFD;
};


struct IndexContents {
	std::vector<std::string> paths;
	time_t		walked = 0;
	int			walkSeconds = 0;
};


/*!	Reads an index, whole. If it is damaged, it is as if it wasn't there. */
static bool
read_index(const std::string& index, IndexContents* contents,
	off_t from = 0)
{
	if (index.empty())
		return false;

	const int fd = open(index.c_str(), O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return false;

	std::string data;
	struct stat status;
	bool isRead = fstat(fd, &status) == 0;
	if (isRead) {
		data.resize(status.st_size);
		size_t done = 0;
		while (done < data.size()) {
			const ssize_t bytes = read(fd, &data[done], data.size() - done);
			if (bytes <= 0)
				break;
			done += bytes;
		}
		data.resize(done);
	}
	close(fd);
	if (!isRead)
		return false;

	const size_t lineEnd = data.find('\n');
	const size_t headerLength = strlen(kIndexHeader);
	if (lineEnd == std::string::npos
		|| data.compare(0, headerLength, kIndexHeader) != 0) {
		return false;
	}
	long long walked = 0;
	int walkSeconds = 0;
	if (sscanf(data.c_str() + headerLength, " %lld %d", &walked, &walkSeconds)
			!= 2) {
		return false;
	}
	contents->walked = walked;
	contents->walkSeconds = walkSeconds;

	// (Those that were added to an index more than once count once.)
	std::unordered_set<std::string> seen;
	size_t position = std::max((size_t)from, lineEnd + 1);
	while (position < data.size()) {
		const size_t end = data.find('\0', position);
		if (end == std::string::npos)
			break;		// half of an entry: being written, or a crash
		std::string path = data.substr(position, end - position);
		position = end + 1;
		if (seen.insert(path).second)
			contents->paths.push_back(std::move(path));
	}
	return true;
}


static bool
index_is_old(const IndexContents& contents, bool hasGoneEntry)
{
	const int age = hasGoneEntry
		? environment_value("HOKKU_QUERY_INDEX_GONE_MAX_AGE",
			std::max(kIndexGoneMaxAge, contents.walkSeconds * kWalkCostFactor))
		: environment_value("HOKKU_QUERY_INDEX_MAX_AGE",
			std::max(kIndexMaxAge, contents.walkSeconds * kIndexCostFactor));
	return time(NULL) - contents.walked >= age;
}


/*!	Collects what a walk finds, for a new index. */
class IndexBuilder {
public:
	IndexBuilder(std::string index)
		:
		fIndex(std::move(index)),
		fStart(time(NULL)),
		fStartNode(0),
		fStartSize(0)
	{
		// What gets added to the index during the walk is to stay.
		IndexLock lock(fIndex, LOCK_SH);
		struct stat status;
		if (stat(fIndex.c_str(), &status) == 0) {
			fStartNode = status.st_ino;
			fStartSize = status.st_size;
		}
	}

	void Add(const std::string& path)
	{
		fPaths.push_back(path);
	}

	void Commit()
	{
		IndexLock lock(fIndex, LOCK_EX);

		IndexContents added;
		struct stat status;
		if (stat(fIndex.c_str(), &status) == 0) {
			read_index(fIndex, &added,
				status.st_ino == fStartNode ? fStartSize : 0);
		}

		const std::string temporary = fIndex + ".new";
		const int fd = open(temporary.c_str(),
			O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
		if (fd < 0)
			return;

		char header[128];
		std::string data(header, snprintf(header, sizeof(header), "%s %lld %d\n",
			kIndexHeader, (long long)fStart, (int)(time(NULL) - fStart)));
		for (const std::string& path : fPaths)
			data.append(path).push_back('\0');
		for (const std::string& path : added.paths)
			data.append(path).push_back('\0');

		size_t done = 0;
		while (done < data.size()) {
			const ssize_t bytes = write(fd, data.data() + done,
				data.size() - done);
			if (bytes <= 0)
				break;
			done += bytes;
		}
		close(fd);
		if (done == data.size())
			rename(temporary.c_str(), fIndex.c_str());
		else
			unlink(temporary.c_str());
	}

private:
	std::string	fIndex;
	time_t		fStart;
	ino_t		fStartNode;
	off_t		fStartSize;
	std::vector<std::string> fPaths;
};


/*!	Makes a new index in the background, unless this or another program is at it.
	The thread is left to itself; if the program ends first, the old index stays.
*/
static void
start_walking_for_index(dev_t device, const std::vector<fs::path>& roots,
	const std::string& index)
{
	if (index.empty())
		return;

	static std::mutex* sLock = new std::mutex;
	static std::set<std::string>* sRunning = new std::set<std::string>;
	{
		std::lock_guard<std::mutex> lock(*sLock);
		if (!sRunning->insert(index).second)
			return;
	}

	std::thread([device, roots, index] {
		setpriority(PRIO_PROCESS, gettid(), 10);

		const int walker = open((index + ".walk").c_str(),
			O_RDWR | O_CREAT | O_CLOEXEC, 0600);
		if (walker >= 0 && flock(walker, LOCK_EX | LOCK_NB) == 0) {
			IndexBuilder builder(index);
			VolumeWalk walk(device, roots);
			std::string path;
			std::string name;
			bool isDirectory;
			while (walk.Next(&path, &name, &isDirectory)) {
				if (has_user_attribute(path.c_str()))
					builder.Add(path);
			}
			// (A test's way to write something while a walk is going on.)
			sleep(environment_value("HOKKU_QUERY_INDEX_COMMIT_DELAY", 0));
			builder.Commit();
		}
		if (walker >= 0)
			close(walker);

		std::lock_guard<std::mutex> lock(*sLock);
		sRunning->erase(index);
	}).detach();
}


class QueryRun {
public:
	virtual ~QueryRun() {}

	virtual status_t Init(const char* predicate) = 0;
	virtual status_t Next(entry_ref* ref) = 0;
	virtual status_t Rewind() = 0;
};


/*!	A query that is over when its entries have been read. It goes only as far as
	it is read.
*/
class WalkQueryRun : public QueryRun {
public:
	WalkQueryRun(dev_t device, std::vector<fs::path> roots, uint32 flags)
		:
		fDevice(device),
		fRoots(std::move(roots)),
		fFlags(flags),
		fWalk(device, fRoots),
		fRequiresAttribute(false),
		fUsesIndex(false),
		fPosition(0),
		fHasGoneEntry(false)
	{
	}

	virtual ~WalkQueryRun()
	{
		// It did not get to the end, and there still is no index.
		if (fBuilder)
			start_walking_for_index(fDevice, fRoots, fIndex);
	}

	virtual status_t Init(const char* predicate)
	{
		const status_t status = init_expression(fExpression, predicate);
		if (status != B_OK)
			return status;

		fRequiresAttribute = requires_attribute(fExpression.Root());
		fIndex = index_path(fRoots);

		const bool hasIndex = read_index(fIndex, &fContents);
		fUsesIndex = hasIndex && fRequiresAttribute
			&& (fFlags & B_QUERY_NON_INDEXED) == 0;
		if (!hasIndex && !fIndex.empty())
			fBuilder.reset(new IndexBuilder(fIndex));
		else if (hasIndex && index_is_old(fContents, false))
			start_walking_for_index(fDevice, fRoots, fIndex);
		return B_OK;
	}

	virtual status_t Next(entry_ref* ref)
	{
		return fUsesIndex ? _NextFromIndex(ref) : _NextFromWalk(ref);
	}

	virtual status_t Rewind()
	{
		fPosition = 0;
		fWalk.Rewind();
		if (fBuilder)
			fBuilder.reset(new IndexBuilder(fIndex));
		return B_OK;
	}

private:
	status_t _NextFromIndex(entry_ref* ref)
	{
		while (fPosition < fContents.paths.size()) {
			const std::string& path = fContents.paths[fPosition++];
			const std::string name = fs::path(path).filename().native();
			if (matches(fExpression, path.c_str(), name.c_str())) {
				const status_t status = _SetRef(ref, path);
				if (status != B_ENTRY_NOT_FOUND)
					return status;
			}

			struct stat status;
			if (lstat(path.c_str(), &status) != 0)
				fHasGoneEntry = true;
		}

		if (fHasGoneEntry && index_is_old(fContents, true))
			start_walking_for_index(fDevice, fRoots, fIndex);
		return B_ENTRY_NOT_FOUND;
	}

	status_t _NextFromWalk(entry_ref* ref)
	{
		std::string path;
		std::string name;
		bool isDirectory;
		while (fWalk.Next(&path, &name, &isDirectory)) {
			if (fBuilder) {
				const bool hasAttribute = has_user_attribute(path.c_str());
				if (hasAttribute)
					fBuilder->Add(path);
				else if (fRequiresAttribute)
					continue;
			}

			if (!matches(fExpression, path.c_str(), name.c_str()))
				continue;

			const status_t status = _SetRef(ref, path);
			if (status != B_ENTRY_NOT_FOUND)
				return status;
		}

		if (fBuilder) {
			fBuilder->Commit();
			fBuilder.reset();
		}
		return B_ENTRY_NOT_FOUND;
	}

	status_t _SetRef(entry_ref* ref, const std::string& path)
	{
		struct stat parent;
		if (stat(fs::path(path).parent_path().c_str(), &parent) != 0)
			return B_ENTRY_NOT_FOUND;
		return set_ref(ref, fDevice, parent.st_ino, path.c_str());
	}

private:
	Expression	fExpression;
	dev_t		fDevice;
	std::vector<fs::path> fRoots;
	uint32		fFlags;
	VolumeWalk	fWalk;

	bool		fRequiresAttribute;
	std::string	fIndex;
	IndexContents fContents;
	bool		fUsesIndex;
	size_t		fPosition;
	bool		fHasGoneEntry;
	std::unique_ptr<IndexBuilder> fBuilder;
};


/*!	A query that goes on telling its target what starts and stops matching.

	Everything but Next() and Rewind() happens on its thread: nothing else
	touches the expression, the watches or what is known to match.
*/
class LiveQueryRun : public QueryRun {
public:
	LiveQueryRun(dev_t device, std::vector<fs::path> roots, uint32 flags,
		port_id port, int32 token)
		:
		fDevice(device),
		fRoots(std::move(roots)),
		fFlags(flags),
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

		fCanUseIndex = requires_attribute(fExpression.Root())
			&& (fFlags & B_QUERY_NON_INDEXED) == 0;
		fIndex = index_path(fRoots);

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
		std::unordered_set<std::string> withMatch;

		IndexContents contents;
		const bool hasIndex = read_index(fIndex, &contents);
		if (hasIndex && fCanUseIndex) {
			for (const std::string& path : contents.paths) {
				if (fStop)
					break;
				const std::string name = fs::path(path).filename().native();
				if (matches(fExpression, path.c_str(), name.c_str()))
					_AddFirstResult(path, &withMatch);
			}
			for (std::string& folder : nearest_folders(fDevice, fRoots,
					fWatchLimit, &fStop)) {
				candidates.push_back({_Depth(folder), std::move(folder)});
			}
		} else {
			std::unique_ptr<IndexBuilder> builder;
			if (!hasIndex && !fIndex.empty())
				builder.reset(new IndexBuilder(fIndex));

			VolumeWalk walk(fDevice, fRoots);
			std::string path;
			std::string name;
			bool isDirectory;
			while (!fStop && walk.Next(&path, &name, &isDirectory)) {
				if (isDirectory)
					_AddCandidate(&candidates, path);
				if (builder && has_user_attribute(path.c_str()))
					builder->Add(path);
				if (matches(fExpression, path.c_str(), name.c_str()))
					_AddFirstResult(path, &withMatch);
			}
			if (builder && !fStop)
				builder->Commit();
		}
		if (hasIndex && index_is_old(contents, false))
			start_walking_for_index(fDevice, fRoots, fIndex);

		{
			std::lock_guard<std::mutex> lock(fResultsLock);
			fFirstWalkDone = true;
			fResultsChanged.notify_all();
		}

		for (const fs::path& root : fRoots)
			_Watch(root.native());
		for (const std::string& folder : withMatch)
			_Watch(folder);
		std::sort(candidates.begin(), candidates.end());
		for (const Candidate& candidate : candidates)
			_Watch(candidate.path);
	}

	void _AddFirstResult(const std::string& path,
		std::unordered_set<std::string>* withMatch)
	{
		Known known;
		if (!_GetKnown(path, &known))
			return;

		fKnown[path] = known;
		withMatch->insert(fs::path(path).parent_path().native());

		std::lock_guard<std::mutex> lock(fResultsLock);
		fResults.push_back({path, known.directory});
		fResultsChanged.notify_one();
	}

	void _AddCandidate(std::vector<Candidate>* candidates,
		const std::string& path)
	{
		// Of the folders only the nearest are kept, so that a home folder's
		// 400,000 aren't all in memory for a moment.
		candidates->push_back({_Depth(path), path});
		if (candidates->size() > 2 * fWatchLimit) {
			std::nth_element(candidates->begin(),
				candidates->begin() + fWatchLimit, candidates->end());
			candidates->resize(fWatchLimit);
		}
	}

	void _WalkAgain()
	{
		std::unordered_set<std::string> seen;

		IndexContents contents;
		const bool hasIndex = read_index(fIndex, &contents);
		bool hasGoneEntry = false;
		if (hasIndex && fCanUseIndex) {
			for (const std::string& path : contents.paths) {
				if (fStop)
					return;
				const std::string name = fs::path(path).filename().native();
				if (matches(fExpression, path.c_str(), name.c_str())) {
					seen.insert(path);
					if (fKnown.find(path) == fKnown.end())
						_Started(path);
				} else {
					struct stat status;
					if (lstat(path.c_str(), &status) != 0)
						hasGoneEntry = true;
				}
			}
		} else {
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
		}
		if (fStop)
			return;

		if (hasIndex && (index_is_old(contents, false)
				|| (hasGoneEntry && index_is_old(contents, true)))) {
			start_walking_for_index(fDevice, fRoots, fIndex);
		}

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
		// (From the system, as on Haiku: see send_node_monitor_message().)
		BMessage::Private(&message).SetNoReturnAddress();
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
	uint32		fFlags;
	port_id		fPort;
	int32		fToken;
	bool		fCanUseIndex = false;
	std::string	fIndex;

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

	sOverflowExists = _cosmoe_attr_overflow_exists() != 0;

	QueryRun* run;
	if ((flags & B_LIVE_QUERY) != 0) {
		run = new(std::nothrow) LiveQueryRun(device, roots_of_volume(device),
			flags, port, token);
	} else {
		run = new(std::nothrow) WalkQueryRun(device, roots_of_volume(device),
			flags);
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


std::vector<std::string>
folders_to_watch(dev_t device)
{
	const std::vector<fs::path> roots = roots_of_volume(device);
	const std::atomic<bool> stop(false);
	std::vector<std::string> folders;
	for (const fs::path& root : roots)
		folders.push_back(root.native());
	for (std::string& folder : nearest_folders(device, roots,
			environment_value("HOKKU_QUERY_WATCH_LIMIT", kDefaultWatchLimit),
			&stop)) {
		folders.push_back(std::move(folder));
	}
	return folders;
}


void
note_attribute_written(int fd)
{
	char link[64];
	char path[B_PATH_NAME_LENGTH];
	snprintf(link, sizeof(link), "/proc/self/fd/%d", fd);
	const ssize_t length = readlink(link, path, sizeof(path) - 1);
	if (length <= 0)
		return;
	path[length] = '\0';

	// It is enough to say so once in a while: most writes are to files that are
	// in the list already. It can't be once and for all, since a file can go
	// and come back.
	static std::mutex sLock;
	static std::unordered_set<std::string> sRecent;
	static time_t sRecentSince = 0;
	{
		std::lock_guard<std::mutex> lock(sLock);
		const time_t now = time(NULL);
		if (now - sRecentSince >= kWalkInterval) {
			sRecent.clear();
			sRecentSince = now;
		}
		if (!sRecent.insert(path).second)
			return;
	}

	struct stat status;
	if (fstat(fd, &status) != 0)
		return;

	// Only what a walk would find.
	const std::vector<fs::path> roots = roots_of_volume(status.st_dev);
	bool isBelowRoot = false;
	for (const fs::path& root : roots) {
		const std::string prefix = root.native() + "/";
		if (strncmp(path, prefix.c_str(), prefix.size()) == 0
			&& strstr(path + prefix.size() - 1, "/.") == NULL) {
			isBelowRoot = true;
			break;
		}
	}
	if (!isBelowRoot)
		return;

	// If there is no index, the next walk is going to make one.
	const std::string index = index_path(roots);
	if (index.empty())
		return;
	IndexLock lock(index, LOCK_SH);
	const int file = open(index.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC);
	if (file < 0)
		return;
	(void)!write(file, path, length + 1);
	close(file);
}


}	// namespace Storage
}	// namespace BPrivate


#endif	// __linux__
