/*
 * Copyright 2026 the Hokku authors. MIT licence.
 *
 * Areas on Linux, shared between processes as on Haiku.
 *
 * Every area is backed by a POSIX shared memory object mapped MAP_SHARED,
 * so clone_area() in another team maps the same pages. A registry in its own
 * shared memory object ("/hokku-areas-<uid>") lists every team's areas, so
 * area IDs are unique across processes and find_area(), get_area_info() and
 * get_next_area_info() see all of them, as on Haiku.
 *
 * Differences from Haiku, all because user space can't do better:
 *   - Areas of a team that exits are freed by its exit handler, or, if it
 *     crashed, by the next team that creates or looks up an area.
 *   - resize_area() grows the backing object for every team, but only the
 *     caller's mapping changes size; clones elsewhere keep their old size.
 *   - _kern_transfer_area() changes the owner and unmaps the area here; the
 *     new owner maps it when it first looks it up (get_area_info()).
 *   - addr_spec is ignored: the kernel picks the address.
 *   - One user's processes share one registry; other users can't clone.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE	/* mremap */
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "../../config.h"

#include <SupportDefs.h>
#include <StorageDefs.h>	/* Just because BeOS apps expect this here */
#include <OS.h>


#define TRACE_AREA 0
#if TRACE_AREA
#	define TRACE(x) printf x
#else
#	define TRACE(x) ;
#endif

#define kRegistryMagic		0x48414552	/* HAER */
#define kRegistryVersion	1
#define kMaxAreas			4096
#define kMaxLocalAreas		1024


/* One area in the registry. "backing" is the ID of the area that created
 * the shared memory object; clones point at it, and the object is unlinked
 * when the last area using it goes. */
typedef struct registry_area {
	area_id		id;				/* 0 = free slot */
	area_id		backing;
	team_id		team;
	char		name[B_OS_NAME_LENGTH];
	size_t		size;
	uint32		lock;
	uint32		protection;
	void*		address;		/* in the owning team */
} registry_area;

typedef struct area_registry {
	uint32			magic;		/* set last, once the lock is ready */
	uint32			version;
	pthread_mutex_t	lock;
	area_id			next_id;
	registry_area	areas[kMaxAreas];
} area_registry;

/* This team's mappings */
typedef struct local_area {
	area_id		id;
	void*		address;
	size_t		size;
} local_area;


static area_registry* sRegistry = NULL;
static pthread_once_t sRegistryOnce = PTHREAD_ONCE_INIT;
static local_area sLocal[kMaxLocalAreas];
static pthread_mutex_t sLocalLock = PTHREAD_MUTEX_INITIALIZER;


/*	#pragma mark - Registry */


static void
registry_name(char* buffer, size_t size)
{
	snprintf(buffer, size, "/hokku-areas-%u", (unsigned)getuid());
}


static void
backing_name(area_id backing, char* buffer, size_t size)
{
	snprintf(buffer, size, "/hokku-area-%u-%d", (unsigned)getuid(),
		(int)backing);
}


static void delete_team_areas(void);
static void reap_dead_teams(void);


