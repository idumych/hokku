/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	What a Linux file system can't do by itself and a program expects anyway:
	attributes of any size, an index for everything and watch_volume(). Run it
	from a folder on a real disk (a file system where a value has a limit, like
	btrfs, is what the first test is about):
		LargeAttributeTest
*/


#include <Application.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <fs_index.h>
#include <fs_query.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <Query.h>
#include <String.h>
#include <TypeConstants.h>
#include <Volume.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <vector>


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


static std::string
test_value(size_t size, char seed = 'a')
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


static std::string
read_whole(BNode& node, const char* attribute, type_code type = B_RAW_TYPE)
{
	attr_info info;
	if (node.GetAttrInfo(attribute, &info) != B_OK)
		return "<no info>";
	std::string value(info.size, '\0');
	const ssize_t bytes = node.ReadAttr(attribute, type, 0, &value[0],
		value.size());
	return bytes == (ssize_t)value.size() ? value : "<short read>";
}


/*!	The names that a program sees. */
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


/*!	How many extended attributes the file really has for continuations. */
static int
continuations(const std::string& path)
{
	char names[256 * 1024];
	const ssize_t size = llistxattr(path.c_str(), names, sizeof(names));
	int count = 0;
	for (ssize_t i = 0; i < size; i += strlen(names + i) + 1) {
		if (strstr(names + i, "COSMOE:MORE:") != NULL)
			count++;
	}
	return count;
}


static void
test_sizes()
{
	const std::string path = new_file("sizes");
	BNode node(path.c_str());

	// around where a value is split, and well beyond where btrfs stops
	for (size_t size : {(size_t)0, (size_t)1, (size_t)255, (size_t)4095,
			(size_t)4096, (size_t)4097, (size_t)8191, (size_t)8192, (size_t)8193,
			(size_t)12288, (size_t)12289, (size_t)16000, (size_t)65536,
			(size_t)200000, (size_t)1000000}) {
		const std::string value = test_value(size);
		const ssize_t written = node.WriteAttr("Big:Attr", B_RAW_TYPE, 0,
			value.data(), value.size());
		if (written != (ssize_t)size) {
			printf("FAIL write of %zu bytes: %s\n", size, strerror(written));
			sChecks++;
			sFailures++;
			continue;
		}
		attr_info info;
		CHECK(node.GetAttrInfo("Big:Attr", &info) == B_OK);
		CHECK(info.size == (off_t)size);
		CHECK(info.type == B_RAW_TYPE);
		CHECK(read_whole(node, "Big:Attr") == value);
	}
}


static void
test_what_stays_one_value()
{
	const std::string path = new_file("small");
	BNode node(path.c_str());

	// other programs see what fits as they always did
	const std::string value = test_value(3000);
	CHECK(node.WriteAttr("Small:Attr", B_RAW_TYPE, 0, value.data(),
		value.size()) == (ssize_t)value.size());
	CHECK(continuations(path) == 0);
	char raw[4096];
	CHECK(lgetxattr(path.c_str(), "user.Small:Attr", raw, sizeof(raw)) == 3000);

	// exactly the size of a part, and not split
	const std::string exact = test_value(4096);
	CHECK(node.WriteAttr("Exact:Attr", B_RAW_TYPE, 0, exact.data(), exact.size())
		== 4096);
	CHECK(continuations(path) == 0);
	CHECK(read_whole(node, "Exact:Attr") == exact);
}


static void
test_overwriting()
{
	const std::string path = new_file("overwritten");
	BNode node(path.c_str());
	const std::string big = test_value(100000);
	const std::string bigger = test_value(200000, 'b');
	const std::string small = test_value(100, 'c');

	CHECK(node.WriteAttr("Over:Attr", B_RAW_TYPE, 0, big.data(), big.size())
		== (ssize_t)big.size());
	CHECK(continuations(path) > 0);
	const int parts = continuations(path);

	CHECK(node.WriteAttr("Over:Attr", B_RAW_TYPE, 0, bigger.data(), bigger.size())
		== (ssize_t)bigger.size());
	CHECK(read_whole(node, "Over:Attr") == bigger);
	CHECK(continuations(path) > parts);

	// less than before, still split: nothing is left of the longer value
	CHECK(node.WriteAttr("Over:Attr", B_RAW_TYPE, 0, big.data(), big.size())
		== (ssize_t)big.size());
	CHECK(read_whole(node, "Over:Attr") == big);
	CHECK(continuations(path) == parts);

	// small again: not split any more
	CHECK(node.WriteAttr("Over:Attr", B_RAW_TYPE, 0, small.data(), small.size())
		== (ssize_t)small.size());
	CHECK(read_whole(node, "Over:Attr") == small);
	CHECK(continuations(path) == 0);
}


