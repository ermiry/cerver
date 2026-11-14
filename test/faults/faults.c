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

FAULTS_LINKAGE void *malloc (size_t size) FAULTS_NO_THROW {

	void *pointer = NULL;

	if (faults_fails (FAULTS_FUNCTION_MALLOC, size)) errno = ENOMEM;
	else pointer = __libc_malloc (size);

	return pointer;

}

FAULTS_LINKAGE void *calloc (size_t count, size_t size) FAULTS_NO_THROW {

	void *pointer = NULL;

	if (faults_fails (FAULTS_FUNCTION_CALLOC, count * size)) errno = ENOMEM;
	else pointer = __libc_calloc (count, size);

	return pointer;

}

FAULTS_LINKAGE void *realloc (void *pointer, size_t size) FAULTS_NO_THROW {

	void *resized = NULL;

	if (faults_fails (FAULTS_FUNCTION_REALLOC, size)) errno = ENOMEM;
	else resized = __libc_realloc (pointer, size);

	return resized;

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