static void
open_registry(void)
{
	char name[64];
	registry_name(name, sizeof(name));

	bool created = false;
	int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
	if (fd >= 0) {
		created = true;
		if (ftruncate(fd, sizeof(area_registry)) != 0) {
			fprintf(stderr, "areas: can't size %s: %s\n", name,
				strerror(errno));
			close(fd);
			shm_unlink(name);
			return;
		}
	} else if (errno == EEXIST)
		fd = shm_open(name, O_RDWR, 0600);
	if (fd < 0) {
		fprintf(stderr, "areas: can't open %s: %s\n", name, strerror(errno));
		return;
	}

	/* Another team may have created it but not sized it yet. */
	struct stat st;
	for (int i = 0; i < 1000; i++) {
		if (fstat(fd, &st) == 0 && st.st_size >= (off_t)sizeof(area_registry))
			break;
		usleep(1000);
	}

	area_registry* registry = (area_registry*)mmap(NULL,
		sizeof(area_registry), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (registry == MAP_FAILED) {
		fprintf(stderr, "areas: can't map %s: %s\n", name, strerror(errno));
		return;
	}

	if (created) {
		pthread_mutexattr_t attributes;
		pthread_mutexattr_init(&attributes);
		pthread_mutexattr_setpshared(&attributes, PTHREAD_PROCESS_SHARED);
		pthread_mutexattr_setrobust(&attributes, PTHREAD_MUTEX_ROBUST);
		pthread_mutex_init(&registry->lock, &attributes);
		pthread_mutexattr_destroy(&attributes);
		registry->version = kRegistryVersion;
		registry->next_id = 1;
		__atomic_store_n(&registry->magic, kRegistryMagic, __ATOMIC_RELEASE);
	} else {
		for (int i = 0; i < 1000; i++) {
			if (__atomic_load_n(&registry->magic, __ATOMIC_ACQUIRE)
					== kRegistryMagic)
				break;
			usleep(1000);
		}
		if (registry->magic != kRegistryMagic
			|| registry->version != kRegistryVersion) {
			fprintf(stderr, "areas: %s is not a version %d registry\n", name,
				kRegistryVersion);
			munmap(registry, sizeof(area_registry));
			return;
		}
	}

	sRegistry = registry;
	atexit(delete_team_areas);

	/* Clean up after teams that crashed since anyone last looked. */
	if (pthread_mutex_lock(&sRegistry->lock) == EOWNERDEAD)
		pthread_mutex_consistent(&sRegistry->lock);
	reap_dead_teams();
	pthread_mutex_unlock(&sRegistry->lock);
}


static bool
lock_registry(void)
{
	pthread_once(&sRegistryOnce, open_registry);
	if (sRegistry == NULL)
		return false;
	int status = pthread_mutex_lock(&sRegistry->lock);
	if (status == EOWNERDEAD) {
		/* A team died holding the lock. Entries are written whole before
		 * the lock is released, so the table is still usable. */
		pthread_mutex_consistent(&sRegistry->lock);
		status = 0;
	}
	return status == 0;
}


static void
unlock_registry(void)
{
	pthread_mutex_unlock(&sRegistry->lock);
}


static registry_area*
find_entry(area_id id)
{
	if (id <= 0)
		return NULL;
	for (int i = 0; i < kMaxAreas; i++) {
		if (sRegistry->areas[i].id == id)
			return &sRegistry->areas[i];
	}
	return NULL;
}


static bool
team_alive(team_id team)
{
	return team == getpid() || kill(team, 0) == 0 || errno != ESRCH;
}


/* Frees a registry slot, and the shared memory object once no area uses it.
 * Registry locked. */
static void
release_entry(registry_area* entry)
{
	area_id backing = entry->backing;
	memset(entry, 0, sizeof(*entry));
	for (int i = 0; i < kMaxAreas; i++) {
		if (sRegistry->areas[i].id != 0
			&& sRegistry->areas[i].backing == backing)
			return;
	}
	char name[64];
	backing_name(backing, name, sizeof(name));
	shm_unlink(name);
}


/* Frees the areas of teams that died without cleaning up. Registry locked. */
static void
reap_dead_teams(void)
{
	for (int i = 0; i < kMaxAreas; i++) {
		registry_area* entry = &sRegistry->areas[i];
		if (entry->id != 0 && !team_alive(entry->team)) {
			TRACE(("areas: reaping %d \"%s\" of dead team %d\n", entry->id,
				entry->name, entry->team));
			release_entry(entry);
		}
	}
}


static registry_area*
free_entry(void)
{
	for (int i = 0; i < kMaxAreas; i++) {
		if (sRegistry->areas[i].id == 0)
			return &sRegistry->areas[i];
	}
	reap_dead_teams();
	for (int i = 0; i < kMaxAreas; i++) {
		if (sRegistry->areas[i].id == 0)
			return &sRegistry->areas[i];
	}
	return NULL;
}


/*	#pragma mark - This team's mappings */


static int
mmap_protection(uint32 protection)
{
	int result = 0;
	if ((protection & B_READ_AREA) != 0)
		result |= PROT_READ;
	if ((protection & B_WRITE_AREA) != 0)
		result |= PROT_READ | PROT_WRITE;
	if ((protection & B_EXECUTE_AREA) != 0)
		result |= PROT_READ | PROT_EXEC;
	return result;
}


static size_t
page_round(size_t size)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	return (size + page - 1) & ~(page - 1);
}


