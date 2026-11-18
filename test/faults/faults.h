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
	FAULTS_FUNCTION_SYSCALL = 4,

	FAULTS_FUNCTION_COUNT = 5

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

// the next call of syscall () with a number fails once with an errno, after some calls with that number were
// left to pass: cerver makes the system calls of io_uring with syscall (), no other one
extern void faults_arm_syscall (const long number, const int error, const unsigned int passed);

// the function does not fail, even if it was armed and not called
extern void faults_disarm (const FaultsFunction function);

// the calls of the function that were made to fail
extern unsigned int faults_triggered (const FaultsFunction function);

// a method that a sweep runs with one of its allocations failing; it checks what it leaves
typedef void (*FaultsSweepWork) (void *data);

// runs a method with each of its calls to malloc () failing, the first one in the first run, until a run
// has no call left to fail
// returns the runs with a failed allocation: the calls to malloc () of the method
extern unsigned int faults_sweep (FaultsSweepWork work, void *data);

#endif
