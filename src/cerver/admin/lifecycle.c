#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>
#include <sys/poll.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"
#include "cerver/admin/internal.h"
#include "cerver/admin/lifecycle.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/core/update.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region start

static const i64 ADMIN_MICROSECONDS_PER_SECOND = 1000000;
static const i64 ADMIN_NANOSECONDS_PER_MICROSECOND = 1000;

static i64 admin_timespec_diff_microseconds (
	const struct timespec *start, const struct timespec *end
) {

	return ((i64) (end->tv_sec - start->tv_sec) * ADMIN_MICROSECONDS_PER_SECOND)
		+ ((i64) (end->tv_nsec - start->tv_nsec) / ADMIN_NANOSECONDS_PER_MICROSECOND);

}

// called in a dedicated thread only if a user method was set
// executes methods every tick
static void *admin_cerver_update (void *args) {

	if (args) {
		AdminCerver *admin_cerver = (AdminCerver *) args;

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update () has started!",
			admin_cerver->cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (admin_cerver->cerver, admin_cerver->update_args);

		const i64 time_per_frame = ADMIN_MICROSECONDS_PER_SECOND / (admin_cerver->update_ticks ? admin_cerver->update_ticks : 1);
		i64 sleep_time = 0;

		struct timespec start = CERVER_ZERO_INITIALIZER, middle = CERVER_ZERO_INITIALIZER;

		while (cerver_is_running (admin_cerver->cerver)) {
			(void) clock_gettime (CLOCK_MONOTONIC, &start);

			// do stuff
			if (admin_cerver->update) admin_cerver->update (cu);

			// limit the fps, the full timespec is used to cross every second
			(void) clock_gettime (CLOCK_MONOTONIC, &middle);
			sleep_time = time_per_frame - admin_timespec_diff_microseconds (&start, &middle);
			if (sleep_time > 0) {
				// the stop wakes it up, it does not wait for the end of the frame
				cerver_sleep_microseconds (admin_cerver->cerver, sleep_time);
			}
		}

		cerver_update_delete (cu);

		if (admin_cerver->update_args) {
			if (admin_cerver->delete_update_args) {
				admin_cerver->delete_update_args (admin_cerver->update_args);
			}
		}

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update () has ended!",
			admin_cerver->cerver->info->name
		);
		#endif
	}

	return NULL;

}

// called in a dedicated thread only if a user method was set
// executes methods every x seconds
static void *admin_cerver_update_interval (void *args) {

	if (args) {
		AdminCerver *admin_cerver = (AdminCerver *) args;

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update_interval () has started!",
			admin_cerver->cerver->info->name
		);
		#endif

		CerverUpdate *cu = cerver_update_new (
			admin_cerver->cerver, admin_cerver->update_interval_args
		);

		while (cerver_is_running (admin_cerver->cerver)) {
			if (admin_cerver->update_interval)
				admin_cerver->update_interval (cu);

			// the teardown waits for this thread, it wakes up when the cerver stops
			cerver_sleep (admin_cerver->cerver, admin_cerver->update_interval_secs);
		}

		cerver_update_delete (cu);

		if (admin_cerver->update_interval_args) {
			if (admin_cerver->delete_update_interval_args) {
				admin_cerver->delete_update_interval_args (
					admin_cerver->update_interval_args
				);
			}
		}

		#ifdef ADMIN_DEBUG
		cerver_log_success (
			"Cerver's %s admin_cerver_update_interval () has ended!",
			admin_cerver->cerver->info->name
		);
		#endif
	}

	return NULL;

}

// inits admin cerver's internal structures & values
static u8 admin_cerver_start_internal (AdminCerver *admin_cerver) {

	u8 retval = 1;

	if (admin_cerver) {
		admin_cerver->fds = (struct pollfd *) calloc (
			admin_cerver->max_n_fds, sizeof (struct pollfd)
		);

		if (admin_cerver->fds) {
			(void) memset (
				admin_cerver->fds, 0, sizeof (struct pollfd) * admin_cerver->max_n_fds
			);

			for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
				admin_cerver->fds[i].fd = -1;

			admin_cerver->current_n_fds = 0;

			admin_cerver->poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			(void) pthread_mutex_init (admin_cerver->poll_lock, NULL);

			retval = 0;
		}
	}

	return retval;

}