/* Maps the backing object of an area into this team. */
static void*
map_backing(area_id backing, size_t size, uint32 protection, bool create)
{
	char name[64];
	backing_name(backing, name, sizeof(name));
	int fd = shm_open(name, create ? (O_RDWR | O_CREAT | O_EXCL) : O_RDWR,
		0600);
	if (fd < 0)
		return NULL;
	if (create && ftruncate(fd, page_round(size)) != 0) {
		close(fd);
		shm_unlink(name);
		return NULL;
	}
	void* address = mmap(NULL, page_round(size), mmap_protection(protection),
		MAP_SHARED, fd, 0);
	close(fd);
	if (address == MAP_FAILED) {
		if (create)
			shm_unlink(name);
		return NULL;
	}
	return address;
}


static local_area*
find_local(area_id id)
{
	for (int i = 0; i < kMaxLocalAreas; i++) {
		if (sLocal[i].id == id)
			return &sLocal[i];
	}
	return NULL;
}


static bool
add_local(area_id id, void* address, size_t size)
{
	pthread_mutex_lock(&sLocalLock);
	local_area* local = find_local(0);
	if (local != NULL) {
		local->id = id;
		local->address = address;
		local->size = size;
	}
	pthread_mutex_unlock(&sLocalLock);
	return local != NULL;
}


static void
fill_info(registry_area* entry, area_info* info)
{
	memset(info, 0, sizeof(*info));
	info->area = entry->id;
	snprintf(info->name, sizeof(info->name), "%s", entry->name);
	info->size = entry->size;
	info->lock = entry->lock;
	info->protection = entry->protection;
	info->team = entry->team;
	info->ram_size = entry->size;
	info->address = entry->address;

	/* This team's own address for an area it has mapped */
	pthread_mutex_lock(&sLocalLock);
	local_area* local = find_local(entry->id);
	if (local != NULL)
		info->address = local->address;
	pthread_mutex_unlock(&sLocalLock);

	/* An area another team handed this one (_kern_transfer_area()): mapped
	 * here now */
	if (local == NULL && entry->team == getpid()) {
		void* address = map_backing(entry->backing, entry->size,
			entry->protection, false);
		if (address != NULL) {
			if (add_local(entry->id, address, entry->size)) {
				entry->address = address;
				info->address = address;
			} else
				munmap(address, page_round(entry->size));
		}
	}
}


/* Adds an area: maps the backing (creating it for a new area) and registers
 * it. Returns its ID or an error. */
static area_id
add_area(const char* name, void** _address, area_id backing, size_t size,
	uint32 lock, uint32 protection)
{
	if (size == 0)
		return B_BAD_VALUE;
	if (!lock_registry())
		return B_NO_MEMORY;

	registry_area* entry = free_entry();
	if (entry == NULL) {
		unlock_registry();
		return B_NO_MEMORY;
	}
	area_id id = sRegistry->next_id++;
	if (sRegistry->next_id <= 0)
		sRegistry->next_id = 1;
	bool isNew = backing < 0;
	if (isNew)
		backing = id;

	void* address = map_backing(backing, size, protection, isNew);
	if (address == NULL) {
		status_t error = errno == ENOENT ? B_BAD_VALUE : B_NO_MEMORY;
		unlock_registry();
		return error;
	}
	if (!add_local(id, address, size)) {
		munmap(address, page_round(size));
		if (isNew) {
			char objectName[64];
			backing_name(backing, objectName, sizeof(objectName));
			shm_unlink(objectName);
		}
		unlock_registry();
		return B_NO_MEMORY;
	}

	entry->id = id;
	entry->backing = backing;
	entry->team = getpid();
	snprintf(entry->name, sizeof(entry->name), "%s", name != NULL ? name : "");
	entry->size = size;
	entry->lock = lock;
	entry->protection = protection;
	entry->address = address;
	unlock_registry();

	if (_address != NULL)
		*_address = address;
	TRACE(("areas: %d \"%s\" (backing %d) at %p, %zu bytes\n", id, entry->name,
		backing, address, size));
	return id;
}


