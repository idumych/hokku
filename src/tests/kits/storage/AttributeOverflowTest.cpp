/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	Attributes that don't fit on a file: the store of what a file system refuses.
	ext4 keeps about a block of attributes for a file, all together. This program
	behaves like that wherever it runs, by having a fsetxattr() of its own, which
	libbe's calls come to: run it from a folder on a real disk.
		AttributeOverflowTest
*/


#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <filesystem>
#include <set>
#include <string>

#include <Entry.h>
#include <File.h>
#include <fs_attr.h>
#include <fs_query.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <String.h>
#include <TypeConstants.h>
#include <Volume.h>


static const ssize_t kFileLimit = 3900;
static bool sLimitOn = true;

static int sChecks = 0;
static int sFailures = 0;
static std::string sHome;
static dev_t sDevice;


#define CHECK(condition) \
	do { \
		sChecks++; \
		if (!(condition)) { \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
			sFailures++; \
		} \
	} while (false)


/*!	What ext4 does: all the attributes of a file together have a limit. */
extern "C" int
fsetxattr(int fd, const char* name, const void* value, size_t size, int flags)
{
	typedef int (*Real)(int, const char*, const void*, size_t, int);
	static Real real = (Real)dlsym(RTLD_NEXT, "fsetxattr");
	if (sLimitOn) {
		char names[64 * 1024];
		ssize_t total = strlen(name) + size;
		const ssize_t listed = flistxattr(fd, names, sizeof(names));
		for (ssize_t i = 0; i < listed; i += strlen(names + i) + 1) {
			if (strcmp(names + i, name) == 0)
				continue;
			total += strlen(names + i) + fgetxattr(fd, names + i, NULL, 0);
		}
		if (total > kFileLimit) {
			errno = ENOSPC;
			return -1;
		}
	}
	return real(fd, name, value, size, flags);
}


static std::string
value_of(size_t size, char seed = 'a')
{
	std::string value(size, '\0');
	for (size_t i = 0; i < size; i++)
		value[i] = (char)(seed + (i * 7 + i / 251) % 26);
	return value;
}


static std::string
new_file(const char* name)
{
	const std::string path = sHome + "/" + name;
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE | B_ERASE_FILE);
	CHECK(file.InitCheck() == B_OK);
	return path;
}


static bool
write_attr(BNode& node, const char* name, const std::string& value,
	type_code type = B_RAW_TYPE)
{
	return node.WriteAttr(name, type, 0, value.data(), value.size())
		== (ssize_t)value.size();
}


static std::string
read_whole(BNode& node, const char* name)
{
	attr_info info;
	if (node.GetAttrInfo(name, &info) != B_OK)
		return "<no info>";
	std::string value(info.size, '\0');
	const ssize_t bytes = node.ReadAttr(name, info.type, 0, &value[0], value.size());
	return bytes == (ssize_t)value.size() ? value : "<short read>";
}


static std::set<std::string>
listed(BNode& node)
{
	std::set<std::string> names;
	char name[B_ATTR_NAME_LENGTH];
	node.RewindAttrs();
	while (node.GetNextAttrName(name) == B_OK)
		names.insert(name);
	names.erase("COSMOE:ATTR_TYPES");
	return names;
}


static bool
on_the_file(const std::string& path, const char* name)
{
	return lgetxattr(path.c_str(), (std::string("user.") + name).c_str(), NULL, 0) >= 0;
}


static int
files_in_store()
{
	namespace fs = std::filesystem;
	int count = 0;
	std::error_code error;
	for (const auto& entry : fs::recursive_directory_iterator(
			sHome + "/.local/share/cosmoe/attr-overflow", error)) {
		if (entry.is_regular_file())
			count++;
	}
	return count;
}


