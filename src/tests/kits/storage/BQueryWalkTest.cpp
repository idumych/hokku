/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 */


/*!	BQuery on a Linux file system: what the predicate means, what a query walks
	and what it leaves out. Run it from a folder on a real disk (the home folder
	of the test is made there; a tmpfs is not the volume of a home folder):
		BQueryWalkTest
*/


#include <Directory.h>
#include <AppDefs.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <fs_info.h>
#include <Looper.h>
#include <Message.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <Node.h>
#include <Path.h>
#include <Query.h>
#include <TypeConstants.h>
#include <Volume.h>
#include <VolumeRoster.h>

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


typedef std::set<std::string> Names;


static void
make_file(const std::string& relative)
{
	const std::string path = sHome + "/" + relative;
	create_directory(path.substr(0, path.rfind('/')).c_str(), 0755);
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE);
}


static void
set_string(const std::string& relative, const char* attribute,
	const char* value)
{
	BNode node((sHome + "/" + relative).c_str());
	BString text(value);
	CHECK(node.WriteAttrString(attribute, &text) == B_OK);
}


static void
set_int32(const std::string& relative, const char* attribute, int32 value)
{
	BNode node((sHome + "/" + relative).c_str());
	CHECK(node.WriteAttr(attribute, B_INT32_TYPE, 0, &value, sizeof(value))
		== sizeof(value));
}


/*!	The names of what the query finds, relative to the home folder. */
static Names
run_query(const char* predicate, status_t* _status = NULL)
{
	BVolume volume(sDevice);
	BQuery query;
	Names names;
	query.SetVolume(&volume);
	query.SetPredicate(predicate);
	const status_t status = query.Fetch();
	if (_status != NULL)
		*_status = status;
	if (status != B_OK)
		return names;

	entry_ref ref;
	while (query.GetNextRef(&ref) == B_OK) {
		std::string path = BPath(&ref).Path();
		if (path.rfind(sHome + "/", 0) == 0)
			path = path.substr(sHome.size() + 1);
		names.insert(path);
	}
	return names;
}


static void
check_query(const char* predicate, const Names& expected)
{
	status_t status;
	const Names found = run_query(predicate, &status);
	sChecks++;
	if (status == B_OK && found == expected)
		return;

	sFailures++;
	printf("FAIL %s: status %s, found:", predicate, strerror(status));
	for (const std::string& name : found)
		printf(" '%s'", name.c_str());
	printf("; expected:");
	for (const std::string& name : expected)
		printf(" '%s'", name.c_str());
	printf("\n");
}


static void
make_tree()
{
	make_file("a");
	set_string("a", "SEN:ID", "0ID1");
	set_string("a", "SEN:TO", "0ID2,0ID3");
	set_string("a", "Audio:Artist", "Ann Author");
	set_int32("a", "Media:Rating", 5);

	make_file("sub/deep/b");
	set_string("sub/deep/b", "SEN:ID", "0ID2");
	set_string("sub/deep/b", "Audio:Artist", "Ben Writer");
	set_int32("sub/deep/b", "Media:Rating", 8);

	make_file("no attributes");
	CHECK(create_directory((sHome + "/a folder").c_str(), 0755) == B_OK);
	set_string("a folder", "SEN:ID", "0ID3");
}


static void
test_equations()
{
	check_query("SEN:ID==\"0ID1\"", {"a"});
	check_query("SEN:ID==\"0ID\"", {});
	check_query("SEN:ID!=\"0ID1\"", {"sub/deep/b", "a folder"});
	check_query("SEN:ID==\"0ID3\"", {"a folder"});
	// the form that SEN finds inbound relations with
	check_query("SEN:TO==\"*0ID3*\"", {"a"});
	check_query("Audio:Artist==\"Ann*\"", {"a"});
	check_query("Audio:Artist==\"?en Writer\"", {"sub/deep/b"});
	check_query("Audio:Artist==\"[aA]nn Author\"", {"a"});
	// compared as the numbers that they are: 8 < 10, which is not so for text
	check_query("Media:Rating==5", {"a"});
	check_query("Media:Rating>5", {"sub/deep/b"});
	check_query("Media:Rating<10", {"a", "sub/deep/b"});
	check_query("Media:Rating>=8", {"sub/deep/b"});
	check_query("Media:Rating<=5", {"a"});
}


