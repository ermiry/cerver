#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include "cerver/events/internal.h"

#include "../test.h"
#include "events.h"

// the clock of the tests starts in the middle of a tick, far from zero
#define WHEEL_TEST_START					((u64) ((1000 * EVENT_WHEEL_TICK) + 100))

// the timers of the tests with many of them
#define WHEEL_TEST_TIMERS					64

// a turn of the wheel in milliseconds
#define WHEEL_TEST_TURN						((u64) (EVENT_WHEEL_TICK * EVENT_WHEEL_SLOTS))

// the times that a timer arms itself again in its callback
#define WHEEL_TEST_REARMS					4

// a deadline of the tests, in milliseconds after the start
#define WHEEL_TEST_DELAY					1000

typedef struct WheelTestExpired {

	EventWheel *wheel;

	unsigned int count;
	EventTimer *last;

	// when not zero, each expired timer is armed again these milliseconds later
	u64 rearm;
	u64 now;

} WheelTestExpired;

static void wheel_test_expired (EventTimer *timer, void *args) {

	WheelTestExpired *expired = (WheelTestExpired *) args;

	// a timer is out of the wheel when it is reported
	test_check_false (timer->armed);

	expired->count += 1;
	expired->last = timer;

	if (expired->rearm) event_wheel_add (expired->wheel, timer, expired->now + expired->rearm);

}

static WheelTestExpired wheel_test_expired_create (EventWheel *wheel) {

	WheelTestExpired expired;
	(void) memset (&expired, 0, sizeof (WheelTestExpired));
	expired.wheel = wheel;

	return expired;

}

static void test_wheel_init (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	test_check_unsigned_eq (wheel.armed, 0, NULL);
	test_check_int_eq (event_wheel_next (&wheel, WHEEL_TEST_START), EVENT_WHEEL_NEXT_NONE, NULL);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);
	test_check_unsigned_eq (
		event_wheel_advance (&wheel, WHEEL_TEST_START + WHEEL_TEST_TURN, wheel_test_expired, &expired), 0, NULL
	);

	// NULL arguments are ignored
	event_wheel_init (NULL, 0);
	event_wheel_add (NULL, NULL, 0);
	event_wheel_add (&wheel, NULL, 0);
	event_wheel_remove (NULL, NULL);
	event_wheel_remove (&wheel, NULL);
	test_check_unsigned_eq (event_wheel_advance (NULL, 0, wheel_test_expired, &expired), 0, NULL);
	test_check_int_eq (event_wheel_next (NULL, 0), EVENT_WHEEL_NEXT_NONE, NULL);

}

// a timer never expires before its deadline, and it expires with the first tick after it
static void test_wheel_expires_after_deadline (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	const u64 deadline = WHEEL_TEST_START + WHEEL_TEST_DELAY;
	event_wheel_add (&wheel, &timer, deadline);

	test_check_true (timer.armed);
	test_check_unsigned_eq (wheel.armed, 1, NULL);
	test_check_unsigned_eq (timer.deadline, deadline, NULL);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);

	// every millisecond before the deadline
	for (u64 now = WHEEL_TEST_START; now < deadline; now++) {
		test_check_unsigned_eq (event_wheel_advance (&wheel, now, wheel_test_expired, &expired), 0, NULL);
	}

	test_check_true (timer.armed);

	// and it is reported at most a tick after it
	unsigned int count = 0;
	u64 now = deadline;
	for (; (now < (deadline + EVENT_WHEEL_TICK)) && !count; now++) {
		count = event_wheel_advance (&wheel, now, wheel_test_expired, &expired);
	}

	test_check_unsigned_eq (count, 1, NULL);
	test_check_unsigned_eq (expired.count, 1, NULL);
	test_check_ptr_eq (expired.last, &timer);
	test_check_false (timer.armed);
	test_check_unsigned_eq (wheel.armed, 0, NULL);

	// only once
	test_check_unsigned_eq (
		event_wheel_advance (&wheel, now + WHEEL_TEST_TURN, wheel_test_expired, &expired), 0, NULL
	);

}

// a deadline that is exactly the start of a tick expires with that tick
static void test_wheel_deadline_on_tick (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	const u64 deadline = ((WHEEL_TEST_START / EVENT_WHEEL_TICK) + 2) * EVENT_WHEEL_TICK;
	event_wheel_add (&wheel, &timer, deadline);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);

	test_check_unsigned_eq (event_wheel_advance (&wheel, deadline - 1, wheel_test_expired, &expired), 0, NULL);
	test_check_unsigned_eq (event_wheel_advance (&wheel, deadline, wheel_test_expired, &expired), 1, NULL);

}