static void
test_a_value_that_does_not_fit()
{
	const std::string path = new_file("large");
	BNode node(path.c_str());
	const std::string value = value_of(50000);

	CHECK(write_attr(node, "Over:Large", value, B_MESSAGE_TYPE));
	CHECK(!on_the_file(path, "Over:Large"));
	CHECK(files_in_store() == 1);

	attr_info info;
	CHECK(node.GetAttrInfo("Over:Large", &info) == B_OK);
	CHECK(info.size == (off_t)value.size());
	CHECK(info.type == B_MESSAGE_TYPE);
	CHECK(read_whole(node, "Over:Large") == value);
	CHECK(listed(node) == std::set<std::string>{"Over:Large"});

	// parts of it
	char buffer[100];
	CHECK(node.ReadAttr("Over:Large", B_RAW_TYPE, 0, buffer, 100) == 100);
	CHECK(memcmp(buffer, value.data(), 100) == 0);
	CHECK(node.ReadAttr("Over:Large", B_RAW_TYPE, 20000, buffer, 100) == 100);
	CHECK(memcmp(buffer, value.data() + 20000, 100) == 0);
	CHECK(node.ReadAttr("Over:Large", B_RAW_TYPE, 49990, buffer, 100) == 10);
	CHECK(node.ReadAttr("Over:Large", B_RAW_TYPE, 60000, buffer, 100) == 0);

	// replaced, in the store
	const std::string other = value_of(60000, 'b');
	CHECK(write_attr(node, "Over:Large", other));
	CHECK(read_whole(node, "Over:Large") == other);
	CHECK(files_in_store() == 1);

	// small again: it's on the file, and gone from the store with its mark
	const std::string small = value_of(100, 'c');
	CHECK(write_attr(node, "Over:Large", small));
	CHECK(on_the_file(path, "Over:Large"));
	CHECK(read_whole(node, "Over:Large") == small);
	CHECK(files_in_store() == 0);
	CHECK(!on_the_file(path, "COSMOE:OVERFLOW"));

	// back in the store, then removed
	CHECK(write_attr(node, "Over:Large", value));
	CHECK(node.RemoveAttr("Over:Large") == B_OK);
	CHECK(files_in_store() == 0);
	CHECK(!on_the_file(path, "COSMOE:OVERFLOW"));
	CHECK(node.GetAttrInfo("Over:Large", &info) == B_ENTRY_NOT_FOUND);
	CHECK(node.RemoveAttr("Over:Large") == B_ENTRY_NOT_FOUND);
	CHECK(listed(node).empty());
}


static void
test_the_largest_makes_room()
{
	const std::string path = new_file("room");
	BNode node(path.c_str());
	const std::string large = value_of(3500);
	BString id("0ID1");

	// the file is full, and a small value still goes on it: the large one moves
	CHECK(write_attr(node, "Room:Large", large));
	CHECK(on_the_file(path, "Room:Large"));
	const std::string middle = value_of(600, 'd');
	CHECK(write_attr(node, "Room:Middle", middle));
	CHECK(node.WriteAttrString("SEN:ID", &id) == B_OK);
	CHECK(on_the_file(path, "SEN:ID"));
	CHECK(on_the_file(path, "Room:Middle"));
	CHECK(!on_the_file(path, "Room:Large"));
	CHECK(read_whole(node, "Room:Large") == large);
	CHECK(read_whole(node, "Room:Middle") == middle);
	BString text;
	CHECK(node.ReadAttrString("SEN:ID", &text) == B_OK && text == "0ID1");
	CHECK(listed(node) == (std::set<std::string>{"Room:Large", "Room:Middle", "SEN:ID"}));

	// it's the largest that goes, whatever order they came in
	const std::string order = new_file("order");
	BNode orderNode(order.c_str());
	CHECK(write_attr(orderNode, "Order:Small", value_of(100, 'h')));
	CHECK(write_attr(orderNode, "Order:Middle", value_of(800, 'i')));
	CHECK(write_attr(orderNode, "Order:Large", value_of(2500, 'j')));
	CHECK(write_attr(orderNode, "Order:New", value_of(1200, 'k')));
	CHECK(on_the_file(order, "Order:Small"));
	CHECK(on_the_file(order, "Order:Middle"));
	CHECK(on_the_file(order, "Order:New"));
	CHECK(!on_the_file(order, "Order:Large"));
	CHECK(read_whole(orderNode, "Order:Large") == value_of(2500, 'j'));

	// many of a size
	const std::string many = new_file("many");
	BNode manyNode(many.c_str());
	std::set<std::string> names;
	for (int i = 0; i < 40; i++) {
		const std::string name = "Many:" + std::to_string(i);
		names.insert(name);
		CHECK(write_attr(manyNode, name.c_str(), value_of(500, 'a' + i % 20)));
	}
	CHECK(listed(manyNode) == names);
	for (int i = 0; i < 40; i++) {
		const std::string name = "Many:" + std::to_string(i);
		CHECK(read_whole(manyNode, name.c_str()) == value_of(500, 'a' + i % 20));
	}

	// a name with what a file's name can't have
	const std::string named = new_file("named");
	BNode namedNode(named.c_str());
	const char* odd = "SEN:REL:relation/x-vnd.sen-labs.100%.reference";
	CHECK(write_attr(namedNode, odd, value_of(9000)));
	CHECK(!on_the_file(named, odd));
	CHECK(read_whole(namedNode, odd) == value_of(9000));
	CHECK(listed(namedNode) == std::set<std::string>{odd});
}