static void
test_operators()
{
	check_query("(SEN:ID==\"0ID1\")&&(Media:Rating==5)", {"a"});
	check_query("(SEN:ID==\"0ID1\")&&(Media:Rating==8)", {});
	check_query("(SEN:ID==\"0ID1\")||(SEN:ID==\"0ID2\")", {"a", "sub/deep/b"});
	check_query("(SEN:ID==\"0ID*\")&&(!(Media:Rating==5))", {"sub/deep/b"});
}


static void
test_built_in_attributes()
{
	make_file("sized");
	BFile file((sHome + "/sized").c_str(), B_WRITE_ONLY);
	file.Write("12345", 5);
	file.Sync();

	check_query("name==\"no attributes\"", {"no attributes"});
	check_query("name==\"no*\"", {"no attributes"});
	check_query("(size==5)&&(name==\"sized\")", {"sized"});
	check_query("(size>4)&&(name==\"sized\")", {"sized"});
	check_query("(last_modified>1000)&&(name==\"sized\")", {"sized"});
	check_query("(last_modified<1000)&&(name==\"sized\")", {});
	// a date, as %...% in a predicate has one on Haiku
	check_query("(last_modified>%Jan 1, 1999%)&&(name==\"sized\")", {"sized"});
	check_query("(last_modified<%tomorrow%)&&(name==\"sized\")", {"sized"});
	check_query("(last_modified>%tomorrow%)&&(name==\"sized\")", {});

	std::filesystem::remove(sHome + "/sized");
}


static void
test_long_value()
{
	// As on BFS, only the start of a long value can be found.
	const std::string start(250, 'x');
	const std::string value = start + std::string(5000, 'y') + "END";
	make_file("long");
	set_string("long", "Test:Long", value.c_str());

	check_query((std::string("Test:Long==\"") + start + "*\"").c_str(),
		{"long"});
	check_query("Test:Long==\"*END\"", {});
}


static void
test_foreign_attribute()
{
	// An attribute that Hokku did not write has no recorded type.
	make_file("foreign");
	const char* value = "from setfattr";
	CHECK(setxattr((sHome + "/foreign").c_str(), "user.Test:Foreign", value,
		strlen(value), 0) == 0);
	check_query("Test:Foreign==\"from setfattr\"", {"foreign"});
}


static void
test_what_is_left_out()
{
	const std::string outside = sHome + ".outside";
	create_directory(outside.c_str(), 0755);
	BFile target((outside + "/target").c_str(), B_READ_WRITE | B_CREATE_FILE);
	BString id("0HIDDEN");
	target.WriteAttrString("Test:Left", &id);

	make_file(".hidden");
	set_string(".hidden", "Test:Left", "0HIDDEN");
	make_file(".cache/deep");
	set_string(".cache/deep", "Test:Left", "0HIDDEN");
	CHECK(symlink(outside.c_str(), (sHome + "/link to folder").c_str()) == 0);
	CHECK(symlink((outside + "/target").c_str(),
		(sHome + "/link to file").c_str()) == 0);
	CHECK(mkfifo((sHome + "/a fifo").c_str(), 0400) == 0);
	make_file("closed/x");
	set_string("closed/x", "Test:Closed", "x");
	CHECK(chmod((sHome + "/closed").c_str(), 0) == 0);

	alarm(30);	// a walk that blocks fails instead of hanging
	check_query("Test:Left==\"0HIDDEN\"", {});
	check_query("Test:Closed==\"x\"", {});
	// a link is an entry of its own (found by its name), not what it points at
	check_query("name==\"link to file\"", {"link to file"});
	alarm(0);

	chmod((sHome + "/closed").c_str(), 0755);
	std::error_code ignored;
	std::filesystem::remove_all(outside, ignored);
}


