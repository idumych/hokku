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
open_query(dev_t, const char*, uint32, QueryRun**)
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
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <FindDirectory.h>
#include <fs_attr.h>
#include <Path.h>

#define QUERY_EXPRESSION_ONLY
#define QUERY_FATAL(message...)		debugger("query parser")
#define QUERY_INFORM(message...)	do {} while (false)
#include <file_systems/QueryParser.h>


namespace fs = std::filesystem;


namespace BPrivate {
namespace Storage {


static const char* const kAttributeTypes = "user.COSMOE:ATTR_TYPES";
static const size_t kAttributePrefixLength = 5;	// "user."


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


class QueryRun {
public:
	QueryRun(dev_t device, std::vector<fs::path> roots)
		:
		fDevice(device),
		fRoots(std::move(roots)),
		fNextRoot(0),
		fWalking(false)
	{
	}

	status_t Init(const char* predicate)
	{
		const char* position = NULL;
		const status_t status = fExpression.Init(predicate, &position);
		if (status != B_OK)
			return status;
		return fExpression.Root() != NULL ? B_OK : B_BAD_VALUE;
	}

	status_t Rewind()
	{
		fNextRoot = 0;
		fWalking = false;
		fEntry = fs::recursive_directory_iterator();
		return B_OK;
	}

	status_t Next(entry_ref* ref)
	{
		std::error_code error;
		while (_NextEntry()) {
			const fs::path path = fEntry->path();
			const std::string name = path.filename().native();
			_Advance(name, path, error);
			if (name[0] == '.')
				continue;

			QueryEntry entry(path.c_str(), name.c_str());
			// (A status below 0 is a value that can't be compared with the one
			// in the predicate: a pattern for a number, say. It's no match.)
			if (fExpression.Root()->Match(&entry, &entry)
					!= QueryParser::MATCH_OK) {
				continue;
			}

			struct stat parent;
			if (stat(path.parent_path().c_str(), &parent) != 0)
				continue;
			ref->device = fDevice;
			ref->directory = parent.st_ino;
			return ref->set_name(path.c_str());
		}
		return B_ENTRY_NOT_FOUND;
	}

private:
	/*!	The walk is at an entry that was not looked at yet. */
	bool _NextEntry()
	{
		const fs::recursive_directory_iterator end;
		while (true) {
			if (fWalking && fEntry != end)
				return true;

			if (fNextRoot >= fRoots.size())
				return false;

			std::error_code error;
			fCurrentRoot = fRoots[fNextRoot++];
			fEntry = fs::recursive_directory_iterator(fCurrentRoot,
				fs::directory_options::skip_permission_denied, error);
			fWalking = !error;
		}
	}

	/*!	Steps off the entry, into it if it is a folder to go into. */
	void _Advance(const std::string& name, const fs::path& path,
		std::error_code& error)
	{
		struct stat status;
		if (name[0] == '.' || (fEntry->is_directory(error)
				&& (lstat(path.c_str(), &status) != 0
					|| status.st_dev != fDevice))) {
			fEntry.disable_recursion_pending();
		}
		fEntry.increment(error);
		if (error) {
			// A folder that went away meanwhile ends the iterator.
			fWalking = false;
		}
	}

private:
	Expression	fExpression;
	dev_t		fDevice;
	std::vector<fs::path> fRoots;
	size_t		fNextRoot;
	fs::path	fCurrentRoot;
	fs::recursive_directory_iterator fEntry;
	bool		fWalking;
};


status_t
open_query(dev_t device, const char* predicate, uint32 flags, QueryRun** _run)
{
	if (predicate == NULL || _run == NULL)
		return B_BAD_VALUE;
	if ((flags & B_LIVE_QUERY) != 0)
		return B_NOT_SUPPORTED;

	QueryRun* run = new(std::nothrow) QueryRun(device, roots_of_volume(device));
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
