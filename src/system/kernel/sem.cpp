/*
 * Copyright 2004-2025, Bill Hayden
 * Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2002-2010, Axel Dörfler, axeld@pinc-software.de.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Travis Geiselbrecht. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */


/*

Semaphores for thread-only use, each one a count and a FIFO queue of waiting
threads, all under one mutex. As on Haiku, a release hands the count to the
threads at the head of the queue, and a thread acquires straight away only
when no one is waiting: a thread that releases and acquires again in a loop
(a looper between messages) can't keep a waiting thread out. POSIX semaphores
don't queue their waiters, which let it.

*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#include <OS.h>

#include <errno.h>
#include <time.h>

// Platform-specific system headers
#ifdef _WIN32
	#include <process.h>  // For getpid()
#else
	#include <unistd.h>   // For getpid() on POSIX
#endif

//#define TRACE_SEM
#ifdef TRACE_SEM
#	define TRACE(x) printf x
#else
#	define TRACE(x) ;
#endif

#define MAX_SEMS 4096

// Still waiting; otherwise the result of the wait
#define SEM_WAITING 1

// A thread blocked in acquire_sem_etc(), on its own stack
struct sem_waiter {
	sem_waiter* next;
	sem_waiter* previous;
	pthread_cond_t condition;
	int32 count;
	status_t status;
};

// Internal semaphore structure
struct cosmoe_sem {
	int32 count;			// available now
	char name[B_OS_NAME_LENGTH];
	team_id owner;
	bool in_use;
	sem_waiter* head;		// oldest waiter
	sem_waiter* tail;
	int32 waiter_count;		// threads waiting
	int32 waiting_count;	// the count they wait for, together
};

static struct cosmoe_sem sem_table[MAX_SEMS];
static pthread_mutex_t sem_table_lock = PTHREAD_MUTEX_INITIALIZER;

static void construct_sem_timeout(struct timespec* tmout,
								  uint32 flags,
								  bigtime_t raw_timeout);


// The count as Haiku reports it: negative while threads wait
static int32
reported_count(const cosmoe_sem& sem)
{
	return sem.waiter_count > 0 ? sem.count - sem.waiting_count : sem.count;
}


// Call with sem_table_lock held
static void
dequeue_waiter(cosmoe_sem& sem, sem_waiter* waiter)
{
	if (waiter->previous != NULL)
		waiter->previous->next = waiter->next;
	else
		sem.head = waiter->next;
	if (waiter->next != NULL)
		waiter->next->previous = waiter->previous;
	else
		sem.tail = waiter->previous;
	waiter->next = waiter->previous = NULL;
	sem.waiter_count--;
	sem.waiting_count -= waiter->count;
}


// Hands the count to the waiters at the head of the queue, in order, as long
// as it covers what the first one waits for. Call with sem_table_lock held.
static void
wake_waiters(cosmoe_sem& sem)
{
	while (sem.head != NULL && sem.head->count <= sem.count) {
		sem_waiter* waiter = sem.head;
		sem.count -= waiter->count;
		dequeue_waiter(sem, waiter);
		waiter->status = B_OK;
		pthread_cond_signal(&waiter->condition);
	}
}


sem_id create_sem_etc(int32 count,
					  const char *name,
					  team_id owner)
{
	TRACE(("create_sem_etc: enter\n"));

	if (count < 0)
		return B_BAD_VALUE;

	pthread_mutex_lock(&sem_table_lock);

	// Find a free slot
	sem_id id = -1;
	for (int i = 0; i < MAX_SEMS; i++) {
		if (!sem_table[i].in_use) {
			id = i;
			break;
		}
	}

	if (id == -1) {
		pthread_mutex_unlock(&sem_table_lock);
		TRACE(("create_sem_etc(): no free semaphore slots\n"));
		return B_NO_MORE_SEMS;
	}

	// Set up metadata
	cosmoe_sem& sem = sem_table[id];
	sem.in_use = true;
	sem.count = count;
	sem.owner = owner;
	sem.head = sem.tail = NULL;
	sem.waiter_count = 0;
	sem.waiting_count = 0;
	if (name) {
		strncpy(sem.name, name, B_OS_NAME_LENGTH - 1);
		sem.name[B_OS_NAME_LENGTH - 1] = '\0';
	} else {
		strcpy(sem.name, "unnamed sem");
	}

	pthread_mutex_unlock(&sem_table_lock);

	TRACE(("create_sem_etc(): created sem %d\n", id));
	return id;
}


sem_id create_sem(int32 count,
				  const char *name)
{
	return create_sem_etc(count, name, getpid());
}


status_t delete_sem(sem_id id)
{
	return delete_sem_etc(id, 0, false);
}


status_t delete_sem_etc(sem_id id,
						status_t return_code,
						bool interrupted)
{
	TRACE(("delete_sem_etc(%ld): enter\n", id));

	if (id < 0 || id >= MAX_SEMS)
		return B_BAD_SEM_ID;

	pthread_mutex_lock(&sem_table_lock);

	cosmoe_sem& sem = sem_table[id];
	if (!sem.in_use) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_SEM_ID;
	}

	// Waiters return B_BAD_SEM_ID; each wakes on its own condition, so
	// nothing is left for them to touch once they run
	sem.in_use = false;
	while (sem.head != NULL) {
		sem_waiter* waiter = sem.head;
		dequeue_waiter(sem, waiter);
		waiter->status = B_BAD_SEM_ID;
		pthread_cond_signal(&waiter->condition);
	}

	pthread_mutex_unlock(&sem_table_lock);

	return B_OK;
}


status_t acquire_sem(sem_id id)
{
	return acquire_sem_etc(id, 1, 0, 0);
}


struct sem_wait_context {
	sem_id id;
	sem_waiter* waiter;
};


// Runs when a waiting thread is cancelled (kill_thread()): pthread_cond_wait()
// returns to it with the mutex held. Gives back what it was granted, takes it
// out of the queue and unlocks.
static void
sem_wait_cancelled(void* data)
{
	sem_wait_context* context = (sem_wait_context*)data;
	cosmoe_sem& sem = sem_table[context->id];
	sem_waiter* waiter = context->waiter;
	if (waiter->status == SEM_WAITING) {
		dequeue_waiter(sem, waiter);
		wake_waiters(sem);
	} else if (waiter->status == B_OK) {
		sem.count += waiter->count;
		wake_waiters(sem);
	}
	pthread_mutex_unlock(&sem_table_lock);
	pthread_cond_destroy(&waiter->condition);
}


status_t acquire_sem_etc(sem_id id,
						 int32 count,
						 uint32 flags,
						 bigtime_t timeout)
{
	struct timespec tmout;

	TRACE(("acquire_sem_etc(%ld): enter\n", id));

	if (id < 0 || id >= MAX_SEMS)
		return B_BAD_SEM_ID;

	pthread_mutex_lock(&sem_table_lock);

	cosmoe_sem& sem = sem_table[id];
	if (!sem.in_use) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_SEM_ID;
	}

	if (count <= 0) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_VALUE;
	}

	// Only when no one is waiting, or a thread could pass the queue
	if (sem.head == NULL && sem.count >= count) {
		sem.count -= count;
		pthread_mutex_unlock(&sem_table_lock);
		return B_OK;
	}

	// Check if timeout is requested and not infinite
	bool has_timeout = (flags & (B_RELATIVE_TIMEOUT | B_ABSOLUTE_TIMEOUT))
	                   && (timeout != B_INFINITE_TIMEOUT);

	if (has_timeout && timeout <= 0 && (flags & B_RELATIVE_TIMEOUT)) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_WOULD_BLOCK;
	}
	if (has_timeout)
		construct_sem_timeout(&tmout, flags, timeout);

	sem_waiter waiter;
	pthread_cond_init(&waiter.condition, NULL);
	waiter.count = count;
	waiter.status = SEM_WAITING;
	waiter.next = NULL;
	waiter.previous = sem.tail;
	if (sem.tail != NULL)
		sem.tail->next = &waiter;
	else
		sem.head = &waiter;
	sem.tail = &waiter;
	sem.waiter_count++;
	sem.waiting_count += count;

	sem_wait_context context = { id, &waiter };
	pthread_cleanup_push(sem_wait_cancelled, &context);

	while (waiter.status == SEM_WAITING) {
		int err = has_timeout
			? pthread_cond_timedwait(&waiter.condition, &sem_table_lock, &tmout)
			: pthread_cond_wait(&waiter.condition, &sem_table_lock);
		if (err == ETIMEDOUT && waiter.status == SEM_WAITING) {
			dequeue_waiter(sem, &waiter);
			// It may have been the one holding the others up
			wake_waiters(sem);
			waiter.status = B_TIMED_OUT;
		}
	}

	pthread_cleanup_pop(0);

	status_t status = waiter.status;
	pthread_mutex_unlock(&sem_table_lock);
	pthread_cond_destroy(&waiter.condition);

	return status;
}


status_t
release_sem(sem_id id)
{
	return release_sem_etc(id, 1, 0);
}


status_t
release_sem_etc(sem_id id, int32 count, uint32 flags)
{
	TRACE(("release_sem_etc(%ld): enter\n", id));

	if (id < 0 || id >= MAX_SEMS)
		return B_BAD_SEM_ID;

	pthread_mutex_lock(&sem_table_lock);

	cosmoe_sem& sem = sem_table[id];
	if (!sem.in_use) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_SEM_ID;
	}

	if ((flags & B_RELEASE_ALL) != 0) {
		// Enough for every waiter; nothing when there's none
		count = sem.waiting_count - sem.count;
		if (count < 0)
			count = 0;
	} else if (count <= 0) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_VALUE;
	} else if ((flags & B_RELEASE_IF_WAITING_ONLY) != 0
		&& sem.waiter_count == 0) {
		count = 0;
	}

	sem.count += count;
	wake_waiters(sem);

	pthread_mutex_unlock(&sem_table_lock);

	return B_OK;
}


status_t
get_sem_count(sem_id id, int32 *_count)
{
	TRACE(("get_sem_count(%ld): enter\n", id));

	if (id < 0 || id >= MAX_SEMS)
		return B_BAD_SEM_ID;

	if (_count == NULL)
		return B_BAD_VALUE;

	pthread_mutex_lock(&sem_table_lock);

	if (!sem_table[id].in_use) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_SEM_ID;
	}

	*_count = reported_count(sem_table[id]);

	pthread_mutex_unlock(&sem_table_lock);

	return B_OK;
}


/*!	Called by the get_sem_info() macro. */
status_t
_get_sem_info(sem_id id, struct sem_info *info, size_t size)
{
	TRACE(("_get_sem_info(%ld): enter\n", id));

	if (id < 0 || id >= MAX_SEMS)
		return B_BAD_SEM_ID;
	
	pthread_mutex_lock(&sem_table_lock);
	
	if (!sem_table[id].in_use) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_SEM_ID;
	}
	
	if (info == NULL || size != sizeof(sem_info)) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_VALUE;
	}

	info->sem = id;
	info->team = sem_table[id].owner;
	strncpy(info->name, sem_table[id].name, B_OS_NAME_LENGTH - 1);
	info->name[B_OS_NAME_LENGTH - 1] = '\0';
	
	info->count = reported_count(sem_table[id]);
	info->latest_holder	= 0;
	
	pthread_mutex_unlock(&sem_table_lock);
	
	return B_OK;
}