static void
test_parts_of_a_value()
{
	const std::string path = new_file("parts");
	BNode node(path.c_str());
	const std::string value = test_value(50000);
	CHECK(node.WriteAttr("Part:Attr", B_RAW_TYPE, 0, value.data(), value.size())
		== (ssize_t)value.size());

	char buffer[10000];
	// the start, the middle, across parts, the end, past the end
	CHECK(node.ReadAttr("Part:Attr", B_RAW_TYPE, 0, buffer, 10) == 10);
	CHECK(memcmp(buffer, value.data(), 10) == 0);
	CHECK(node.ReadAttr("Part:Attr", B_RAW_TYPE, 4090, buffer, 100) == 100);
	CHECK(memcmp(buffer, value.data() + 4090, 100) == 0);
	CHECK(node.ReadAttr("Part:Attr", B_RAW_TYPE, 20000, buffer, 9000) == 9000);
	CHECK(memcmp(buffer, value.data() + 20000, 9000) == 0);
	CHECK(node.ReadAttr("Part:Attr", B_RAW_TYPE, 49990, buffer, 100) == 10);
	CHECK(memcmp(buffer, value.data() + 49990, 10) == 0);
	CHECK(node.ReadAttr("Part:Attr", B_RAW_TYPE, 50000, buffer, 100) == 0);
	CHECK(node.ReadAttr("Part:Attr", B_RAW_TYPE, 90000, buffer, 100) == 0);

	// a value that isn't split, in a buffer that is too small: as on Haiku
	const std::string small = test_value(2000);
	CHECK(node.WriteAttr("Part:Small", B_RAW_TYPE, 0, small.data(), small.size())
		== 2000);
	CHECK(node.ReadAttr("Part:Small", B_RAW_TYPE, 0, buffer, 100) == 100);
	CHECK(memcmp(buffer, small.data(), 100) == 0);
	CHECK(node.ReadAttr("Part:Small", B_RAW_TYPE, 1990, buffer, 100) == 10);
	CHECK(memcmp(buffer, small.data() + 1990, 10) == 0);
	CHECK(node.ReadAttr("Part:Small", B_RAW_TYPE, 100, buffer, sizeof(buffer))
		== 1900);
	CHECK(memcmp(buffer, small.data() + 100, 1900) == 0);

	// and of no attribute at all
	CHECK(node.ReadAttr("Part:None", B_RAW_TYPE, 0, buffer, 100)
		== B_ENTRY_NOT_FOUND);
}


static void
test_listing_and_removing()
{
	const std::string path = new_file("listed");
	BNode node(path.c_str());
	const std::string big = test_value(50000);
	CHECK(node.WriteAttr("List:Big", B_RAW_TYPE, 0, big.data(), big.size())
		== (ssize_t)big.size());
	BString text("small");
	CHECK(node.WriteAttrString("List:Small", &text) == B_OK);

	// the continuations are nobody's business
	CHECK(continuations(path) > 0);
	CHECK(listed(node) == (std::set<std::string>{"List:Big", "List:Small"}));

	CHECK(node.RemoveAttr("List:Big") == B_OK);
	CHECK(continuations(path) == 0);
	CHECK(listed(node) == std::set<std::string>{"List:Small"});
	attr_info info;
	CHECK(node.GetAttrInfo("List:Big", &info) == B_ENTRY_NOT_FOUND);
	CHECK(node.RemoveAttr("List:Big") == B_ENTRY_NOT_FOUND);
}


static void
test_renaming()
{
	const std::string path = new_file("renamed");
	BNode node(path.c_str());
	const std::string big = test_value(50000);
	CHECK(node.WriteAttr("Old:Name", B_RAW_TYPE, 0, big.data(), big.size())
		== (ssize_t)big.size());
	CHECK(node.RenameAttr("Old:Name", "New:Name") == B_OK);
	CHECK(read_whole(node, "New:Name") == big);
	attr_info info;
	CHECK(node.GetAttrInfo("Old:Name", &info) == B_ENTRY_NOT_FOUND);
	CHECK(listed(node) == std::set<std::string>{"New:Name"});
}