static void
test_settings_folder_is_searched()
{
	BPath settings;
	CHECK(find_directory(B_USER_SETTINGS_DIRECTORY, &settings) == B_OK);
	const std::string path = std::string(settings.Path()) + "/in settings";
	create_directory(settings.Path(), 0755);
	BFile file(path.c_str(), B_READ_WRITE | B_CREATE_FILE);
	BString value("x");
	file.WriteAttrString("Test:Settings", &value);

	const Names found = run_query("Test:Settings==\"x\"");
	CHECK(found.size() == 1);
}


static void
test_api()
{
	BVolume volume(sDevice);
	CHECK(volume.KnowsAttr());
	CHECK(volume.KnowsQuery());

	// what BFS refuses is refused
	status_t status;
	for (const char* predicate : {"SEN:ID", "==1", "SEN:ID!1", "SEN:ID==\"[a\"", ""}) {
		run_query(predicate, &status);
		CHECK(status != B_OK);
	}

	// nothing can be fetched without a predicate or a volume
	{
		BQuery query;
		CHECK(query.Fetch() == B_NO_INIT);
		entry_ref ref;
		CHECK(query.GetNextRef(&ref) == B_FILE_ERROR);
		CHECK(query.Rewind() == B_FILE_ERROR);
	}

	// a predicate built on the stack is the same as one given as text
	{
		BQuery query;
		query.SetVolume(&volume);
		query.PushAttr("SEN:ID");
		query.PushString("0ID1");
		query.PushOp(B_EQ);
		CHECK(query.Fetch() == B_OK);
		entry_ref ref;
		CHECK(query.GetNextRef(&ref) == B_OK);
		CHECK(strcmp(BPath(&ref).Leaf(), "a") == 0);
		CHECK(query.GetNextRef(&ref) == B_ENTRY_NOT_FOUND);

		// once fetched it can't be changed, it can be read from the start again
		CHECK(query.PushAttr("x") == B_NOT_ALLOWED);
		CHECK(query.SetPredicate("x==1") == B_NOT_ALLOWED);
		CHECK(query.Rewind() == B_OK);
		CHECK(query.GetNextRef(&ref) == B_OK);
		CHECK(strcmp(BPath(&ref).Leaf(), "a") == 0);

		// and used again after Clear()
		CHECK(query.Clear() == B_OK);
		query.SetVolume(&volume);
		query.SetPredicate("SEN:ID==\"0ID2\"");
		CHECK(query.Fetch() == B_OK);
		CHECK(query.GetNextRef(&ref) == B_OK);
		CHECK(strcmp(BPath(&ref).Leaf(), "b") == 0);
	}

	// the ref opens the file, with its folder's node as on Haiku
	{
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("SEN:ID==\"0ID2\"");
		CHECK(query.Fetch() == B_OK);
		entry_ref ref;
		CHECK(query.GetNextRef(&ref) == B_OK);
		CHECK(ref.device == sDevice);
		struct stat parent;
		CHECK(stat((sHome + "/sub/deep").c_str(), &parent) == 0);
		CHECK(ref.directory == parent.st_ino);
		BNode node(&ref);
		CHECK(node.InitCheck() == B_OK);
	}

	// the entries come as BEntry and dirent, too
	{
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("SEN:ID==\"0ID1\"");
		CHECK(query.Fetch() == B_OK);
		BEntry entry;
		CHECK(query.GetNextEntry(&entry) == B_OK);
		char name[B_FILE_NAME_LENGTH];
		entry.GetName(name);
		CHECK(strcmp(name, "a") == 0);
		CHECK(query.GetNextEntry(&entry) == B_ENTRY_NOT_FOUND);

		char buffer[sizeof(struct dirent) + B_PATH_NAME_LENGTH];
		struct dirent* dirent = (struct dirent*)buffer;
		CHECK(query.Rewind() == B_OK);
		CHECK(query.GetNextDirents(dirent, sizeof(buffer), 1) == 1);
		CHECK(sHome + "/a" == dirent->d_name);
		CHECK(query.GetNextDirents(dirent, sizeof(buffer), 1) == 0);
	}

}