/* Runs at exit: a team's areas die with it, as on Haiku. */
static void
delete_team_areas(void)
{
	if (sRegistry == NULL || !lock_registry())
		return;
	team_id team = getpid();
	for (int i = 0; i < kMaxAreas; i++) {
		if (sRegistry->areas[i].id != 0 && sRegistry->areas[i].team == team)
			release_entry(&sRegistry->areas[i]);
	}
	reap_dead_teams();
	unlock_registry();
}


/*	#pragma mark - Public API */


area_id
create_area(const char* name, void** startAddress, uint32 addressSpec,
	size_t size, uint32 lock, uint32 protection)
{
	(void)addressSpec;
	return add_area(name, startAddress, -1, size, lock, protection);
}


area_id
clone_area(const char* name, void** destAddress, uint32 addressSpec,
	uint32 protection, area_id source)
{
	(void)addressSpec;
	if (!lock_registry())
		return B_NO_MEMORY;
	registry_area* entry = find_entry(source);
	if (entry == NULL || !team_alive(entry->team)) {
		unlock_registry();
		return B_BAD_VALUE;
	}
	area_id backing = entry->backing;
	size_t size = entry->size;
	uint32 lock = entry->lock;
	unlock_registry();

	return add_area(name, destAddress, backing, size, lock, protection);
}


area_id
find_area(const char* name)
{
	if (name == NULL)
		return B_BAD_VALUE;
	if (!lock_registry())
		return B_NAME_NOT_FOUND;
	area_id found = B_NAME_NOT_FOUND;
	for (int i = 0; i < kMaxAreas; i++) {
		registry_area* entry = &sRegistry->areas[i];
		if (entry->id != 0 && strcmp(entry->name, name) == 0) {
			if (!team_alive(entry->team)) {
				release_entry(entry);
				continue;
			}
			found = entry->id;
			break;
		}
	}
	unlock_registry();
	return found;
}


area_id
area_for(void* address)
{
	area_id found = B_ERROR;
	pthread_mutex_lock(&sLocalLock);
	for (int i = 0; i < kMaxLocalAreas; i++) {
		char* start = (char*)sLocal[i].address;
		if (sLocal[i].id > 0 && (char*)address >= start
			&& (char*)address < start + sLocal[i].size) {
			found = sLocal[i].id;
			break;
		}
	}
	pthread_mutex_unlock(&sLocalLock);
	return found;
}


status_t
delete_area(area_id id)
{
	if (!lock_registry())
		return B_ERROR;
	registry_area* entry = find_entry(id);
	if (entry == NULL || entry->team != getpid()) {
		unlock_registry();
		return B_ERROR;
	}
	pthread_mutex_lock(&sLocalLock);
	local_area* local = find_local(id);
	if (local != NULL) {
		munmap(local->address, page_round(local->size));
		memset(local, 0, sizeof(*local));
	}
	pthread_mutex_unlock(&sLocalLock);
	release_entry(entry);
	unlock_registry();
	return B_OK;
}


status_t
_get_area_info(area_id id, area_info* info, size_t size)
{
	(void)size;
	if (info == NULL)
		return B_BAD_VALUE;
	if (!lock_registry())
		return B_BAD_VALUE;
	registry_area* entry = find_entry(id);
	if (entry != NULL)
		fill_info(entry, info);
	unlock_registry();
	return entry != NULL ? B_OK : B_BAD_VALUE;
}


status_t
_get_next_area_info(team_id team, ssize_t* cookie, area_info* info,
	size_t size)
{
	(void)size;
	if (cookie == NULL || info == NULL)
		return B_BAD_VALUE;
	if (team == B_CURRENT_TEAM)
		team = getpid();
	if (!lock_registry())
		return B_ENTRY_NOT_FOUND;
	/* The cookie is a slot index, so areas added meanwhile don't upset it */
	for (ssize_t i = *cookie < 0 ? 0 : *cookie; i < kMaxAreas; i++) {
		registry_area* entry = &sRegistry->areas[i];
		if (entry->id != 0 && entry->team == team) {
			fill_info(entry, info);
			*cookie = i + 1;
			unlock_registry();
			return B_OK;
		}
	}
	unlock_registry();
	return B_ENTRY_NOT_FOUND;
}