static void
test_failing_keeps_the_old_value()
{
	// A name so long that a continuation's wouldn't fit: writing whole fails on
	// btrfs, splitting fails too, and the attribute is to stay as it was.
	const std::string path = new_file("failing");
	BNode node(path.c_str());
	const std::string name(240, 'n');
	const std::string big = test_value(50000);

	const std::string small = test_value(50, 'd');
	CHECK(node.WriteAttr(name.c_str(), B_RAW_TYPE, 0, small.data(), small.size())
		== (ssize_t)small.size());
	const ssize_t result = node.WriteAttr(name.c_str(), B_RAW_TYPE, 0, big.data(),
		big.size());
	if (result == (ssize_t)big.size()) {
		// (A file system that takes it in one piece: nothing to find out here.)
		printf("note: this file system takes 50 KB in one value\n");
		return;
	}
	CHECK(result == B_NAME_TOO_LONG);
	CHECK(read_whole(node, name.c_str()) == small);
	CHECK(continuations(path) == 0);

	// Failing halfway: the name of the tenth part doesn't fit, but those of the
	// first nine do, and they have replaced what was there.
	const std::string halfway(236, 'h');
	const std::string before = test_value(30000, 'e');
	const std::string after = test_value(60000, 'f');
	CHECK(node.WriteAttr(halfway.c_str(), B_RAW_TYPE, 0, before.data(),
		before.size()) == (ssize_t)before.size());
	const int parts = continuations(path);
	CHECK(parts > 0 && parts < 9);
	CHECK(node.WriteAttr(halfway.c_str(), B_RAW_TYPE, 0, after.data(),
		after.size()) == B_NAME_TOO_LONG);
	CHECK(read_whole(node, halfway.c_str()) == before);
	CHECK(continuations(path) == parts);

	// a name that is too long at all
	const std::string tooLong(300, 'n');
	char buffer[10];
	CHECK(node.WriteAttr(tooLong.c_str(), B_RAW_TYPE, 0, buffer, 1)
		== B_NAME_TOO_LONG);
}


static void
test_errors_say_why()
{
	const std::string path = new_file("read only");
	CHECK(chmod(path.c_str(), 0400) == 0);
	char value = 'x';
	CHECK(fs_write_attr(-1, "Err:Attr", B_RAW_TYPE, 0, &value, 1) < 0);
	CHECK(errno == B_BAD_VALUE);	// a bad descriptor is not one of the others

	// (an attribute needs write permission on the file)
	int fd = open(path.c_str(), O_RDONLY);
	CHECK(fd >= 0);
	const ssize_t denied = fs_write_attr(fd, "Err:Attr", B_RAW_TYPE, 0, &value, 1);
	if (geteuid() != 0) {
		CHECK(denied < 0);
		CHECK(errno == B_PERMISSION_DENIED);
	}
	close(fd);
	chmod(path.c_str(), 0600);
}


static void
test_query_of_a_large_value()
{
	const std::string path = new_file("queried");
	BNode node(path.c_str());
	BString text("0ABC,0DEF,");
	while (text.Length() < 30000)
		text << "0XYZ,";
	CHECK(node.WriteAttrString("Query:Long", &text) == B_OK);

	// As on BFS, a query sees the start of a value.
	BVolume volume(sDevice);
	BQuery query;
	query.SetVolume(&volume);
	query.SetFlags(B_QUERY_NON_INDEXED);
	query.SetPredicate("Query:Long==\"0ABC*\"");
	CHECK(query.Fetch() == B_OK);
	entry_ref ref;
	CHECK(query.GetNextRef(&ref) == B_OK);
	CHECK(strcmp(BPath(&ref).Leaf(), "queried") == 0);
	CHECK(query.GetNextRef(&ref) == B_ENTRY_NOT_FOUND);
}


static void
test_indices()
{
	index_info info;
	memset(&info, 0xff, sizeof(info));
	CHECK(fs_stat_index(sDevice, "SEN:ID", &info) == 0);
	CHECK(info.type == B_STRING_TYPE);
	CHECK(info.uid == getuid());
	CHECK(fs_stat_index(sDevice, "any attribute at all", &info) == 0);

	// made already, so far as anyone can tell, which callers don't mind
	errno = 0;
	CHECK(fs_create_index(sDevice, "SEN:ID", B_STRING_TYPE, 0) == -1);
	CHECK(errno == B_FILE_EXISTS);

	// of no volume, there is none
	errno = 0;
	CHECK(fs_stat_index((dev_t)-5, "SEN:ID", &info) == -1);
	CHECK(errno == B_ENTRY_NOT_FOUND);
	errno = 0;
	CHECK(fs_create_index((dev_t)-5, "SEN:ID", B_STRING_TYPE, 0) == -1);
	CHECK(errno == B_NOT_SUPPORTED);
}


