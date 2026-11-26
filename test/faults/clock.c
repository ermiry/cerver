#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <pthread.h>

#include <cerver/threads/thread.h>

#include "../test.h"
#include "clock.h"
#include "faults.h"

// the waits of cerver with a jump of the clock of the system: they keep their timeouts

// a jump forward of the realtime clock, longer than any test
#define CLOCK_JUMP_SECONDS				(1 << 12)

#define CLOCK_WAIT_SECONDS				1

// what a wait takes past its timeout
#define CLOCK_WAIT_MARGIN_SECONDS		2

#define CLOCK_STEP_NANOSECONDS			(1000 * 1000)

// read and written atomically
static bool clock_released = false;

typedef struct ClockWait {

	ThreadGroup *group;

	// read and written atomically
	bool returned;

} ClockWait;

static void clock_step (void) {

	const struct timespec step = { 0, CLOCK_STEP_NANOSECONDS };
	(void) nanosleep (&step, NULL);

}

// a thread of the group that lives until the test releases it
static void *clock_blocked (void *data) {

	(void) data;

	while (!__atomic_load_n (&clock_released, __ATOMIC_ACQUIRE)) clock_step ();

	return NULL;

}

static void *clock_waiter (void *wait_ptr) {

	ClockWait *wait = (ClockWait *) wait_ptr;

	(void) thread_group_wait (wait->group, CLOCK_WAIT_SECONDS);
	__atomic_store_n (&wait->returned, true, __ATOMIC_RELEASE);

	return NULL;

}

// waits for the waiter in its timeout and a margin, with the monotonic clock of the test
static bool clock_waiter_returned (const ClockWait *wait) {

	const double deadline = test_wait_now () + (double) (CLOCK_WAIT_SECONDS + CLOCK_WAIT_MARGIN_SECONDS);

	bool returned = false;
	while (!returned && (test_wait_now () < deadline)) {
		returned = __atomic_load_n (&wait->returned, __ATOMIC_ACQUIRE);
		if (!returned) clock_step ();
	}

	return returned;

}

// a thread group whose wait starts while the realtime clock is ahead ends in its timeout
// (its deadline came from CLOCK_REALTIME, so it waited until the clock passed the jump)
static void faults_clock_thread_group_wait (void) {

	ThreadGroup group;
	thread_group_init (&group);

	__atomic_store_n (&clock_released, false, __ATOMIC_RELEASE);
	test_check_unsigned_eq (thread_group_create (&group, clock_blocked, NULL), 0, NULL);

	ClockWait wait = { &group, false };

	faults_shift_realtime (CLOCK_JUMP_SECONDS);

	pthread_t waiter;
	test_check_int_eq (pthread_create (&waiter, NULL, clock_waiter, &wait), 0, NULL);

	const bool returned = clock_waiter_returned (&wait);

	faults_shift_realtime (0);

	// the blocked thread ends, so a waiter that is still waiting sees the group end
	__atomic_store_n (&clock_released, true, __ATOMIC_RELEASE);
	test_check_int_eq (pthread_join (waiter, NULL), 0, NULL);

	test_check (returned, "the wait of the thread group moved with the realtime clock");

	test_check_unsigned_eq (thread_group_wait (&group, THREAD_GROUP_WAIT_FOREVER), 0, NULL);
	thread_group_end (&group);

}

void faults_tests_clock (void) {

	(void) printf ("Testing FAULTS clock...\n");

	faults_clock_thread_group_wait ();

	(void) printf ("Done!\n");

}
