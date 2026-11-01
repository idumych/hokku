/*
 * Copyright 2026 the Hokku authors. MIT licence.
 *
 * Areas shared between processes, as on Haiku: another team finds an area
 * by name, clones it and sees the same memory; a team's areas go when it
 * exits, and when it crashes.
 *
 * The other teams are this program run again (fork + exec), so they start
 * with nothing inherited.
 */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <OS.h>


static int sFailures = 0;

#define CHECK(cond, ...) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: ", __FILE__, __LINE__); \
		printf(__VA_ARGS__); \
		printf("\n"); \
		sFailures++; \
	} \
} while (0)


static const char* sSelf;


// Runs this program as another team with the given role; returns its exit
// status, or -signal if it was killed.
static int
run_team(const char* role, const char* argument)
{
	pid_t pid = fork();
	if (pid == 0) {
		execl(sSelf, sSelf, role, argument, (char*)NULL);
		_exit(127);
	}
	int status = 0;
	waitpid(pid, &status, 0);
	if (WIFSIGNALED(status))
		return -WTERMSIG(status);
	return WEXITSTATUS(status);
}


static char sName[B_OS_NAME_LENGTH];


// Another team: find the area, clone it, check the parent's text, answer.
static int
reader(const char* name)
{
	area_id source = find_area(name);
	if (source < 0) {
		printf("reader: find_area(%s): %s\n", name, strerror(source));
		return 2;
	}
	area_info info;
	if (get_area_info(source, &info) != B_OK || info.team == getpid()
		|| info.size != 3 * B_PAGE_SIZE) {
		printf("reader: info team %d size %zu\n", (int)info.team, info.size);
		return 3;
	}
	char* address = NULL;
	area_id clone = clone_area("reader clone", (void**)&address, B_ANY_ADDRESS,
		B_READ_AREA | B_WRITE_AREA, source);
	if (clone < 0 || clone == source) {
		printf("reader: clone_area: %s\n", strerror(clone));
		return 4;
	}
	if (strcmp(address, "written by the creator") != 0) {
		printf("reader: found \"%.30s\"\n", address);
		return 5;
	}
	// The last page too: the whole area is shared.
	if (address[3 * B_PAGE_SIZE - 1] != 'z')
		return 6;
	strcpy(address + 100, "answered by the reader");
	delete_area(clone);
	return 0;
}


// Another team that creates an area and exits (or crashes) without
// deleting it.
static int
creator(const char* how)
{
	char* address;
	area_id area = create_area(sName, (void**)&address, B_ANY_ADDRESS,
		B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	if (area < 0)
		return 2;
	strcpy(address, "left behind");
	if (strcmp(how, "crash") == 0)
		kill(getpid(), SIGKILL);
	return 0;
}


int
main(int argc, char** argv)
{
	sSelf = argv[0];
	// The creator teams name their area after us, their parent.
	snprintf(sName, sizeof(sName), "area test %d", (int)getpid());

	if (argc == 3 && strcmp(argv[1], "reader") == 0)
		return reader(argv[2]);
	if (argc == 3 && strcmp(argv[1], "creator") == 0) {
		snprintf(sName, sizeof(sName), "area test %d", (int)getppid());
		return creator(argv[2]);
	}

	// Shared with another team
	char name[B_OS_NAME_LENGTH];
	snprintf(name, sizeof(name), "shared %d", (int)getpid());
	char* address = NULL;
	area_id area = create_area(name, (void**)&address, B_ANY_ADDRESS,
		3 * B_PAGE_SIZE, B_NO_LOCK, B_READ_AREA | B_WRITE_AREA);
	CHECK(area > 0, "create_area: %s", strerror(area));
	strcpy(address, "written by the creator");
	address[3 * B_PAGE_SIZE - 1] = 'z';
	CHECK(find_area(name) == area, "find_area in the same team");
	CHECK(area_for(address + 5000) == area, "area_for");

	int status = run_team("reader", name);
	CHECK(status == 0, "reader team failed with %d", status);
	CHECK(strcmp(address + 100, "answered by the reader") == 0,
		"the reader's answer isn't in our memory: \"%.30s\"", address + 100);

	// Clones in the same team share memory too
	char* clone = NULL;
	area_id cloned = clone_area("local clone", (void**)&clone, B_ANY_ADDRESS,
		B_READ_AREA, area);
	CHECK(cloned > 0 && cloned != area && clone != address
		&& strcmp(clone, "written by the creator") == 0, "local clone");
	address[0] = 'W';
	CHECK(clone[0] == 'W', "local clone sees writes");
	CHECK(delete_area(cloned) == B_OK, "delete the clone");
	CHECK(address[0] == 'W', "the original survives its clone");

	// get_next_area_info lists this team's areas
	ssize_t cookie = 0;
	area_info info;
	bool listed = false;
	while (get_next_area_info(B_CURRENT_TEAM, &cookie, &info) == B_OK)
		listed = listed || info.area == area;
	CHECK(listed, "get_next_area_info doesn't list our area");

	// Protection is real: a read-only area can't be written
	CHECK(set_area_protection(area, B_READ_AREA) == B_OK, "protect");
	pid_t pid = fork();
	if (pid == 0) {
		address[0] = 'x';
		_exit(0);
	}
	int wstatus;
	waitpid(pid, &wstatus, 0);
	CHECK(WIFSIGNALED(wstatus) && WTERMSIG(wstatus) == SIGSEGV,
		"writing a read-only area didn't fault");
	CHECK(set_area_protection(area, B_READ_AREA | B_WRITE_AREA) == B_OK,
		"unprotect");

	// Resizing keeps the contents
	CHECK(resize_area(area, 8 * B_PAGE_SIZE) == B_OK, "resize");
	CHECK(get_area_info(area, &info) == B_OK && info.size == 8 * B_PAGE_SIZE,
		"size after resize");
	address = (char*)info.address;
	CHECK(address[1] == 'r' && address[3 * B_PAGE_SIZE - 1] == 'z',
		"contents after resize");
	address[8 * B_PAGE_SIZE - 1] = 'y';

	CHECK(delete_area(area) == B_OK, "delete");
	CHECK(find_area(name) == B_NAME_NOT_FOUND, "deleted area still found");
	CHECK(run_team("reader", name) == 2, "another team still finds it");

	// A team's areas go when it exits, and when it's killed
	CHECK(run_team("creator", "exit") == 0, "creator");
	CHECK(find_area(sName) == B_NAME_NOT_FOUND,
		"area of a team that exited is still there");
	CHECK(run_team("creator", "crash") == -SIGKILL, "crashing creator");
	CHECK(find_area(sName) == B_NAME_NOT_FOUND,
		"area of a team that crashed is still there");

	if (sFailures == 0)
		printf("area_shared_test: all passed\n");
	return sFailures == 0 ? 0 : 1;
}