class Target : public BLooper {
public:
	Target()
		:
		BLooper("volume watcher")
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_NODE_MONITOR) {
			std::lock_guard<std::mutex> lock(fLock);
			fOthers++;
			return;
		}
		// A handler that answers everything, as sen_server does, is not to get
		// its own answers: on Haiku these messages come from the kernel.
		BMessage reply((uint32)0);
		message->SendReply(&reply);
		std::lock_guard<std::mutex> lock(fLock);
		fNoReturnAddress = fNoReturnAddress
			&& !message->ReturnAddress().IsValid();
		const int32 opcode = message->GetInt32("opcode", 0);
		std::string name = message->GetString("name", "");
		if (name.empty())
			name = message->GetString("to name", "");
		if (name.rfind('/') != std::string::npos)
			name = name.substr(name.rfind('/') + 1);
		fEvents.push_back((opcode == B_ENTRY_CREATED ? "+"
			: opcode == B_ENTRY_REMOVED ? "-"
			: opcode == B_ENTRY_MOVED ? ">" : "?") + name);
	}

	int CountOthers()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fOthers;
	}

	bool HadNoReturnAddress()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fNoReturnAddress;
	}

	bool Saw(const std::string& event, int seconds = 5)
	{
		for (int i = 0; i < seconds * 20; i++) {
			{
				std::lock_guard<std::mutex> lock(fLock);
				if (std::find(fEvents.begin(), fEvents.end(), event)
						!= fEvents.end()) {
					return true;
				}
			}
			usleep(50000);
		}
		return false;
	}

private:
	std::mutex	fLock;
	std::vector<std::string> fEvents;
	int			fOthers = 0;
	bool		fNoReturnAddress = true;
};


static void
test_watch_volume()
{
	create_directory((sHome + "/watched/deep/deeper").c_str(), 0755);

	Target* target = new Target;
	target->Run();
	// As in a real application, where a message without a reply address would
	// get that of the application: the target itself, here.
	be_app_messenger = BMessenger(target);

	// not the flags of a volume
	CHECK(watch_volume(sDevice, B_WATCH_DIRECTORY, BMessenger(target))
		== B_BAD_VALUE);
	// no volume, nothing to watch
	CHECK(watch_volume((dev_t)-5, B_WATCH_NAME, BMessenger(target)) != B_OK);

	CHECK(watch_volume(sDevice, B_WATCH_NAME, target) == B_OK);

	// anywhere that is near the start
	{ BFile file((sHome + "/at home").c_str(), B_READ_WRITE | B_CREATE_FILE); }
	CHECK(target->Saw("+at home"));
	{ BFile file((sHome + "/watched/deep/deeper/down").c_str(),
		B_READ_WRITE | B_CREATE_FILE); }
	CHECK(target->Saw("+down"));
	CHECK(unlink((sHome + "/watched/deep/deeper/down").c_str()) == 0);
	CHECK(target->Saw("-down"));
	CHECK(rename((sHome + "/at home").c_str(), (sHome + "/moved").c_str()) == 0);
	CHECK(target->Saw(">moved"));

	// nobody answers these messages
	CHECK(target->HadNoReturnAddress());
	CHECK(target->CountOthers() == 0);

	// a volume with nothing to watch is not an error
	CHECK(watch_volume(sDevice, B_WATCH_NAME, target) == B_OK);
	struct stat root;
	if (stat("/", &root) == 0 && root.st_dev != sDevice)
		CHECK(watch_volume(root.st_dev, B_WATCH_NAME, target) == B_OK);

	// and it ends
	CHECK(stop_watching(BMessenger(target)) == B_OK);
	usleep(200000);
	{ BFile file((sHome + "/after").c_str(), B_READ_WRITE | B_CREATE_FILE); }
	CHECK(!target->Saw("+after", 1));

	target->PostMessage(B_QUIT_REQUESTED);
}


int
main()
{
	char folder[] = "large-attribute-test-XXXXXX";
	if (mkdtemp(folder) == NULL) {
		perror("mkdtemp");
		return 2;
	}
	sHome = std::filesystem::absolute(folder).string();
	setenv("HOME", sHome.c_str(), 1);
	unsetenv("XDG_CONFIG_HOME");
	unsetenv("XDG_CACHE_HOME");
	struct stat status;
	stat(sHome.c_str(), &status);
	sDevice = status.st_dev;

	test_sizes();
	test_what_stays_one_value();
	test_overwriting();
	test_parts_of_a_value();
	test_listing_and_removing();
	test_renaming();
	test_failing_keeps_the_old_value();
	test_errors_say_why();
	test_query_of_a_large_value();
	test_indices();
	test_watch_volume();

	std::error_code ignored;
	std::filesystem::remove_all(sHome, ignored);

	printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