/*!	Keeps what a live query says. */
class Collector : public BLooper {
public:
	Collector()
		:
		BLooper("live query target")
	{
	}

	virtual void MessageReceived(BMessage* message)
	{
		if (message->what != B_QUERY_UPDATE) {
			BLooper::MessageReceived(message);
			return;
		}
		std::lock_guard<std::mutex> lock(fLock);
		const int32 opcode = message->GetInt32("opcode", 0);
		std::string name = message->GetString("name", "");
		if (name.rfind(sHome + "/", 0) == 0)
			name = name.substr(sHome.size() + 1);
		fUpdates.push_back(std::string(opcode == B_ENTRY_CREATED ? "+"
			: opcode == B_ENTRY_REMOVED ? "-" : "?") + name);
		fLastHadFields = message->GetInt32("device", -1) == (int32)sDevice
			&& message->GetInt64("directory", 0) != 0
			&& message->GetInt64("node", 0) != 0;
	}

	/*!	What came within the time, waiting for as many as are expected and then
		a moment longer, for those that should not come.
	*/
	std::vector<std::string> Take(size_t expected, int seconds = 5)
	{
		for (int i = 0; i < seconds * 20; i++) {
			{
				std::lock_guard<std::mutex> lock(fLock);
				if (fUpdates.size() >= expected)
					break;
			}
			usleep(50000);
		}
		usleep(200000);
		std::lock_guard<std::mutex> lock(fLock);
		std::vector<std::string> updates;
		updates.swap(fUpdates);
		return updates;
	}

	bool LastHadFields()
	{
		std::lock_guard<std::mutex> lock(fLock);
		return fLastHadFields;
	}

private:
	std::mutex	fLock;
	std::vector<std::string> fUpdates;
	bool		fLastHadFields = false;
};


static void
check_updates(Collector* collector, const char* what,
	const std::vector<std::string>& expected, int seconds = 5)
{
	const std::vector<std::string> updates
		= collector->Take(expected.size(), seconds);
	sChecks++;
	if (updates == expected)
		return;

	sFailures++;
	printf("FAIL live, %s: got", what);
	for (const std::string& update : updates)
		printf(" '%s'", update.c_str());
	printf("; expected");
	for (const std::string& update : expected)
		printf(" '%s'", update.c_str());
	printf("\n");
}


static std::vector<std::string>
read_all(BQuery& query)
{
	std::vector<std::string> names;
	entry_ref ref;
	while (query.GetNextRef(&ref) == B_OK)
		names.push_back(BPath(&ref).Leaf());
	std::sort(names.begin(), names.end());
	return names;
}