static void
test_it_stays_with_the_file()
{
	const std::string path = new_file("moving");
	const std::string value = value_of(20000);
	{
		BNode node(path.c_str());
		CHECK(write_attr(node, "Move:Attr", value));
	}
	std::filesystem::create_directory(sHome + "/elsewhere");
	const std::string moved = sHome + "/elsewhere/moved";
	CHECK(rename(path.c_str(), moved.c_str()) == 0);
	BNode node(moved.c_str());
	CHECK(read_whole(node, "Move:Attr") == value);

	BNode(moved.c_str()).RemoveAttr("Move:Attr");
}


static void
test_another_file_gets_nothing()
{
	// A copy brings the mark along, as cp -a does, but the store has nothing for
	// it: it must not get another file's value, nor fail in another way.
	const std::string original = new_file("original");
	BNode node(original.c_str());
	CHECK(write_attr(node, "Copy:Attr", value_of(20000)));

	const std::string copy = sHome + "/copy";
	CHECK(system(("cp -a '" + original + "' '" + copy + "'").c_str()) == 0);
	CHECK(on_the_file(copy, "COSMOE:OVERFLOW"));
	BNode copied(copy.c_str());
	attr_info info;
	CHECK(copied.GetAttrInfo("Copy:Attr", &info) == B_ENTRY_NOT_FOUND);
	char buffer[10];
	CHECK(copied.ReadAttr("Copy:Attr", B_RAW_TYPE, 0, buffer, 10) == B_ENTRY_NOT_FOUND);
	CHECK(listed(copied).empty());
	// and it can have one of its own
	CHECK(write_attr(copied, "Copy:Attr", value_of(30000, 'e')));
	CHECK(read_whole(copied, "Copy:Attr") == value_of(30000, 'e'));
	CHECK(read_whole(node, "Copy:Attr") == value_of(20000));

	// What tells a file from a later one with the same inode: its folder in the
	// store is named after both. (A file system can't be made to reuse an inode.)
	struct statx identity;
	CHECK(statx(AT_FDCWD, original.c_str(), 0, STATX_BTIME | STATX_INO, &identity) == 0);
	const std::string expected = std::to_string(identity.stx_ino) + "-"
		+ std::to_string(identity.stx_btime.tv_sec) + "-"
		+ std::to_string(identity.stx_btime.tv_nsec);
	bool found = false;
	std::error_code error;
	for (const auto& entry : std::filesystem::recursive_directory_iterator(
			sHome + "/.local/share/cosmoe/attr-overflow", error)) {
		if (entry.path().filename() == expected)
			found = true;
	}
	CHECK(found);

	// A file deleted: a new one, even if it were to get the inode, has another
	// birth time, so what is left in the store is nothing to it.
	CHECK(unlink(copy.c_str()) == 0);
	const std::string again = new_file("copy");
	BNode newFile(again.c_str());
	CHECK(newFile.GetAttrInfo("Copy:Attr", &info) == B_ENTRY_NOT_FOUND);
}


