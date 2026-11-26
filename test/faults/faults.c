#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <dlfcn.h>
#include <pthread.h>

#include "faults.h"

// the allocators of glibc, exported for the programs that replace malloc ()
#ifdef __cplusplus
extern "C" {
#endif

extern void *__libc_malloc (size_t size);
extern void *__libc_calloc (size_t count, size_t size);
extern void *__libc_realloc (void *pointer, size_t size);
extern void __libc_free (void *pointer);

#ifdef __cplusplus
}
#endif

// the definitions match the declarations of glibc, that do not throw in C++
#ifdef __cplusplus
#define FAULTS_NO_THROW					noexcept (true)
#define FAULTS_LINKAGE					extern "C"
#else
#define FAULTS_NO_THROW
#define FAULTS_LINKAGE
#endif

typedef int (*FaultsThreadCreate) (
	pthread_t *thread, const pthread_attr_t *attributes,
	void *(*work) (void *), void *arguments
);

typedef int (*FaultsClockGettime) (clockid_t clock, struct timespec *time);

// what clock_gettime () returns when it fails
#define FAULTS_CLOCK_FAILED				(-1)

// the seconds that CLOCK_REALTIME is moved, read and written atomically
static time_t faults_realtime_shift = 0;

typedef struct FaultsArm {

	// read and written atomically: armed is taken by the call that fails
	bool armed;
	unsigned int triggered;

	bool this_thread;
	pthread_t thread;
	size_t size;

	// the calls that match and are left to pass before the one that fails
	unsigned int passed;

} FaultsArm;

static FaultsArm faults_arms[FAULTS_FUNCTION_COUNT];

void faults_arm_after (
	const FaultsFunction function, const bool this_thread, const size_t size, const unsigned int passed
) {

	FaultsArm *arm = &faults_arms[function];

	arm->this_thread = this_thread;
	arm->thread = pthread_self ();
	arm->size = size;

	__atomic_store_n (&arm->passed, passed, __ATOMIC_RELEASE);
	__atomic_store_n (&arm->armed, true, __ATOMIC_RELEASE);

}

void faults_arm (
	const FaultsFunction function, const bool this_thread, const size_t size
) {

	faults_arm_after (function, this_thread, size, 0);

}

void faults_disarm (const FaultsFunction function) {

	__atomic_store_n (&faults_arms[function].armed, false, __ATOMIC_RELEASE);

}

unsigned int faults_triggered (const FaultsFunction function) {

	return __atomic_load_n (&faults_arms[function].triggered, __ATOMIC_ACQUIRE);

}

// a call matches its arm in its thread and its size
static bool faults_matches (const FaultsArm *arm, const size_t size) {

	const bool thread = !arm->this_thread || pthread_equal (arm->thread, pthread_self ());
	const bool sized = (arm->size == FAULTS_ANY_SIZE) || (arm->size == size);

	return thread && sized;

}

// a call that matches its arm is left to pass while the arm still has calls to pass
static bool faults_passes (FaultsArm *arm) {

	unsigned int passed = __atomic_load_n (&arm->passed, __ATOMIC_ACQUIRE);

	bool passes = false;
	while (passed && !passes) {
		passes = __atomic_compare_exchange_n (
			&arm->passed, &passed, passed - 1, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
		);
	}

	return passes;

}