static void
test_live()
{
	Collector* collector = new Collector;
	collector->Run();

	BVolume volume(sDevice);
	BQuery query;
	query.SetVolume(&volume);
	query.SetPredicate("Live:Tag==\"yes*\"");
	CHECK(query.SetTarget(BMessenger()) == B_BAD_VALUE);
	CHECK(query.SetTarget(BMessenger(collector)) == B_OK);
	CHECK(query.IsLive());

	make_file("live/before");
	set_string("live/before", "Live:Tag", "yes");
	make_file("live/other");
	CHECK(query.Fetch() == B_OK);
	CHECK(query.SetPredicate("x==1") == B_NOT_ALLOWED);

	// what matched at the start is read, not sent
	CHECK(read_all(query) == std::vector<std::string>{"before"});
	CHECK(query.Rewind() == B_OK);
	CHECK(read_all(query) == std::vector<std::string>{"before"});
	check_updates(collector, "nothing yet", {}, 0);

	// an attribute starts to match, changes and stops
	set_string("live/other", "Live:Tag", "yes");
	check_updates(collector, "attribute set", {"+live/other"});
	CHECK(collector->LastHadFields());
	set_string("live/other", "Live:Tag", "yes, still");
	check_updates(collector, "still matches", {}, 0);
	set_string("live/other", "Live:Tag", "no");
	check_updates(collector, "attribute changed", {"-live/other"});

	// a removed attribute
	set_string("live/other", "Live:Tag", "yes");
	check_updates(collector, "again", {"+live/other"});
	BNode((sHome + "/live/other").c_str()).RemoveAttr("Live:Tag");
	check_updates(collector, "attribute removed", {"-live/other"});

	// a new file, with an attribute
	make_file("live/new");
	check_updates(collector, "no attribute yet", {}, 0);
	set_string("live/new", "Live:Tag", "yes");
	check_updates(collector, "new file", {"+live/new"});

	// a rename, to a name and to a hidden name
	CHECK(rename((sHome + "/live/new").c_str(),
		(sHome + "/live/renamed").c_str()) == 0);
	check_updates(collector, "renamed", {"-live/new", "+live/renamed"});
	CHECK(rename((sHome + "/live/renamed").c_str(),
		(sHome + "/live/.hidden now").c_str()) == 0);
	check_updates(collector, "now hidden", {"-live/renamed"});

	// a removed file
	CHECK(unlink((sHome + "/live/before").c_str()) == 0);
	check_updates(collector, "deleted", {"-live/before"});

	// a new folder, its file, and a file in a new folder made after
	CHECK(create_directory((sHome + "/live/made").c_str(), 0755) == B_OK);
	usleep(200000);
	make_file("live/made/inside");
	set_string("live/made/inside", "Live:Tag", "yes");
	check_updates(collector, "in a new folder", {"+live/made/inside"});

	// a folder that comes with what is in it
	const std::string away = sHome + ".away";
	CHECK(create_directory((away + "/deep").c_str(), 0755) == B_OK);
	{
		BFile file((away + "/deep/there").c_str(),
			B_READ_WRITE | B_CREATE_FILE);
		BString value("yes");
		file.WriteAttrString("Live:Tag", &value);
	}
	CHECK(rename(away.c_str(), (sHome + "/live/moved in").c_str()) == 0);
	check_updates(collector, "folder moved in", {"+live/moved in/deep/there"});

	// and one that goes with it, and is not listened to any more
	CHECK(rename((sHome + "/live/moved in").c_str(), away.c_str()) == 0);
	check_updates(collector, "folder moved out",
		{"-live/moved in/deep/there"});
	{
		BNode node((away + "/deep/there").c_str());
		node.RemoveAttr("Live:Tag");
		BString value("yes");
		node.WriteAttrString("Live:Tag", &value);
	}
	check_updates(collector, "out of sight", {}, 0);
	std::error_code ignored;

	// a new folder of the old name is another folder, and is listened to
	// (the moved one still exists, so that its watch would be alive if it had
	// been kept)
	CHECK(create_directory((sHome + "/live/moved in/deep").c_str(), 0755)
		== B_OK);
	usleep(300000);
	make_file("live/moved in/deep/again");
	set_string("live/moved in/deep/again", "Live:Tag", "yes");
	check_updates(collector, "same name, new folder",
		{"+live/moved in/deep/again"});
	std::filesystem::remove_all(away, ignored);

	// a folder deleted with a match in it
	std::filesystem::remove_all(sHome + "/live/made", ignored);
	check_updates(collector, "folder deleted", {"-live/made/inside"});

	// after Clear() nothing more comes
	CHECK(query.Clear() == B_OK);
	make_file("live/after");
	set_string("live/after", "Live:Tag", "yes");
	check_updates(collector, "after Clear", {}, 0);

	collector->PostMessage(B_QUIT_REQUESTED);
}


/*!	A messenger's target can be a looper's preferred handler, whichever it is. */
static void
test_live_preferred_handler()
{
	Collector* collector = new Collector;
	collector->Run();
	{
		BVolume volume(sDevice);
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("Preferred:Tag==\"yes\"");
		CHECK(query.SetTarget(BMessenger(NULL, collector)) == B_OK);
		CHECK(query.Fetch() == B_OK);
		CHECK(read_all(query).empty());
		make_file("preferred");
		set_string("preferred", "Preferred:Tag", "yes");
		check_updates(collector, "preferred handler", {"+preferred"});
	}
	collector->PostMessage(B_QUIT_REQUESTED);
}


