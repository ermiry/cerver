#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"
#include "cerver/collections/pool.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/core/internal.h"
#include "cerver/core/update.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/http/core/http.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region update

CerverUpdate *cerver_update_new (Cerver *cerver, void *args) {

	CerverUpdate *cu = (CerverUpdate *) malloc (sizeof (CerverUpdate));
	if (cu) {
		cu->cerver = cerver;
		cu->args = args;
	}

	return cu;

}

void cerver_update_delete (void *cerver_update_ptr) {

	if (cerver_update_ptr) free (cerver_update_ptr);

}

static const i64 CERVER_MICROSECONDS_PER_SECOND = 1000000;
static const i64 CERVER_NANOSECONDS_PER_MICROSECOND = 1000;

static i64 cerver_timespec_diff_microseconds (
	const struct timespec *start, const struct timespec *end
) {

	return ((i64) (end->tv_sec - start->tv_sec) * CERVER_MICROSECONDS_PER_SECOND)
		+ ((i64) (end->tv_nsec - start->tv_nsec) / CERVER_NANOSECONDS_PER_MICROSECOND);

}

// sleeps the microseconds or until the cerver is stopped, used to limit the fps of an update
void cerver_sleep_microseconds (Cerver *cerver, i64 microseconds) {

	const i64 nanoseconds_per_second = CERVER_MICROSECONDS_PER_SECOND * CERVER_NANOSECONDS_PER_MICROSECOND;

	struct timespec deadline = CERVER_ZERO_INITIALIZER;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);

	i64 nanoseconds = (i64) deadline.tv_nsec + (microseconds * CERVER_NANOSECONDS_PER_MICROSECOND);
	deadline.tv_sec += (time_t) (nanoseconds / nanoseconds_per_second);
	deadline.tv_nsec = (long) (nanoseconds % nanoseconds_per_second);

	(void) pthread_mutex_lock (cerver->stop_mutex);

	int result = 0;
	while (cerver_is_running (cerver) && (result != ETIMEDOUT)) {
		result = pthread_cond_timedwait (cerver->stop_cond, cerver->stop_mutex, &deadline);
	}

	(void) pthread_mutex_unlock (cerver->stop_mutex);

}

// called in a dedicated thread only if a user method was set
// executes methods every tick
void *cerver_update (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update () has started!",
			cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (cerver, cerver->update_args);

		const i64 time_per_frame = CERVER_MICROSECONDS_PER_SECOND / (cerver->update_ticks ? cerver->update_ticks : 1);
		i64 sleep_time = 0;

		struct timespec start = CERVER_ZERO_INITIALIZER, middle = CERVER_ZERO_INITIALIZER;

		while (cerver_is_running (cerver)) {
			(void) clock_gettime (CLOCK_MONOTONIC, &start);

			// do stuff
			if (cerver->update) cerver->update (cu);

			// limit the fps, the full timespec is used to cross every second
			(void) clock_gettime (CLOCK_MONOTONIC, &middle);
			sleep_time = time_per_frame - cerver_timespec_diff_microseconds (&start, &middle);
			if (sleep_time > 0) {
				// the stop wakes it up, it does not wait for the end of the frame
				cerver_sleep_microseconds (cerver, sleep_time);
			}
		}

		cerver_update_delete (cu);

		if (cerver->update_args) {
			if (cerver->delete_update_args) {
				cerver->delete_update_args (cerver->update_args);
			}
		}

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update () has ended!",
			cerver->info->name
		);
		#endif
	}

	return NULL;

}

// called in a dedicated thread only if a user method was set
// executes methods every x seconds
void *cerver_update_interval (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update_interval () has started!",
			cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (
			cerver, cerver->update_interval_args
		);

		while (cerver_is_running (cerver)) {
			if (cerver->update_interval) cerver->update_interval (cu);

			cerver_sleep (cerver, cerver->update_interval_secs);
		}

		cerver_update_delete (cu);

		if (cerver->update_interval_args) {
			if (cerver->delete_update_interval_args) {
				cerver->delete_update_interval_args (
					cerver->update_interval_args
				);
			}
		}

		#ifdef CERVER_DEBUG
		cerver_log_success (
			"Cerver's %s cerver_update_interval () has ended!",
			cerver->info->name
		);
		#endif
	}

	return NULL;

}

#pragma endregion
