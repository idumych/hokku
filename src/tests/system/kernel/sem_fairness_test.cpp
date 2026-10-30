/*
 * Copyright 2026, Hokku contributors.
 * Distributed under the terms of the MIT License.
 *
 * Semaphores hand their count to waiting threads in order, as on Haiku: a
 * thread that releases and acquires again in a loop (a looper between
 * messages) can't keep a waiting thread out. Also checks the paths that
 * leave the queue early: timeouts, kill_thread() and delete_sem().
 */

#include <stdio.h>

#include <OS.h>

static int sFailures = 0;

static void
check(bool success, const char* what)
{
	if (!success)
		sFailures++;
	printf("  %s: %s\n", what, success ? "pass" : "FAIL");
}


// Holds the semaphore as a busy looper holds its lock: for a while (a
// message), then releases it and takes it straight back, again and again
static sem_id sLock;
static volatile bool sStop;

static int32
hog_thread(void*)
{
	acquire_sem(sLock);
	while (!sStop) {
		bigtime_t until = system_time() + 200;
		while (system_time() < until)
			;
		release_sem(sLock);
		acquire_sem(sLock);
	}
	release_sem(sLock);
	return 0;
}


static void
fairness_test()
{
	printf("fairness: a waiter gets in while another thread loops on the sem\n");
	sLock = create_sem(1, "fairness");
	sStop = false;
	thread_id hog = spawn_thread(hog_thread, "hog", B_NORMAL_PRIORITY, NULL);
	resume_thread(hog);
	snooze(20000);

	int got = 0, timedOut = 0;
	bigtime_t worst = 0;
	for (int i = 0; i < 50; i++) {
		bigtime_t start = system_time();
		if (acquire_sem_etc(sLock, 1, B_RELATIVE_TIMEOUT, 100000) == B_OK) {
			got++;
			release_sem(sLock);
		} else
			timedOut++;
		bigtime_t waited = system_time() - start;
		if (waited > worst)
			worst = waited;
	}
	printf("  got it %d of 50 times, longest wait %lld us\n", got,
		(long long)worst);
	check(timedOut == 0, "never timed out");
	check(worst < 50000, "no wait near the timeout");

	sStop = true;
	status_t result;
	wait_for_thread(hog, &result);
	delete_sem(sLock);
}


static sem_id sOrder;
static int32 sOrderLog[3];
static int32 sOrderCount;

static int32
order_thread(void* data)
{
	if (acquire_sem(sOrder) == B_OK)
		sOrderLog[atomic_add(&sOrderCount, 1)] = (int32)(addr_t)data;
	return 0;
}


static void
order_test()
{
	printf("order: waiters are woken in the order they came\n");
	sOrder = create_sem(0, "order");
	sOrderCount = 0;
	thread_id threads[3];
	for (int i = 0; i < 3; i++) {
		threads[i] = spawn_thread(order_thread, "order", B_NORMAL_PRIORITY,
			(void*)(addr_t)i);
		resume_thread(threads[i]);
		snooze(20000);
	}
	for (int i = 0; i < 3; i++) {
		release_sem(sOrder);
		snooze(20000);
	}
	status_t result;
	for (int i = 0; i < 3; i++)
		wait_for_thread(threads[i], &result);
	check(sOrderCount == 3 && sOrderLog[0] == 0 && sOrderLog[1] == 1
		&& sOrderLog[2] == 2, "first come, first served");
	delete_sem(sOrder);
}


static sem_id sBig;

static int32
big_waiter_thread(void*)
{
	// Waits for more than will come, then times out
	return acquire_sem_etc(sBig, 5, B_RELATIVE_TIMEOUT, 100000);
}


static int32
small_waiter_thread(void*)
{
	return acquire_sem_etc(sBig, 1, B_RELATIVE_TIMEOUT, 1000000);
}