status_t
resize_area(area_id id, size_t newSize)
{
	if (newSize == 0)
		return B_BAD_VALUE;
	if (!lock_registry())
		return B_BAD_VALUE;
	registry_area* entry = find_entry(id);
	if (entry == NULL || entry->team != getpid()) {
		unlock_registry();
		return B_BAD_VALUE;
	}
	if (entry->size == newSize) {
		unlock_registry();
		return B_OK;
	}

	char name[64];
	backing_name(entry->backing, name, sizeof(name));
	int fd = shm_open(name, O_RDWR, 0600);
	/* Only grow the object: clones elsewhere may still map all of it. */
	struct stat st;
	if (fd < 0 || fstat(fd, &st) != 0
		|| (st.st_size < (off_t)page_round(newSize)
			&& ftruncate(fd, page_round(newSize)) != 0)) {
		if (fd >= 0)
			close(fd);
		unlock_registry();
		return B_NO_MEMORY;
	}
	close(fd);

	pthread_mutex_lock(&sLocalLock);
	local_area* local = find_local(id);
	void* address = MAP_FAILED;
	if (local != NULL) {
		/* Haiku resizes in place; move only when that's impossible, as
		 * this implementation always has. */
		address = mremap(local->address, page_round(local->size),
			page_round(newSize), 0);
		if (address == MAP_FAILED) {
			address = mremap(local->address, page_round(local->size),
				page_round(newSize), MREMAP_MAYMOVE);
		}
		if (address != MAP_FAILED) {
			local->address = address;
			local->size = newSize;
		}
	}
	pthread_mutex_unlock(&sLocalLock);
	if (address == MAP_FAILED) {
		unlock_registry();
		return B_NO_MEMORY;
	}
	entry->address = address;
	entry->size = newSize;
	unlock_registry();
	return B_OK;
}


/* Private: hands an area to another team: it's unmapped here, and the
 * target maps it when it looks it up; returns the area's ID, which stays the
 * same. */
status_t
_kern_transfer_area(area_id id, void** _address, uint32 addressSpec,
	team_id target)
{
	(void)addressSpec;
	if (!lock_registry())
		return B_BAD_VALUE;
	registry_area* entry = find_entry(id);
	if (entry == NULL) {
		unlock_registry();
		return B_BAD_VALUE;
	}
	if (target != getpid() && entry->team == getpid()) {
		pthread_mutex_lock(&sLocalLock);
		local_area* local = find_local(id);
		if (local != NULL) {
			munmap(local->address, page_round(local->size));
			memset(local, 0, sizeof(*local));
		}
		pthread_mutex_unlock(&sLocalLock);
		entry->address = NULL;
	}
	entry->team = target;
	if (_address != NULL)
		*_address = entry->address;
	unlock_registry();
	return id;
}


status_t
set_area_protection(area_id id, uint32 newProtection)
{
	if (!lock_registry())
		return B_BAD_VALUE;
	registry_area* entry = find_entry(id);
	if (entry == NULL) {
		unlock_registry();
		return B_BAD_VALUE;
	}
	pthread_mutex_lock(&sLocalLock);
	local_area* local = find_local(id);
	int status = 0;
	if (local != NULL) {
		status = mprotect(local->address, page_round(local->size),
			mmap_protection(newProtection));
	}
	pthread_mutex_unlock(&sLocalLock);
	if (status == 0)
		entry->protection = newProtection;
	unlock_registry();
	return status == 0 ? B_OK : B_BAD_VALUE;
}


int32
_area_count_for_team(team_id team)
{
	if (!lock_registry())
		return 0;
	int32 count = 0;
	for (int i = 0; i < kMaxAreas; i++) {
		if (sRegistry->areas[i].id != 0 && sRegistry->areas[i].team == team)
			count++;
	}
	unlock_registry();
	return count;
}