static u8 admin_cerver_app_handler_start (AdminCerver *admin_cerver) {

	u8 retval = 0;

	if (admin_cerver) {
		if (admin_cerver->app_packet_handler) {
			if (!admin_cerver->app_packet_handler->direct_handle) {
				if (!handler_start (admin_cerver->app_packet_handler)) {
					#ifdef ADMIN_DEBUG
					cerver_log_success (
						"Admin cerver %s app_packet_handler has started!",
						admin_cerver->cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start ADMIN cerver %s app_packet_handler!",
						admin_cerver->cerver->info->name
					);
				}
			}
		}

		else {
			#ifdef ADMIN_DEBUG
			cerver_log_warning (
				"Admin cerver %s does not have an app_packet_handler",
				admin_cerver->cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

static u8 admin_cerver_app_error_handler_start (AdminCerver *admin_cerver) {

	u8 retval = 0;

	if (admin_cerver) {
		if (admin_cerver->app_error_packet_handler) {
			if (!admin_cerver->app_error_packet_handler->direct_handle) {
				if (!handler_start (admin_cerver->app_error_packet_handler)) {
					#ifdef ADMIN_DEBUG
					cerver_log_success (
						"Admin cerver %s app_error_packet_handler has started!",
						admin_cerver->cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start ADMIN cerver %s app_error_packet_handler!",
						admin_cerver->cerver->info->name
					);
				}
			}
		}

		else {
			#ifdef ADMIN_DEBUG
			cerver_log_warning (
				"Admin cerver %s does not have an app_error_packet_handler",
				admin_cerver->cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

static u8 admin_cerver_custom_handler_start (AdminCerver *admin_cerver) {

	u8 retval = 0;

	if (admin_cerver) {
		if (admin_cerver->custom_packet_handler) {
			if (!admin_cerver->custom_packet_handler->direct_handle) {
				if (!handler_start (admin_cerver->custom_packet_handler)) {
					#ifdef ADMIN_DEBUG
					cerver_log_success (
						"Admin cerver %s custom_packet_handler has started!",
						admin_cerver->cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start ADMIN cerver %s custom_packet_handler!",
						admin_cerver->cerver->info->name
					);
				}
			}
		}

		else {
			// #ifdef ADMIN_DEBUG
			// cerver_log_warning (
			//     "Cerver %s does not have a custom_packet_handler",
			// 	admin_cerver->cerver->info->name
			// );
			// #endif
		}
	}

	return retval;

}

static u8 admin_cerver_handlers_start (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		#ifdef ADMIN_DEBUG
		cerver_log_debug (
			"Initializing cerver %s admin handlers...",
			admin_cerver->cerver->info->name
		);
		#endif

		admin_cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		(void) pthread_mutex_init (admin_cerver->handlers_lock, NULL);

		errors |= admin_cerver_app_handler_start (admin_cerver);

		errors |= admin_cerver_app_error_handler_start (admin_cerver);

		errors |= admin_cerver_custom_handler_start (admin_cerver);

		if (!errors) {
			#ifdef ADMIN_DEBUG
			cerver_log_debug (
				"Done initializing cerver %s admin handlers!",
				admin_cerver->cerver->info->name
			);
			#endif
		}
	}

	return errors;

}

static u8 admin_cerver_start_poll (Cerver *cerver) {

	u8 retval = 1;

	// joined by admin_cerver_end () before anything it uses is released
	if (cerver) {
		if (!thread_create_joinable (
			&cerver->admin_thread_id,
			admin_poll,
			cerver
		)) {
			retval = 0;		// success
		}

		else {
			cerver_log_error (
				"Failed to create admin_poll () thread in cerver %s!",
				cerver->info->name
			);
		}
	}

	return retval;

}

u8 admin_cerver_start (AdminCerver *admin_cerver) {

	u8 retval = 1;

	// the update threads are joined by admin_cerver_end ()
	if (admin_cerver) {
		if (!admin_cerver_start_internal (admin_cerver)) {
			if (admin_cerver->update) {
				if (thread_create_joinable (
					&admin_cerver->update_thread_id,
					admin_cerver_update,
					admin_cerver
				)) {
					cerver_log_error (
						"Failed to create cerver %s ADMIN UPDATE thread!",
						admin_cerver->cerver->info->name
					);
				}
			}

			if (admin_cerver->update_interval) {
				if (thread_create_joinable (
					&admin_cerver->update_interval_thread_id,
					admin_cerver_update_interval,
					admin_cerver
				)) {
					cerver_log_error (
						"Failed to create cerver %s ADMIN UPDATE INTERVAL thread!",
						admin_cerver->cerver->info->name
					);
				}
			}

			if (!admin_cerver_handlers_start (admin_cerver)) {
				if (!admin_cerver_start_poll (admin_cerver->cerver)) {
					retval = 0;
				}
			}

			else {
				cerver_log_error (
					"admin_cerver_start () - failed to start cerver %s admin handlers!",
					admin_cerver->cerver->info->name
				);
			}
		}

		else {
			cerver_log_error (
				"admin_cerver_start () - failed to start cerver %s admin internal!",
				admin_cerver->cerver->info->name
			);
		}
	}

	return retval;

}

#pragma endregion

#pragma region end

// correctly destroy admin cerver's custom handlers,
// each thread is woken and joined, a handler that never started is skipped
static u8 admin_cerver_handlers_end (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		#ifdef ADMIN_DEBUG
		cerver_log_debug (
			"Stopping handlers in cerver %s admin...",
			admin_cerver->cerver->info->name
		);
		#endif

		handler_stop (admin_cerver->app_packet_handler);

		handler_stop (admin_cerver->app_error_packet_handler);

		handler_stop (admin_cerver->custom_packet_handler);
	}

	return 0;

}

static u8 admin_cerver_disconnect_admins (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		if (dlist_size (admin_cerver->admins)) {
			// send a cerver teardown packet to all clients connected to cerver
			Packet *packet = packet_generate_request (PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_TEARDOWN, NULL, 0);
			if (packet) {
				errors |= admin_cerver_broadcast_to_admins (admin_cerver, packet);
				packet_delete (packet);
			}
		}
	}

	return errors;

}

static void admin_cerver_thread_join (pthread_t *thread_id) {

	if (*thread_id) {
		(void) pthread_join (*thread_id, NULL);
		*thread_id = 0;
	}

}

u8 admin_cerver_end (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		#ifdef ADMIN_DEBUG
		cerver_log_debug (
			"Staring cerver %s admin teardown...",
			admin_cerver->cerver->info->name
		);
		#endif

		// the poll and the update threads end as soon as the cerver is stopped
		admin_cerver_thread_join (&admin_cerver->cerver->admin_thread_id);
		admin_cerver_thread_join (&admin_cerver->update_thread_id);
		admin_cerver_thread_join (&admin_cerver->update_interval_thread_id);

		errors |= admin_cerver_handlers_end (admin_cerver);

		errors |= admin_cerver_disconnect_admins (admin_cerver);

		cerver_log_success (
			"Cerver %s admin teardown was successful!",
			admin_cerver->cerver->info->name
		);
	}

	return errors;

}

#pragma endregion