// a deadline that already passed expires with the next tick, not never
static void test_wheel_past_deadline (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	event_wheel_add (&wheel, &timer, WHEEL_TEST_START - WHEEL_TEST_DELAY);
	test_check_true (timer.armed);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);

	test_check_unsigned_eq (event_wheel_advance (&wheel, WHEEL_TEST_START, wheel_test_expired, &expired), 0, NULL);
	test_check_unsigned_eq (
		event_wheel_advance (&wheel, WHEEL_TEST_START + EVENT_WHEEL_TICK, wheel_test_expired, &expired), 1, NULL
	);

	// and it can be removed before, from the slot where it was put
	event_wheel_add (&wheel, &timer, 0);
	event_wheel_remove (&wheel, &timer);
	test_check_false (timer.armed);
	test_check_unsigned_eq (wheel.armed, 0, NULL);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, WHEEL_TEST_START + WHEEL_TEST_TURN, wheel_test_expired, &expired), 0, NULL
	);

}

static void test_wheel_remove (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	// three timers in the same slot, removed from the middle, the start and the end
	EventTimer timers[3];
	(void) memset (timers, 0, sizeof (timers));

	const u64 deadline = WHEEL_TEST_START + WHEEL_TEST_DELAY;
	for (int idx = 0; idx < 3; idx++) event_wheel_add (&wheel, &timers[idx], deadline);

	test_check_unsigned_eq (wheel.armed, 3, NULL);

	event_wheel_remove (&wheel, &timers[1]);
	test_check_unsigned_eq (wheel.armed, 2, NULL);
	test_check_false (timers[1].armed);

	// removing it again does nothing
	event_wheel_remove (&wheel, &timers[1]);
	test_check_unsigned_eq (wheel.armed, 2, NULL);

	event_wheel_remove (&wheel, &timers[2]);
	event_wheel_remove (&wheel, &timers[0]);
	test_check_unsigned_eq (wheel.armed, 0, NULL);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);
	test_check_unsigned_eq (
		event_wheel_advance (&wheel, deadline + WHEEL_TEST_TURN, wheel_test_expired, &expired), 0, NULL
	);

	test_check_int_eq (event_wheel_next (&wheel, deadline + WHEEL_TEST_TURN), EVENT_WHEEL_NEXT_NONE, NULL);

}

// adding an armed timer moves it: it expires once, with its new deadline
static void test_wheel_move (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	const u64 first = WHEEL_TEST_START + WHEEL_TEST_DELAY;
	const u64 second = first + (4 * WHEEL_TEST_DELAY);

	event_wheel_add (&wheel, &timer, first);
	event_wheel_add (&wheel, &timer, second);

	test_check_unsigned_eq (wheel.armed, 1, NULL);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, first + EVENT_WHEEL_TICK, wheel_test_expired, &expired), 0, NULL
	);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, second + EVENT_WHEEL_TICK, wheel_test_expired, &expired), 1, NULL
	);

	// and earlier
	event_wheel_add (&wheel, &timer, second + (8 * WHEEL_TEST_DELAY));
	event_wheel_add (&wheel, &timer, second + (2 * WHEEL_TEST_DELAY));

	test_check_unsigned_eq (
		event_wheel_advance (
			&wheel, second + (2 * WHEEL_TEST_DELAY) + EVENT_WHEEL_TICK, wheel_test_expired, &expired
		), 1, NULL
	);

	test_check_unsigned_eq (wheel.armed, 0, NULL);

}

// a deadline farther than a turn of the wheel stays in its slot until its turn
static void test_wheel_longer_than_a_turn (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	// the same slot as a timer of the next tick
	EventTimer near;
	(void) memset (&near, 0, sizeof (EventTimer));

	const u64 deadline = WHEEL_TEST_START + (2 * WHEEL_TEST_TURN) + EVENT_WHEEL_TICK;
	event_wheel_add (&wheel, &timer, deadline);
	event_wheel_add (&wheel, &near, WHEEL_TEST_START + EVENT_WHEEL_TICK);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);

	// tick by tick, only the near one expires in the first two turns
	unsigned int count = 0;
	for (u64 now = WHEEL_TEST_START; now < deadline; now += EVENT_WHEEL_TICK) {
		count += event_wheel_advance (&wheel, now, wheel_test_expired, &expired);
	}

	test_check_unsigned_eq (count, 1, NULL);
	test_check_ptr_eq (expired.last, &near);
	test_check_true (timer.armed);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, deadline + EVENT_WHEEL_TICK, wheel_test_expired, &expired), 1, NULL
	);

	test_check_ptr_eq (expired.last, &timer);

}

