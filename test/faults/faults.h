#ifndef _CERVER_TESTS_FAULTS_H_
#define _CERVER_TESTS_FAULTS_H_

#include <stdbool.h>
#include <stddef.h>

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

// the function does not fail, even if it was armed and not called
extern void faults_disarm (const FaultsFunction function);

// the calls of the function that were made to fail
extern unsigned int faults_triggered (const FaultsFunction function);

// the allocations of size bytes that this thread makes are watched until faults_watch_stop (),
// and their free () is seen in any thread, so a test finds what a failure path leaks
extern void faults_watch_start (const size_t size);

// no new allocation is watched, the ones already watched still see their free ()
extern void faults_watch_stop (void);

// the allocations that were watched
extern unsigned int faults_watch_allocated (void);

// the watched allocations that were not freed
extern unsigned int faults_watch_alive (void);

#endif