/*!	Called by the get_next_sem_info() macro. */
status_t
_get_next_sem_info(team_id teamID, int32 *_cookie, struct sem_info *info,
	size_t size)
{
	int32 slot = *_cookie;
	
	TRACE(("_get_next_sem_info(): enter, cookie=%d\n", slot));
	
	// Validate team_id (must be non-negative, 0 means current team)
	if (teamID < 0)
		return B_BAD_TEAM_ID;
	
	if (slot < 0)
		slot = 0;
	
	pthread_mutex_lock(&sem_table_lock);
	
	// Find the next valid semaphore
	while (slot < MAX_SEMS) {
		if (sem_table[slot].in_use) {
			// Found one, fill in the info directly (can't call _get_sem_info - would deadlock)
			if (info != NULL && size == sizeof(sem_info)) {
				info->sem = slot;
				info->team = sem_table[slot].owner;
				strncpy(info->name, sem_table[slot].name, B_OS_NAME_LENGTH - 1);
				info->name[B_OS_NAME_LENGTH - 1] = '\0';
				
				info->count = reported_count(sem_table[slot]);
				info->latest_holder = 0;
				
				*_cookie = slot + 1;  // Move to next slot for next call
				pthread_mutex_unlock(&sem_table_lock);
				return B_OK;
			}
		}
		slot++;
	}
	
	pthread_mutex_unlock(&sem_table_lock);
	
	return B_BAD_VALUE;  // No more semaphores
}


