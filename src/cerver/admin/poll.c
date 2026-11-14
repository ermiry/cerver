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
#include "cerver/admin/poll.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region poll

// get a free index in the admin cerver poll fds array
static i32 admin_cerver_poll_get_free_idx (
	AdminCerver *admin_cerver
) {

	if (admin_cerver) {
		for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
			if (admin_cerver->fds[i].fd == -1) return i;

	}

	return -1;

}

// get the idx of the connection sock fd in the admin cerver poll fds
static i32 admin_cerver_poll_get_idx_with_sock_fd (
	AdminCerver *admin_cerver, i32 sock_fd
) {

	if (admin_cerver) {
		for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
			if (admin_cerver->fds[i].fd == sock_fd) return i;
	}

	return -1;

}

// regsiters a client connection to the cerver's admin poll array
// returns 0 on success, 1 on error
u8 admin_cerver_poll_register_connection (
	AdminCerver *admin_cerver, Connection *connection
) {

	u8 retval = 1;

	if (admin_cerver && connection) {
		(void) pthread_mutex_lock (admin_cerver->poll_lock);

		i32 idx = admin_cerver_poll_get_free_idx (admin_cerver);
		if (idx >= 0) {
			admin_cerver->fds[idx].fd = connection->socket->sock_fd;
			admin_cerver->fds[idx].events = POLLIN;
			admin_cerver->current_n_fds++;

			admin_cerver->stats->current_connections++;
			admin_cerver->stats->total_admin_connections++;

			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Added sock fd <%d> to cerver %s ADMIN poll, idx: %i",
				connection->socket->sock_fd,
				admin_cerver->cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connections: %lu",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connections
			);
			#endif

			retval = 0;
		}

		else if (idx < 0) {
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN poll is full!",
				admin_cerver->cerver->info->name
			);
			#endif
		}

		(void) pthread_mutex_unlock (admin_cerver->poll_lock);
	}

	return retval;

}

// unregsiters a sock fd connection from the cerver's admin poll array
// returns 0 on success, 1 on error
u8 admin_cerver_poll_unregister_sock_fd (
	AdminCerver *admin_cerver, const i32 sock_fd
) {

	u8 retval = 1;

	if (admin_cerver) {
		(void) pthread_mutex_lock (admin_cerver->poll_lock);

		i32 idx = admin_cerver_poll_get_idx_with_sock_fd (admin_cerver, sock_fd);
		if (idx >= 0) {
			admin_cerver->fds[idx].fd = -1;
			admin_cerver->fds[idx].events = -1;
			admin_cerver->current_n_fds--;

			admin_cerver->stats->current_connections--;

			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Removed sock fd <%d> from cerver %s ADMIN poll, idx: %d",
				sock_fd, admin_cerver->cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connections: %lu",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connections
			);
			#endif

			retval = 0;
		}

		else {
			// #ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_ADMIN,
				"Sock fd <%d> was NOT found in cerver %s ADMIN poll!",
				sock_fd, admin_cerver->cerver->info->name
			);
			// #endif
		}

		(void) pthread_mutex_unlock (admin_cerver->poll_lock);
	}

	return retval;

}

// unregsiters a connection from the cerver's admin hold poll array
// returns 0 on success, 1 on error
u8 admin_cerver_poll_unregister_connection (
	AdminCerver *admin_cerver, Connection *connection
) {

	return (admin_cerver && connection) ?
		admin_cerver_poll_unregister_sock_fd (
			admin_cerver, connection->socket->sock_fd
		) : 1;

}

// the poll thread polls its own copy of the fds,
// because the on hold poll registers the admins while it polls
static void admin_poll_copy_fds (
	AdminCerver *admin_cerver, struct pollfd *polled_fds
) {

	(void) pthread_mutex_lock (admin_cerver->poll_lock);

	(void) memcpy (
		polled_fds, admin_cerver->fds,
		sizeof (struct pollfd) * admin_cerver->max_n_fds
	);

	(void) pthread_mutex_unlock (admin_cerver->poll_lock);

}

static inline void admin_poll_handle (
	AdminCerver *admin_cerver, char *packet_buffer,
	const struct pollfd *polled_fds
) {

	// one or more fd(s) are readable, need to determine which ones they are
	for (u32 idx = 0; idx < admin_cerver->max_n_fds; idx++) {
		if ((polled_fds[idx].fd > -1) && polled_fds[idx].revents) {
			CerverReceive *cr = cerver_receive_create (
				RECEIVE_TYPE_ADMIN, admin_cerver->cerver, polled_fds[idx].fd
			);

			if (cr) {
				switch (polled_fds[idx].revents) {
					case POLLIN: {
						cerver_receive_internal (
							cr,
							packet_buffer,
							admin_cerver->receive_buffer_size
						);
					} break;

					default: {
						if (polled_fds[idx].revents != 0) {
							cerver_receive_handle_failed (cr);
						}
					} break;
				}

				cerver_receive_delete (cr);
			}
		}
	}

}

// handles packets from the admin clients
void *admin_poll (void *cerver_ptr) {

	if (cerver_ptr) {
		Cerver *cerver = (Cerver *) cerver_ptr;
		AdminCerver *admin_cerver = cerver->admin;

		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_ADMIN,
			"Cerver %s ADMIN poll has started!",
			cerver->info->name
		);

		(void) thread_set_name ("%s-admin", cerver->info->alias);

		char *packet_buffer = (char *) calloc (
			admin_cerver->receive_buffer_size, sizeof (char)
		);

		struct pollfd *polled_fds = (struct pollfd *) calloc (
			admin_cerver->max_n_fds, sizeof (struct pollfd)
		);

		if (packet_buffer && polled_fds) {
			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Waiting for admin connections..."
			);
			#endif

			int poll_retval = 0;
			while (cerver_is_running (cerver)) {
				admin_poll_copy_fds (admin_cerver, polled_fds);

				poll_retval = poll (
					polled_fds,
					admin_cerver->max_n_fds,
					admin_cerver->poll_timeout
				);

				switch (poll_retval) {
					case -1: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_ADMIN,
							"Cerver %s ADMIN poll has failed!",
							cerver->info->name
						);

						perror ("Error");
						cerver_stop_request (cerver);
					} break;

					case 0: {
						// #ifdef ADMIN_DEBUG
						// cerver_log (
						//     LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
						//     "Cerver %s ADMIN poll timeout",
						// 	cerver->info->name
						// );
						// #endif
					} break;

					default: {
						admin_poll_handle (
							admin_cerver, packet_buffer, polled_fds
						);
					} break;
				}
			}

			#ifdef ADMIN_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN poll has stopped!", cerver->info->name
			);
			#endif
		}

		else {
			cerver_log_error (
				"Failed to allocate ADMIN cerver poll's packet buffer!"
			);
		}

		free (packet_buffer);
		free (polled_fds);
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_ADMIN,
			"Can't handle admins on a NULL cerver!"
		);
	}

	return NULL;

}

#pragma endregion