static void
test_the_store_is_private()
{
	namespace fs = std::filesystem;
	const std::string path = new_file("private");
	BNode node(path.c_str());
	CHECK(write_attr(node, "Private:Attr", value_of(20000)));
	std::error_code error;
	for (const auto& entry : fs::recursive_directory_iterator(
			sHome + "/.local/share/cosmoe/attr-overflow", error)) {
		struct stat status;
		CHECK(lstat(entry.path().c_str(), &status) == 0);
		CHECK((status.st_mode & 077) == 0);
	}
}


static void
test_failing_keeps_the_old_value()
{
	// There is nowhere to put it: an error, and the attribute is what it was.
	const std::string path = new_file("failing");
	BNode node(path.c_str());
	const std::string old = value_of(200, 'f');
	CHECK(write_attr(node, "Fail:Attr", old));

	const std::string home = sHome;
	setenv("HOME", "/nonexistent/home", 1);
	const std::string value = value_of(20000);
	const ssize_t result = node.WriteAttr("Fail:Attr", B_RAW_TYPE, 0, value.data(),
		value.size());
	setenv("HOME", home.c_str(), 1);
	CHECK(result == B_DEVICE_FULL);
	CHECK(read_whole(node, "Fail:Attr") == old);
	CHECK(on_the_file(path, "Fail:Attr"));
	CHECK(!on_the_file(path, "COSMOE:OVERFLOW"));

	// a value that was in the store survives a failure, too
	const std::string stored = value_of(30000, 'g');
	CHECK(write_attr(node, "Fail:Attr", stored));
	setenv("HOME", "/nonexistent/home", 1);
	CHECK(node.WriteAttr("Fail:Attr", B_RAW_TYPE, 0, value.data(), value.size())
		< 0);
	setenv("HOME", home.c_str(), 1);
	CHECK(read_whole(node, "Fail:Attr") == stored);
}


static void
test_queries_see_it()
{
	const std::string path = new_file("queried");
	BNode node(path.c_str());
	std::string value = "0ABC,";
	value += value_of(20000);
	value.push_back('\0');
	CHECK(write_attr(node, "Query:Over", value, B_STRING_TYPE));
	CHECK(!on_the_file(path, "Query:Over"));

	for (const uint32 flags : {(uint32)B_QUERY_NON_INDEXED, (uint32)0}) {
		BVolume volume(sDevice);
		BQuery query;
		query.SetVolume(&volume);
		query.SetFlags(flags);
		query.SetPredicate("Query:Over==\"0ABC*\"");
		CHECK(query.Fetch() == B_OK);
		entry_ref ref;
		CHECK(query.GetNextRef(&ref) == B_OK);
		CHECK(strcmp(BPath(&ref).Leaf(), "queried") == 0);
		CHECK(query.GetNextRef(&ref) == B_ENTRY_NOT_FOUND);
	}
}


static void
test_without_a_limit_nothing_changes()
{
	// What fits goes on the file, and nothing is made in the store.
	const std::string store = sHome + "/.local/share/cosmoe/attr-overflow";
	const int before = files_in_store();
	const std::string path = new_file("fits");
	BNode node(path.c_str());
	CHECK(write_attr(node, "Fits:Attr", value_of(1000)));
	CHECK(on_the_file(path, "Fits:Attr"));
	CHECK(!on_the_file(path, "COSMOE:OVERFLOW"));
	CHECK(files_in_store() == before);
}


int
main()
{
	char folder[] = "attribute-overflow-test-XXXXXX";
	if (mkdtemp(folder) == NULL) {
		perror("mkdtemp");
		return 2;
	}
	sHome = std::filesystem::absolute(folder).string();
	setenv("HOME", sHome.c_str(), 1);
	unsetenv("XDG_CONFIG_HOME");
	unsetenv("XDG_CACHE_HOME");
	unsetenv("XDG_DATA_HOME");
	struct stat status;
	stat(sHome.c_str(), &status);
	sDevice = status.st_dev;

	test_without_a_limit_nothing_changes();
	test_a_value_that_does_not_fit();
	test_the_largest_makes_room();
	test_it_stays_with_the_file();
	test_another_file_gets_nothing();
	test_the_store_is_private();
	test_failing_keeps_the_old_value();
	test_queries_see_it();

	sLimitOn = false;
	std::error_code ignored;
	std::filesystem::remove_all(sHome, ignored);

	printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