// the call fails when its function is armed and it matches: the first one that takes the arm,
// after the calls that the arm leaves to pass
static bool faults_fails (const FaultsFunction function, const size_t size) {

	FaultsArm *arm = &faults_arms[function];

	bool armed = __atomic_load_n (&arm->armed, __ATOMIC_ACQUIRE);
	const bool fails = armed && faults_matches (arm, size) && !faults_passes (arm)
		&& __atomic_compare_exchange_n (&arm->armed, &armed, false, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

	if (fails) (void) __atomic_add_fetch (&arm->triggered, 1, __ATOMIC_ACQ_REL);

	return fails;

}

unsigned int faults_sweep (FaultsSweepWork work, void *data) {

	unsigned int runs = 0;

	bool failed = true;
	while (failed) {
		const unsigned int triggered = faults_triggered (FAULTS_FUNCTION_MALLOC);

		faults_arm_after (FAULTS_FUNCTION_MALLOC, true, FAULTS_ANY_SIZE, runs);
		work (data);
		faults_disarm (FAULTS_FUNCTION_MALLOC);

		failed = faults_triggered (FAULTS_FUNCTION_MALLOC) != triggered;
		if (failed) runs += 1;
	}

	return runs;

}

typedef struct FaultsWatch {

	// read and written atomically: free () reads the pointers in any thread
	bool watching;
	unsigned int allocated;
	void *pointers[FAULTS_WATCH_CAPACITY];

	pthread_t thread;
	size_t size;

} FaultsWatch;

static FaultsWatch faults_watch;

void faults_watch_start (const size_t size) {

	for (unsigned int i = 0; i < FAULTS_WATCH_CAPACITY; i++) {
		__atomic_store_n (&faults_watch.pointers[i], NULL, __ATOMIC_RELEASE);
	}

	__atomic_store_n (&faults_watch.allocated, 0, __ATOMIC_RELEASE);

	faults_watch.thread = pthread_self ();
	faults_watch.size = size;

	__atomic_store_n (&faults_watch.watching, true, __ATOMIC_RELEASE);

}

void faults_watch_stop (void) {

	__atomic_store_n (&faults_watch.watching, false, __ATOMIC_RELEASE);

}

unsigned int faults_watch_allocated (void) {

	return __atomic_load_n (&faults_watch.allocated, __ATOMIC_ACQUIRE);

}

unsigned int faults_watch_alive (void) {

	unsigned int alive = 0;
	for (unsigned int i = 0; i < FAULTS_WATCH_CAPACITY; i++) {
		if (__atomic_load_n (&faults_watch.pointers[i], __ATOMIC_ACQUIRE)) alive += 1;
	}

	return alive;

}

void *faults_watch_alive_first (void) {

	void *first = NULL;
	for (unsigned int i = 0; (i < FAULTS_WATCH_CAPACITY) && !first; i++) {
		first = __atomic_load_n (&faults_watch.pointers[i], __ATOMIC_ACQUIRE);
	}

	return first;

}

// the watched size, or any size with FAULTS_ANY_SIZE
static bool faults_watch_sized (const size_t size) {

	return (faults_watch.size == FAULTS_ANY_SIZE) || (size == faults_watch.size);

}

// an allocation of the watched size in the watched thread is kept
static void faults_watch_add (void *pointer, const size_t size) {

	const bool watched = pointer && __atomic_load_n (&faults_watch.watching, __ATOMIC_ACQUIRE)
		&& faults_watch_sized (size) && pthread_equal (faults_watch.thread, pthread_self ());

	if (!watched) return;

	const unsigned int index = __atomic_fetch_add (&faults_watch.allocated, 1, __ATOMIC_ACQ_REL);
	if (index < FAULTS_WATCH_CAPACITY) __atomic_store_n (&faults_watch.pointers[index], pointer, __ATOMIC_RELEASE);

}

// a watched pointer that is freed is forgotten
static void faults_watch_remove (const void *pointer) {

	if (!pointer) return;

	const unsigned int allocated = __atomic_load_n (&faults_watch.allocated, __ATOMIC_ACQUIRE);
	const unsigned int count = (allocated < FAULTS_WATCH_CAPACITY) ? allocated : FAULTS_WATCH_CAPACITY;

	for (unsigned int i = 0; i < count; i++) {
		void *expected = (void *) pointer;
		const bool found = (__atomic_load_n (&faults_watch.pointers[i], __ATOMIC_ACQUIRE) == pointer)
			&& __atomic_compare_exchange_n (
				&faults_watch.pointers[i], &expected, NULL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
			);

		if (found) break;
	}

}

FAULTS_LINKAGE void *malloc (size_t size) FAULTS_NO_THROW {

	void *pointer = NULL;

	if (faults_fails (FAULTS_FUNCTION_MALLOC, size)) errno = ENOMEM;
	else pointer = __libc_malloc (size);

	faults_watch_add (pointer, size);

	return pointer;

}

FAULTS_LINKAGE void *calloc (size_t count, size_t size) FAULTS_NO_THROW {

	void *pointer = NULL;

	if (faults_fails (FAULTS_FUNCTION_CALLOC, count * size)) errno = ENOMEM;
	else pointer = __libc_calloc (count, size);

	faults_watch_add (pointer, count * size);

	return pointer;

}

FAULTS_LINKAGE void *realloc (void *pointer, size_t size) FAULTS_NO_THROW {

	void *resized = NULL;

	const bool failed = faults_fails (FAULTS_FUNCTION_REALLOC, size);
	if (failed) errno = ENOMEM;
	else resized = __libc_realloc (pointer, size);

	// the old pointer is released when the resize succeeded, or by a resize to 0 bytes in glibc
	if (!failed && (resized || !size)) faults_watch_remove (pointer);

	faults_watch_add (resized, size);

	return resized;

}

FAULTS_LINKAGE void free (void *pointer) FAULTS_NO_THROW {

	faults_watch_remove (pointer);

	__libc_free (pointer);

}

// the pthread_create () of the system, the one after this program
static FaultsThreadCreate faults_thread_create_next (void) {

	static FaultsThreadCreate next = NULL;

	FaultsThreadCreate found = __atomic_load_n (&next, __ATOMIC_ACQUIRE);
	if (!found) {
		// a function pointer can not be converted from a void * in ISO C, its bytes are copied
		void *symbol = dlsym (RTLD_NEXT, "pthread_create");
		(void) memcpy (&found, &symbol, sizeof (FaultsThreadCreate));
		__atomic_store_n (&next, found, __ATOMIC_RELEASE);
	}

	return found;

}

FAULTS_LINKAGE int pthread_create (
	pthread_t *thread, const pthread_attr_t *attributes,
	void *(*work) (void *), void *arguments
) FAULTS_NO_THROW {

	int result = EAGAIN;

	if (!faults_fails (FAULTS_FUNCTION_THREAD, FAULTS_ANY_SIZE)) {
		FaultsThreadCreate next = faults_thread_create_next ();
		result = next ? next (thread, attributes, work, arguments) : EAGAIN;
	}

	return result;

}

void faults_shift_realtime (const time_t seconds) {

	__atomic_store_n (&faults_realtime_shift, seconds, __ATOMIC_RELEASE);

}

// the clock_gettime () of the system, the one after this program
static FaultsClockGettime faults_clock_gettime_next (void) {

	static FaultsClockGettime next = NULL;

	FaultsClockGettime found = __atomic_load_n (&next, __ATOMIC_ACQUIRE);
	if (!found) {
		void *symbol = dlsym (RTLD_NEXT, "clock_gettime");
		(void) memcpy (&found, &symbol, sizeof (FaultsClockGettime));
		__atomic_store_n (&next, found, __ATOMIC_RELEASE);
	}

	return found;

}

FAULTS_LINKAGE int clock_gettime (clockid_t clock, struct timespec *time) FAULTS_NO_THROW {

	FaultsClockGettime next = faults_clock_gettime_next ();

	const int result = next ? next (clock, time) : FAULTS_CLOCK_FAILED;
	if (!result && (clock == CLOCK_REALTIME)) {
		time->tv_sec += __atomic_load_n (&faults_realtime_shift, __ATOMIC_ACQUIRE);
	}

	return result;

}
