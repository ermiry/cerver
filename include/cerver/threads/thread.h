/**
 * @file cerver/threads/thread.h
 * @brief The threads of cerver, their groups, and the allocated mutexes and conditions.
 */

#ifndef _CERVER_THREADS_H_
#define _CERVER_THREADS_H_

#include <time.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup threads Threads
 * @brief The threads, the thpool, the job queues and the workers of cerver.
 */

/**
 * @defgroup threads_thread Threads
 * @ingroup threads
 * @brief Creating and naming threads.
 *
 * Every thread created by cerver blocks SIGINT and SIGTERM (they are blocked while it is created and it inherits
 * the mask), so the signals of the process are delivered to the main thread.
 * @{
 */

/** @brief What pthread_create () returns on success. */
#define THREAD_OK						0

/** @brief The size of the buffer of the name of a thread; the kernel keeps 15 characters of it. */
#define THREAD_NAME_BUFFER_SIZE			64

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Creates a joinable thread, that must be joined with pthread_join ().
 *
 * @param[out] thread the id of the thread
 * @param[in] work the function of the thread
 * @param[in] args the argument of the function
 * @retval 0 the thread was created
 * @retval 1 a NULL thread or work, or it could not be created (it is logged)
 */
CERVER_PUBLIC unsigned int thread_create_joinable (
	pthread_t *thread, void *(*work) (void *), void *args
);

/**
 * @brief Creates a detached thread, that goes away on its own when its work returns.
 *
 * @param[out] thread the id of the thread
 * @param[in] work the function of the thread
 * @param[in] args the argument of the function
 * @retval 0 the thread was created
 * @retval 1 a NULL thread or work, or it could not be created (it is logged)
 */
CERVER_PUBLIC unsigned int thread_create_detachable (
	pthread_t *thread, void *(*work) (void *), void *args
);

/**
 * @brief Creates a detached thread and returns its id.
 *
 * @param[in] work the function of the thread
 * @param[in] args the argument of the function
 * @return the id of the thread, 0 for a NULL work or if it could not be created (it is logged)
 */
CERVER_PUBLIC pthread_t thread_create_detached (
	void *(*work) (void *), void *args
);

/**
 * @brief Sets the name of the calling thread, with a format as in printf () (prctl (PR_SET_NAME)).
 *
 * @param[in] name the format of the name, the kernel keeps its first 15 characters
 * @param[in] ... the arguments of the format
 * @retval 0 the name was set
 * @retval 1 a NULL name or prctl () failed
 */
CERVER_PUBLIC unsigned int thread_set_name (
	const char *name, ...
) CERVER_PRINTF_FORMAT (1, 2);

/** @} */

/**
 * @defgroup threads_group Thread groups
 * @ingroup threads
 * @brief The detached threads that use the data of an owner, so its teardown waits for them.
 * @{
 */

/** @brief thread_group_wait () waits until every thread of the group ends. */
#define THREAD_GROUP_WAIT_FOREVER		0

/**
 * @brief Counts the detached threads that use the data of an owner (a cerver or a client), so its teardown waits
 * for them before the data is released.
 */
typedef struct ThreadGroup {

	/** @brief The threads of the group that are alive. */
	unsigned int alive;

	/** @brief Protects alive. */
	pthread_mutex_t mutex;
	/** @brief Signaled each time a thread starts or ends. */
	pthread_cond_t ended;

} ThreadGroup;

/**
 * @brief Initializes a group without threads.
 *
 * @param[out] group the group, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void thread_group_init (ThreadGroup *group);

/**
 * @brief Destroys the mutex and the condition of a group, after its threads ended.
 *
 * @param[in,out] group the group, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void thread_group_end (ThreadGroup *group);

/**
 * @brief Creates a detached thread that is counted in the group until its work returns.
 *
 * It is counted before the thread exists, so a wait never misses it.
 *
 * @param[in,out] group the group
 * @param[in] work the function of the thread
 * @param[in] args the argument of the function
 * @retval 0 the thread was created
 * @retval 1 a NULL group or work, or the thread could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int thread_group_create (
	ThreadGroup *group, void *(*work) (void *), void *args
);

/**
 * @brief Returns the number of threads of the group that are alive.
 *
 * @param[in] group the group
 * @return the threads, 0 for a NULL group
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int thread_group_alive (ThreadGroup *group);

/**
 * @brief Waits until every thread of the group has ended, or until the timeout passes.
 *
 * @param[in,out] group the group
 * @param[in] timeout the seconds, #THREAD_GROUP_WAIT_FOREVER waits for them
 * @return the number of threads that are still alive, 0 for a NULL group
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int thread_group_wait (
	ThreadGroup *group, const time_t timeout
);

/** @} */

/**
 * @defgroup threads_mutex Mutex and cond
 * @ingroup threads
 * @brief The allocated mutexes and conditions of cerver.
 * @{
 */

/**
 * @brief Allocates and initializes a mutex.
 *
 * @return the new mutex, released with thread_mutex_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC pthread_mutex_t *thread_mutex_new (void);

/**
 * @brief Destroys and releases a mutex of thread_mutex_new ().
 *
 * @param[in] mutex the mutex, it can be NULL
 */
CERVER_PUBLIC void thread_mutex_delete (pthread_mutex_t *mutex);

/**
 * @brief Locks a mutex.
 *
 * @param[in,out] mutex the mutex, it can be NULL
 */
CERVER_PUBLIC void thread_mutex_lock (pthread_mutex_t *mutex);

/**
 * @brief Unlocks a mutex.
 *
 * @param[in,out] mutex the mutex, it can be NULL
 */
CERVER_PUBLIC void thread_mutex_unlock (pthread_mutex_t *mutex);

/**
 * @brief Allocates and initializes a condition.
 *
 * @return the new condition, released with thread_cond_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC pthread_cond_t *thread_cond_new (void);

/**
 * @brief Destroys and releases a condition of thread_cond_new ().
 *
 * @param[in] cond the condition, it can be NULL
 */
CERVER_PUBLIC void thread_cond_delete (pthread_cond_t *cond);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