static void
timeout_head_test()
{
	printf("timeout: a waiter that times out lets the ones behind it in\n");
	sBig = create_sem(0, "big");
	thread_id big = spawn_thread(big_waiter_thread, "big", B_NORMAL_PRIORITY,
		NULL);
	resume_thread(big);
	snooze(20000);
	thread_id small = spawn_thread(small_waiter_thread, "small",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(small);
	snooze(20000);

	// Not enough for the first waiter, so the second waits behind it
	release_sem(sBig);
	snooze(20000);
	int32 count;
	get_sem_count(sBig, &count);
	check(count == 1 - 6, "count is what's there less what's waited for");

	status_t bigResult, smallResult;
	wait_for_thread(big, &bigResult);
	bigtime_t start = system_time();
	wait_for_thread(small, &smallResult);
	check(bigResult == B_TIMED_OUT, "the big waiter timed out");
	check(smallResult == B_OK && system_time() - start < 50000,
		"the small one got in as soon as it did");
	get_sem_count(sBig, &count);
	check(count == 0, "count is back to 0");
	delete_sem(sBig);
}


static sem_id sKill;

static int32
blocked_thread(void*)
{
	acquire_sem(sKill);
	return 0;
}


static void
kill_test()
{
	printf("kill: a killed waiter leaves the queue\n");
	sKill = create_sem(0, "kill");
	thread_id blocked = spawn_thread(blocked_thread, "blocked",
		B_NORMAL_PRIORITY, NULL);
	resume_thread(blocked);
	snooze(20000);
	kill_thread(blocked);
	snooze(20000);

	int32 count;
	check(get_sem_count(sKill, &count) == B_OK && count == 0,
		"no waiter left behind");
	release_sem(sKill);
	check(acquire_sem_etc(sKill, 1, B_RELATIVE_TIMEOUT, 100000) == B_OK,
		"the count goes to the next thread");
	delete_sem(sKill);
}


static sem_id sDeleted;

static int32
deleted_waiter_thread(void*)
{
	return acquire_sem(sDeleted);
}


static void
delete_test()
{
	printf("delete: waiters return B_BAD_SEM_ID at once\n");
	sDeleted = create_sem(0, "deleted");
	thread_id threads[3];
	for (int i = 0; i < 3; i++) {
		threads[i] = spawn_thread(deleted_waiter_thread, "deleted",
			B_NORMAL_PRIORITY, NULL);
		resume_thread(threads[i]);
	}
	snooze(20000);
	bigtime_t start = system_time();
	delete_sem(sDeleted);
	bool allBad = true;
	for (int i = 0; i < 3; i++) {
		status_t result;
		wait_for_thread(threads[i], &result);
		allBad &= result == B_BAD_SEM_ID;
	}
	check(allBad, "all got B_BAD_SEM_ID");
	check(system_time() - start < 50000, "without delay");
}


static void
absolute_timeout_test()
{
	printf("absolute timeout: waits until the system_time() given\n");
	sem_id sem = create_sem(0, "absolute");
	bigtime_t start = system_time();
	status_t result = acquire_sem_etc(sem, 1, B_ABSOLUTE_TIMEOUT,
		start + 50000);
	bigtime_t waited = system_time() - start;
	check(result == B_TIMED_OUT, "timed out");
	check(waited >= 45000 && waited < 150000, "after about 50 ms");
	delete_sem(sem);
}


static void
release_flags_test()
{
	printf("release flags\n");
	sem_id sem = create_sem(0, "flags");
	release_sem_etc(sem, 2, B_RELEASE_IF_WAITING_ONLY);
	int32 count;
	get_sem_count(sem, &count);
	check(count == 0, "B_RELEASE_IF_WAITING_ONLY adds nothing with no waiter");
	release_sem_etc(sem, 0, B_RELEASE_ALL);
	get_sem_count(sem, &count);
	check(count == 0, "B_RELEASE_ALL adds nothing with no waiter");
	delete_sem(sem);
}


int
main()
{
	fairness_test();
	order_test();
	timeout_head_test();
	kill_test();
	delete_test();
	absolute_timeout_test();
	release_flags_test();
	printf("sem_fairness_test: failures=%d\n", sFailures);
	return sFailures == 0 ? 0 : 1;
}