// a wheel that is moved after a long time (the loop did not run) reports everything that passed,
// once, and nothing that has not
static void test_wheel_long_jump (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timers[WHEEL_TEST_TIMERS];
	(void) memset (timers, 0, sizeof (timers));

	// one in each of the next ticks
	for (unsigned int idx = 0; idx < WHEEL_TEST_TIMERS; idx++) {
		event_wheel_add (&wheel, &timers[idx], WHEEL_TEST_START + ((u64) (idx + 1) * EVENT_WHEEL_TICK));
	}

	EventTimer far;
	(void) memset (&far, 0, sizeof (EventTimer));

	const u64 jump = WHEEL_TEST_START + (3 * WHEEL_TEST_TURN);
	event_wheel_add (&wheel, &far, jump + WHEEL_TEST_TURN);

	WheelTestExpired expired = wheel_test_expired_create (&wheel);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, jump, wheel_test_expired, &expired), WHEEL_TEST_TIMERS, NULL
	);

	test_check_unsigned_eq (wheel.armed, 1, NULL);
	test_check_true (far.armed);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, jump + WHEEL_TEST_TURN + EVENT_WHEEL_TICK, wheel_test_expired, &expired),
		1, NULL
	);

	test_check_ptr_eq (expired.last, &far);

}

// a callback can arm its timer again: it expires again with the new deadline, not in the same pass
static void test_wheel_rearm_in_callback (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	WheelTestExpired expired = wheel_test_expired_create (&wheel);
	expired.rearm = WHEEL_TEST_DELAY;

	u64 now = WHEEL_TEST_START;
	event_wheel_add (&wheel, &timer, now + WHEEL_TEST_DELAY);

	for (unsigned int idx = 0; idx < WHEEL_TEST_REARMS; idx++) {
		now += WHEEL_TEST_DELAY + EVENT_WHEEL_TICK;
		expired.now = now;

		test_check_unsigned_eq (event_wheel_advance (&wheel, now, wheel_test_expired, &expired), 1, NULL);
		test_check_true (timer.armed);
		test_check_unsigned_eq (wheel.armed, 1, NULL);
	}

	test_check_unsigned_eq (expired.count, WHEEL_TEST_REARMS, NULL);

	// a whole turn in the same slot: armed again a turn later, it is not reported twice
	event_wheel_remove (&wheel, &timer);

	expired.rearm = WHEEL_TEST_TURN;
	expired.now = now + EVENT_WHEEL_TICK;
	event_wheel_add (&wheel, &timer, now + 1);

	test_check_unsigned_eq (
		event_wheel_advance (&wheel, now + EVENT_WHEEL_TICK, wheel_test_expired, &expired), 1, NULL
	);

	test_check_true (timer.armed);

}

// the wait of a loop: nothing without timers, the rest of the tick with them
static void test_wheel_next (void) {

	EventWheel wheel;
	event_wheel_init (&wheel, WHEEL_TEST_START);

	EventTimer timer;
	(void) memset (&timer, 0, sizeof (EventTimer));

	test_check_int_eq (event_wheel_next (&wheel, WHEEL_TEST_START), EVENT_WHEEL_NEXT_NONE, NULL);

	event_wheel_add (&wheel, &timer, WHEEL_TEST_START + (16 * WHEEL_TEST_DELAY));

	const int next = event_wheel_next (&wheel, WHEEL_TEST_START);
	test_check ((next > 0) && (next <= (int) EVENT_WHEEL_TICK), "the wait is not inside a tick");

	// the start of the next tick
	test_check_unsigned_eq ((WHEEL_TEST_START + (u64) next) % EVENT_WHEEL_TICK, 0, NULL);

	// a wheel that is late does not wait
	test_check_int_eq (event_wheel_next (&wheel, WHEEL_TEST_START + WHEEL_TEST_TURN), 0, NULL);

	event_wheel_remove (&wheel, &timer);
	test_check_int_eq (event_wheel_next (&wheel, WHEEL_TEST_START), EVENT_WHEEL_NEXT_NONE, NULL);

}

// the clock never goes back
static void test_wheel_clock (void) {

	const u64 first = event_clock_milliseconds ();
	const u64 second = event_clock_milliseconds ();

	test_check (first > 0, "the clock is zero");
	test_check (second >= first, "the clock went back");

}

void events_tests_wheel (void) {

	(void) printf ("\nTesting WHEEL...\n");

	test_run (test_wheel_init);
	test_run (test_wheel_expires_after_deadline);
	test_run (test_wheel_deadline_on_tick);
	test_run (test_wheel_past_deadline);
	test_run (test_wheel_remove);
	test_run (test_wheel_move);
	test_run (test_wheel_longer_than_a_turn);
	test_run (test_wheel_long_jump);
	test_run (test_wheel_rearm_in_callback);
	test_run (test_wheel_next);
	test_run (test_wheel_clock);

	(void) printf ("Done!\n");

}