/* kinda useless in Cosmoe on Wayland, but kept for compatibility */
status_t
set_sem_owner(sem_id id, team_id newTeamID)
{
	TRACE(("set_sem_owner(%ld): enter\n", id));

	if (id < 0 || id >= MAX_SEMS)
		return B_BAD_SEM_ID;
	
	// Validate team_id (must be positive, or B_SYSTEM_TEAM which is 1)
	if (newTeamID < 0)
		return B_BAD_TEAM_ID;
	
	pthread_mutex_lock(&sem_table_lock);
	
	if (!sem_table[id].in_use) {
		pthread_mutex_unlock(&sem_table_lock);
		return B_BAD_SEM_ID;
	}
	
	sem_table[id].owner = newTeamID;
	
	pthread_mutex_unlock(&sem_table_lock);
	
	return B_OK;
}


void construct_sem_timeout(struct timespec* ts, uint32 flags, bigtime_t timeout)
{
	if (timeout == B_INFINITE_TIMEOUT) {
		// Set to the maximum possible time without overflow
		ts->tv_sec = 0x7FFFFFFF;
		ts->tv_nsec = 999999999L;
		return;
	}
	
	if ((flags & B_ABSOLUTE_TIMEOUT) && (flags & B_TIMEOUT_REAL_TIME_BASE))
	{
		// Real time already: microseconds since the epoch
		ts->tv_sec = timeout / 1000000LL;
		ts->tv_nsec = (timeout % 1000000LL) * 1000L;
	}
	else
	{
		// An absolute timeout is in system_time() (microseconds since boot):
		// make it relative first
		if (flags & B_ABSOLUTE_TIMEOUT)
			timeout -= system_time();
		if (timeout < 0)
			timeout = 0;

		// Convert relative timeout to absolute CLOCK_REALTIME time
		// pthread_cond_timedwait() requires absolute time based on CLOCK_REALTIME
		struct timespec now;
		clock_gettime(CLOCK_REALTIME, &now);
		
		// Add the relative timeout (in microseconds) to current time
		int64 total_nsec = (now.tv_sec * 1000000000LL) + now.tv_nsec + (timeout * 1000LL);
		
		ts->tv_sec = total_nsec / 1000000000LL;
		ts->tv_nsec = total_nsec % 1000000000LL;
	}

	/* If we ended up with an overflow in tv_nsec, spill it into tv_sec */
	while (ts->tv_nsec >= 1000000000L)
	{
		ts->tv_sec++;
		ts->tv_nsec -= 1000000000L;
	}
}


static int dump_sem_list(void)
{
	int count = 0;

	pthread_mutex_lock(&sem_table_lock);
	
	for (int id = 0; id < MAX_SEMS; id++) 
	{
		if (sem_table[id].in_use)
		{
			printf("id: %d\t\tcount: %d\t\tname: %s\n", id,
				(int)reported_count(sem_table[id]), sem_table[id].name);
			count++;
		}
	}
	
	pthread_mutex_unlock(&sem_table_lock);
	
	return count;
}


static void dump_sem(int id)
{
	if (id < 0 || id >= MAX_SEMS)
	{
		printf("A semaphore with that ID has never existed.\n");
		return;
	}
	
	pthread_mutex_lock(&sem_table_lock);
	
	if (sem_table[id].in_use)
	{
		printf("id: %d\t\tcount: %d\t\tname: %s\n", id,
			(int)reported_count(sem_table[id]), sem_table[id].name);
	}
	else
	{
		printf("There is no active semaphore with that ID.\n");
	}
	
	pthread_mutex_unlock(&sem_table_lock);
}

int dump_sem_info(int argc, char **argv)
{
	if (argc < 2)
		dump_sem_list();
	else
		dump_sem(atoi(argv[1]));

	return 0;
}
