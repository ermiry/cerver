#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/events/internal.h"

static const u64 event_clock_milliseconds_per_second = 1000;
static const u64 event_clock_nanoseconds_per_millisecond = 1000000;

#pragma region clock

u64 event_clock_milliseconds (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((u64) now.tv_sec * event_clock_milliseconds_per_second)
		+ ((u64) now.tv_nsec / event_clock_nanoseconds_per_millisecond);

}

#pragma endregion

#pragma region slots

// the first tick that starts at or after the deadline: a timer never expires early
static u64 event_wheel_deadline_tick (const u64 deadline) {

	return (deadline + EVENT_WHEEL_TICK - 1) / EVENT_WHEEL_TICK;

}

static void event_wheel_link (EventWheel *wheel, EventTimer *timer, const u64 tick) {

	EventTimer **slot = &wheel->slots[tick % EVENT_WHEEL_SLOTS];

	timer->previous = NULL;
	timer->next = *slot;
	if (*slot) (*slot)->previous = timer;
	*slot = timer;

	timer->armed = true;
	wheel->armed += 1;

}

static void event_wheel_unlink (EventWheel *wheel, EventTimer *timer) {

	EventTimer **slot = &wheel->slots[event_wheel_deadline_tick (timer->deadline) % EVENT_WHEEL_SLOTS];

	if (timer->previous) timer->previous->next = timer->next;
	else *slot = timer->next;

	if (timer->next) timer->next->previous = timer->previous;

	timer->next = NULL;
	timer->previous = NULL;

	timer->armed = false;
	wheel->armed -= 1;

}

#pragma endregion

#pragma region timers

void event_wheel_init (EventWheel *wheel, const u64 now) {

	if (wheel) {
		(void) memset (wheel->slots, 0, sizeof (wheel->slots));

		wheel->tick = now / EVENT_WHEEL_TICK;
		wheel->armed = 0;
	}

}

void event_wheel_add (EventWheel *wheel, EventTimer *timer, const u64 deadline) {

	if (wheel && timer) {
		if (timer->armed) event_wheel_unlink (wheel, timer);

		// a deadline that already passed expires with the next tick,
		// it is kept as the start of that tick so its slot is the one that is searched to remove it
		const u64 next_tick = wheel->tick + 1;
		const u64 tick = event_wheel_deadline_tick (deadline);

		timer->deadline = (tick < next_tick) ? (next_tick * EVENT_WHEEL_TICK) : deadline;

		event_wheel_link (wheel, timer, (tick < next_tick) ? next_tick : tick);
	}

}

void event_wheel_remove (EventWheel *wheel, EventTimer *timer) {

	if (wheel && timer && timer->armed) {
		event_wheel_unlink (wheel, timer);
	}

}

#pragma endregion

#pragma region advance

// takes out of a slot the timers of the tick, the ones of a later turn stay
// returns them linked by next
static EventTimer *event_wheel_take_expired (EventWheel *wheel, const u64 tick) {

	EventTimer *expired = NULL;

	EventTimer *timer = wheel->slots[tick % EVENT_WHEEL_SLOTS];
	while (timer) {
		EventTimer *next = timer->next;

		if (event_wheel_deadline_tick (timer->deadline) <= tick) {
			event_wheel_unlink (wheel, timer);

			timer->next = expired;
			expired = timer;
		}

		timer = next;
	}

	return expired;

}

// the timers are out of the wheel before they are reported, so a callback can arm its timer again
static unsigned int event_wheel_report (
	EventTimer *expired, const EventTimerExpired callback, void *args
) {

	unsigned int count = 0;

	while (expired) {
		EventTimer *timer = expired;
		expired = timer->next;
		timer->next = NULL;

		if (callback) callback (timer, args);

		count += 1;
	}

	return count;

}

unsigned int event_wheel_advance (
	EventWheel *wheel, const u64 now, const EventTimerExpired expired, void *args
) {

	unsigned int count = 0;

	if (wheel) {
		const u64 current = now / EVENT_WHEEL_TICK;

		// after more than a turn every slot is visited once, with the current tick
		u64 tick = wheel->tick + 1;
		if ((current > wheel->tick) && ((current - wheel->tick) > EVENT_WHEEL_SLOTS)) {
			tick = current - EVENT_WHEEL_SLOTS + 1;
		}

		for (; tick <= current; tick++) {
			wheel->tick = tick;

			if (wheel->armed) {
				count += event_wheel_report (
					event_wheel_take_expired (wheel, tick), expired, args
				);
			}
		}
	}

	return count;

}

int event_wheel_next (const EventWheel *wheel, const u64 now) {

	int next = EVENT_WHEEL_NEXT_NONE;

	if (wheel && wheel->armed) {
		const u64 next_tick = (wheel->tick + 1) * EVENT_WHEEL_TICK;

		next = (next_tick > now) ? (int) (next_tick - now) : 0;
	}

	return next;

}

#pragma endregion