/*!	Without watches, or none left for a folder, a walk finds the change. */
static void
test_live_found_by_walking()
{
	setenv("HOKKU_QUERY_WATCH_LIMIT", "1", 1);
	setenv("HOKKU_QUERY_WALK_INTERVAL", "1", 1);
	make_file("walked/one/deep");
	make_file("walked/two/deep");
	set_string("walked/one/deep", "Walked:Tag", "yes");

	Collector* collector = new Collector;
	collector->Run();
	{
		BVolume volume(sDevice);
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("Walked:Tag==\"yes\"");
		CHECK(query.SetTarget(BMessenger(collector)) == B_OK);
		CHECK(query.Fetch() == B_OK);
		CHECK(read_all(query) == std::vector<std::string>{"deep"});

		// the one watch is on the settings folder, nowhere near this
		set_string("walked/two/deep", "Walked:Tag", "yes");
		check_updates(collector, "walked: started", {"+walked/two/deep"}, 10);
		BNode((sHome + "/walked/one/deep").c_str()).RemoveAttr("Walked:Tag");
		check_updates(collector, "walked: stopped", {"-walked/one/deep"}, 10);
		CHECK(unlink((sHome + "/walked/two/deep").c_str()) == 0);
		check_updates(collector, "walked: deleted", {"-walked/two/deep"}, 10);
	}
	collector->PostMessage(B_QUIT_REQUESTED);
	unsetenv("HOKKU_QUERY_WATCH_LIMIT");
	unsetenv("HOKKU_QUERY_WALK_INTERVAL");
}


/*!	A query can be dropped at any moment of its walk. */
static void
test_live_closed_at_once()
{
	Collector* collector = new Collector;
	collector->Run();
	for (int i = 0; i < 50; i++) {
		BVolume volume(sDevice);
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("SEN:ID==\"*\"");
		CHECK(query.SetTarget(BMessenger(collector)) == B_OK);
		CHECK(query.Fetch() == B_OK);
	}
	collector->PostMessage(B_QUIT_REQUESTED);
}


static void
test_a_volume_is_not_walked_twice()
{
	// every volume is asked, as a program that looks everywhere does, and the
	// home folder's files come from its volume only
	BVolumeRoster roster;
	BVolume volume;
	int found = 0;
	while (roster.GetNextVolume(&volume) == B_OK) {
		if (!volume.KnowsQuery())
			continue;
		BQuery query;
		query.SetVolume(&volume);
		query.SetPredicate("SEN:ID==\"0ID1\"");
		if (query.Fetch() != B_OK)
			continue;
		entry_ref ref;
		while (query.GetNextRef(&ref) == B_OK) {
			if (strcmp(BPath(&ref).Path(), (sHome + "/a").c_str()) == 0)
				found++;
		}
	}
	CHECK(found == 1);
}


int
main()
{
	char pattern[] = "bquery-test-XXXXXX";
	if (mkdtemp(pattern) == NULL) {
		perror("mkdtemp");
		return 2;
	}
	sHome = std::filesystem::absolute(pattern).string();
	setenv("HOME", sHome.c_str(), 1);
	unsetenv("XDG_CONFIG_HOME");
	struct stat status;
	stat(sHome.c_str(), &status);
	sDevice = status.st_dev;

	make_tree();
	test_equations();
	test_operators();
	test_built_in_attributes();
	test_long_value();
	test_foreign_attribute();
	test_settings_folder_is_searched();
	test_api();
	test_what_is_left_out();
	test_live();
	test_live_preferred_handler();
	test_live_found_by_walking();
	test_live_closed_at_once();
	test_a_volume_is_not_walked_twice();

	std::error_code ignored;
	std::filesystem::remove_all(sHome, ignored);

	printf("%d checks, %d failures\n", sChecks, sFailures);
	return sFailures == 0 ? 0 : 1;
}
