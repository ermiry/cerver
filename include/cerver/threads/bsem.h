/**
 * @file cerver/threads/bsem.h
 * @brief A binary semaphore with a mutex and a condition.
 */

#ifndef _THREADS_BSEM_H_
#define _THREADS_BSEM_H_

#include <pthread.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup threads_bsem Binary semaphore
 * @ingroup threads
 * @brief A semaphore that is 0 or 1: a post sets it to 1 and a wait takes it back to 0.
 * @{
 */

/**
 * @brief A binary semaphore.
 */
typedef struct bsem {

	/** @brief Protects the value, allocated by bsem_init (). */
	pthread_mutex_t *mutex;
	/** @brief Signaled when the value is posted, allocated by bsem_init (). */
	pthread_cond_t *cond;
	/** @brief The value, 0 or 1. */
	int v;

} bsem;

/**
 * @brief Allocates a binary semaphore without its mutex and its condition.
 *
 * @return the new semaphore, NULL if it could not be allocated
 * @note Call bsem_init () before using it.
 */
CERVER_PUBLIC bsem *bsem_new (void);

/**
 * @brief Releases a binary semaphore with its mutex and its condition.
 *
 * @param[in] bsem_ptr the semaphore, it can be NULL
 */
CERVER_PUBLIC void bsem_delete (void *bsem_ptr);

/**
 * @brief Allocates the mutex and the condition of a semaphore and sets its value.
 *
 * @param[in,out] bsem_p the semaphore
 * @param[in] value 0 or 1; any other value leaves the semaphore without its mutex and its condition
 * @note Without memory for one of them, the semaphore keeps both NULL and the post and the wait do nothing.
 */
CERVER_PUBLIC void bsem_init (bsem *bsem_p, int value);

/**
 * @brief Sets the value of a semaphore to 0, keeping its mutex and its condition for the threads that use them.
 *
 * @param[in,out] bsem_p the semaphore, it can be NULL or not initialized
 */
CERVER_PUBLIC void bsem_reset (bsem *bsem_p);

/**
 * @brief Sets the value to 1 and wakes up one thread that waits.
 *
 * @param[in,out] bsem_p the semaphore, initialized with bsem_init ()
 */
CERVER_PUBLIC void bsem_post (bsem *bsem_p);

/**
 * @brief Sets the value to 1 and wakes up every thread that waits; only one of them takes it.
 *
 * @param[in,out] bsem_p the semaphore, initialized with bsem_init ()
 */
CERVER_PUBLIC void bsem_post_all (bsem *bsem_p);

/**
 * @brief Waits until the value is 1 and sets it back to 0.
 *
 * @param[in,out] bsem_p the semaphore, initialized with bsem_init ()
 */
CERVER_PUBLIC void bsem_wait (bsem *bsem_p);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
