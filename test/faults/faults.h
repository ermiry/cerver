#ifndef _CERVER_TESTS_FAULTS_H_
#define _CERVER_TESTS_FAULTS_H_

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

// the functions that the faults binary defines in place of the ones of the system:
// the calls of libcerver.so resolve to them, and they fail only when a test arms them
typedef enum FaultsFunction {

	FAULTS_FUNCTION_MALLOC = 0,
	FAULTS_FUNCTION_CALLOC = 1,
	FAULTS_FUNCTION_REALLOC = 2,
	FAULTS_FUNCTION_THREAD = 3,

	FAULTS_FUNCTION_COUNT = 4

} FaultsFunction;

// an armed allocation fails with any size
#define FAULTS_ANY_SIZE					0

// the next call of the function fails, only once:
// in any thread, or only in the thread that armed it when this_thread is true,
// and for an allocation, only one of size bytes (FAULTS_ANY_SIZE for any)
extern void faults_arm (
	const FaultsFunction function, const bool this_thread, const size_t size
);

// the function fails once after some calls that match its arm were left to pass: the one that follows them.
// With the calls counted from zero, a test makes every allocation of a path fail, one in each run
extern void faults_arm_after (
	const FaultsFunction function, const bool this_thread, const size_t size, const unsigned int passed
);

// the function does not fail, even if it was armed and not called
extern void faults_disarm (const FaultsFunction function);

// the calls of the function that were made to fail
extern unsigned int faults_triggered (const FaultsFunction function);

// the most allocations that a watch keeps, the next ones are not watched
#define FAULTS_WATCH_CAPACITY			(1 << 12)

// the allocations of size bytes (FAULTS_ANY_SIZE for any) that this thread makes are watched
// until faults_watch_stop (), and their free () is seen in any thread, so a test finds what a failure path leaks
extern void faults_watch_start (const size_t size);

// no new allocation is watched, the ones already watched still see their free ()
extern void faults_watch_stop (void);

// the allocations that were watched
extern unsigned int faults_watch_allocated (void);

// the watched allocations that were not freed
extern unsigned int faults_watch_alive (void);

// the first watched allocation that was not freed, NULL without one: its size says what leaked
extern void *faults_watch_alive_first (void);

// clock_gettime () of CLOCK_REALTIME returns the time moved by the seconds (0 for the real one), like a jump
// of the clock of the system: a deadline that cerver takes from it moves with it, the waits of glibc do not
extern void faults_shift_realtime (const time_t seconds);

// a method that a sweep runs with one of its allocations failing; it checks what it leaves
typedef void (*FaultsSweepWork) (void *data);

// runs a method with each of its calls to malloc () failing, the first one in the first run, until a run
// has no call left to fail
// returns the runs with a failed allocation: the calls to malloc () of the method
extern unsigned int faults_sweep (FaultsSweepWork work, void *data);

#endif
