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

typedef struct FaultsArm {

	// read and written atomically: armed is taken by the call that fails
	bool armed;
	unsigned int triggered;

	bool this_thread;
	pthread_t thread;
	size_t size;

} FaultsArm;

static FaultsArm faults_arms[FAULTS_FUNCTION_COUNT];

void faults_arm (
	const FaultsFunction function, const bool this_thread, const size_t size
) {

	FaultsArm *arm = &faults_arms[function];

	arm->this_thread = this_thread;
	arm->thread = pthread_self ();
	arm->size = size;

	__atomic_store_n (&arm->armed, true, __ATOMIC_RELEASE);

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

// the call fails when its function is armed and it matches: the first one that takes the arm
static bool faults_fails (const FaultsFunction function, const size_t size) {

	FaultsArm *arm = &faults_arms[function];

	bool armed = __atomic_load_n (&arm->armed, __ATOMIC_ACQUIRE);
	const bool fails = armed && faults_matches (arm, size)
		&& __atomic_compare_exchange_n (&arm->armed, &armed, false, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);

	if (fails) (void) __atomic_add_fetch (&arm->triggered, 1, __ATOMIC_ACQ_REL);

	return fails;

}

// the most allocations that a watch keeps, the next ones are not watched
#define FAULTS_WATCH_MAX				(1 << 6)

typedef struct FaultsWatch {

	// read and written atomically: free () reads the pointers in any thread
	bool watching;
	unsigned int allocated;
	void *pointers[FAULTS_WATCH_MAX];

	pthread_t thread;
	size_t size;

} FaultsWatch;

static FaultsWatch faults_watch;

void faults_watch_start (const size_t size) {

	for (unsigned int i = 0; i < FAULTS_WATCH_MAX; i++) {
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
	for (unsigned int i = 0; i < FAULTS_WATCH_MAX; i++) {
		if (__atomic_load_n (&faults_watch.pointers[i], __ATOMIC_ACQUIRE)) alive += 1;
	}

	return alive;

}

// an allocation of the watched size in the watched thread is kept
static void faults_watch_add (void *pointer, const size_t size) {

	const bool watched = pointer && __atomic_load_n (&faults_watch.watching, __ATOMIC_ACQUIRE)
		&& (size == faults_watch.size) && pthread_equal (faults_watch.thread, pthread_self ());

	if (!watched) return;

	const unsigned int index = __atomic_fetch_add (&faults_watch.allocated, 1, __ATOMIC_ACQ_REL);
	if (index < FAULTS_WATCH_MAX) __atomic_store_n (&faults_watch.pointers[index], pointer, __ATOMIC_RELEASE);

}

// a watched pointer that is freed is forgotten
static void faults_watch_remove (const void *pointer) {

	if (!pointer) return;

	const unsigned int allocated = __atomic_load_n (&faults_watch.allocated, __ATOMIC_ACQUIRE);
	const unsigned int count = (allocated < FAULTS_WATCH_MAX) ? allocated : FAULTS_WATCH_MAX;

	for (unsigned int i = 0; i < count; i++) {
		void *expected = (void *) pointer;
		if (__atomic_compare_exchange_n (
			&faults_watch.pointers[i], &expected, NULL, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE
		)) break;
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
