#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <poll.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/htab.h"

#include "cerver/auth/auth.h"
#include "cerver/auth/hold.h"
#include "cerver/auth/internal.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/end.h"
#include "cerver/connection/registry.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thpool.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region handler

static const char *on_hold_bad_auth_request = { "The auth packet has an unknown request type" };
static const char *on_hold_bad_auth_data = { "The auth packet does not have data" };
static const char *on_hold_bad_type = { "The packet can not be handled before the authentication" };
static const char *on_hold_bad_version = { "The packet has an incompatible version" };

// a bad packet of an on hold connection: its sender gets an error,
// and the connection is dropped when it reaches the on hold limit
static CerverAuthError cerver_on_hold_handle_max_bad_packets (
	Cerver *cerver, Connection *connection, const char *reason
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	CERVER_STATS_ADD (cerver->stats->received_packets->n_bad_packets, 1);

	(void) error_packet_generate_and_send (
		CERVER_ERROR_PACKET_ERROR, reason, cerver, NULL, connection
	);

	connection->bad_packets += 1;

	if (connection->bad_packets >= cerver->on_hold_max_bad_packets) {
		#ifdef AUTH_DEBUG
		cerver_log_debug (
			"ON HOLD connection <%d> has reached max bad packets, dropping...",
			connection->socket->sock_fd
		);
		#endif

		on_hold_connection_drop (cerver, connection);

		error = CERVER_AUTH_ERROR_DROPPED;
	}

	return error;

}

// handle an auth packet from an on hold connection
// returns the error that was produced by the auth methods
static CerverAuthError cerver_auth_packet_handler (Packet *packet) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	if (packet->data_size > 0) {
		switch (packet->header.request_type) {
			// the client sent use its data to authenticate itself
			case AUTH_PACKET_TYPE_CLIENT_AUTH:
			case AUTH_PACKET_TYPE_CLIENT_TOKEN:
				error = auth_try (packet);
				break;

			case AUTH_PACKET_TYPE_ADMIN_AUTH:
			case AUTH_PACKET_TYPE_ADMIN_TOKEN:
				error = admin_auth_try (packet);
				break;

			default: {
				#ifdef AUTH_DEBUG
				cerver_log (
					LOG_TYPE_WARNING, LOG_TYPE_PACKET,
					"cerver_auth_packet_hanlder () -- got an unknwown request type"
				);
				#endif

				error = cerver_on_hold_handle_max_bad_packets (
					packet->cerver, packet->connection, on_hold_bad_auth_request
				);
			} break;
		}
	}

	else {
		// bad packet
		error = cerver_on_hold_handle_max_bad_packets (
			packet->cerver, packet->connection, on_hold_bad_auth_data
		);
	}

	return error;

}

// returns the error that was produced by internal cerver methods
// based on the error, packets continue being handled or not
static CerverAuthError on_hold_packet_handler_actual (Packet *packet) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	switch (packet->header.packet_type) {
		// handles authentication packets
		case PACKET_TYPE_AUTH:
			error = cerver_auth_packet_handler (packet);
			break;

		// acknowledge the client we have received his test packet
		case PACKET_TYPE_TEST:
			cerver_test_packet_handler (packet);
			break;

		default: {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_PACKET,
				"Got an ON HOLD packet of unknown type in cerver %s.",
				packet->cerver->info->name
			);
			#endif

			error = cerver_on_hold_handle_max_bad_packets (
				packet->cerver, packet->connection, on_hold_bad_type
			);
		} break;
	}

	return error;

}

static CerverAuthError on_hold_packet_handler_check_version (
	Packet *packet
) {

	CerverAuthError error = CERVER_AUTH_ERROR_NONE;

	// the version came in the header, it used to be expected in the data,
	// and the check was inverted, so every valid packet was a bad one
	if (packet->cerver->on_hold_check_packets && !packet_check (packet)) {
		error = cerver_on_hold_handle_max_bad_packets (
			packet->cerver, packet->connection, on_hold_bad_version
		);
	}

	return error;

}

// handles a packet from an on hold connection
// returns 0 if we can / need to handle more packets
// returns 1 if the connection has been ended or removed from on hold
u8 on_hold_packet_handler (Packet *packet) {

	u8 retval = 1;

	if (!on_hold_packet_handler_check_version (packet)) {
		switch (on_hold_packet_handler_actual (packet)) {
			case CERVER_AUTH_ERROR_NONE:
			case CERVER_AUTH_ERROR_MISSING_VALUES:
			case CERVER_AUTH_ERROR_FAILED:
			case CERVER_AUTH_ERROR_INVALID_SESSION:
				retval = 0;
				break;

			default: break;
		}
	}

	packet_delete (packet);

	return retval;

}

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess or failed authentication
// returns 0 on success, 1 on error
// removes the connection from the on hold maps when its fd could not be polled
static void on_hold_connection_unmap (const Cerver *cerver, Connection *connection) {

	Connection *query = connection_new ();
	query->socket = socket_new ();
	if (query->socket) query->socket->sock_fd = connection->socket->sock_fd;

	(void) avl_remove_node (cerver->on_hold_connections, query);

	connection_delete (query);

	const void *key = &connection->socket->sock_fd;
	(void) htab_remove (cerver->on_hold_connection_sock_fd_map, key, sizeof (i32));

}

u8 on_hold_connection (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (cerver && connection) {
		if (cerver->on_hold_connections) {
			// set before the on hold poll can handle its auth packets
			connection->auth_tries = cerver->max_auth_tries;
			(void) time (&connection->connected_timestamp);

			// the poll thread finds the connection in the maps,
			// so its fd is registered in the poll after them
			const void *key = &connection->socket->sock_fd;
			if (!htab_insert (
				cerver->on_hold_connection_sock_fd_map,
				key, sizeof (i32),
				connection, sizeof (Connection)
			)) {
				avl_insert_node (cerver->on_hold_connections, connection);

				retval = on_hold_poll_register_connection (cerver, connection);
				if (retval) on_hold_connection_unmap (cerver, connection);
			}

			else {
				cerver_log_error (
					"on_hold_connection () - "
					"failed to insert connection in on_hold_connection_sock_fd_map htab!"
				);
			}
		}
	}

	return retval;

}

// removes the connection from the on hold structures
// returns 0 on success, 1 on error
u8 on_hold_connection_remove (
	const Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		if (!on_hold_poll_unregister_connection ((Cerver *) cerver, connection)) {
			// remove the connection associated to the sock fd
			Connection *query = connection_new ();
			query->socket = socket_new ();
			if (query->socket) query->socket->sock_fd = connection->socket->sock_fd;

			if (avl_remove_node (cerver->on_hold_connections, query)) {
				#ifdef AUTH_DEBUG
				cerver_log_debug (
					"on_hold_connection_remove () - "
					"removed connection from on_hold_connections avl"
				);
				#endif
			}

			else {
				cerver_log_error (
					"on_hold_connection_remove () - "
					"failed to remove connection from on_hold_connections avl!"
				);
			}

			// remove connection from on hold map
			const void *key = &connection->socket->sock_fd;
			if (htab_remove (
				cerver->on_hold_connection_sock_fd_map, key, sizeof (i32))
			) {
				#ifdef AUTH_DEBUG
				cerver_log_debug (
					"on_hold_connection_remove () - "
					"removed connection from on_hold_connection_sock_fd_map htab"
				);
				#endif
			}

			else {
				cerver_log_error (
					"on_hold_connection_remove () - "
					"failed to remove connection from on_hold_connection_sock_fd_map htab!"
				);
			}

			connection_delete (query);

			retval = 0;
		}
	}

	return retval;

}

// closes the on hold connection, removes it from the cerver and triggers the event
static void on_hold_connection_close (
	const Cerver *cerver, Connection *connection,
	const CerverEventType event_type
) {

	if (cerver && connection) {
		// remove the connection from the on hold structures
		on_hold_connection_remove (cerver, connection);

		// close the connection socket
		connection_end (connection);

		// we can now safely delete the connection
		connection_delete (connection);

		cerver_event_trigger (event_type, cerver, NULL, NULL);
	}

}

// closes the on hold connection and removes it from the cerver
void on_hold_connection_drop (
	const Cerver *cerver, Connection *connection
) {

	on_hold_connection_close (cerver, connection, CERVER_EVENT_ON_HOLD_DROPPED);

}

// closes the on hold connection that its client closed or lost
void on_hold_connection_disconnected (
	const Cerver *cerver, Connection *connection
) {

	on_hold_connection_close (cerver, connection, CERVER_EVENT_ON_HOLD_DISCONNECTED);

}

#pragma endregion

#pragma region poll

static i32 on_hold_get_free_idx (Cerver *cerver) {

	if (cerver) {
		for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
			if (cerver->hold_fds[i].fd == -1) return i;
	}

	return -1;

}

static i32 on_hold_poll_get_idx_with_sock_fd (
	const Cerver *cerver, i32 sock_fd
) {

	if (cerver) {
		for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
			if (cerver->hold_fds[i].fd == sock_fd) return i;
	}

	return -1;

}

// regsiters a connection to the cerver's on hold poll array
// returns 0 on success, 1 on error
u8 on_hold_poll_register_connection (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		(void) pthread_mutex_lock (cerver->on_hold_poll_lock);

		i32 idx = on_hold_get_free_idx (cerver);
		if (idx >= 0) {
			cerver->hold_fds[idx].fd = connection->socket->sock_fd;
			cerver->hold_fds[idx].events = POLLIN;
			cerver->current_on_hold_nfds++;

			CERVER_STATS_ADD (cerver->stats->current_n_hold_connections, 1);

			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Added sock fd <%d> to cerver %s ON HOLD poll, idx: %i",
				connection->socket->sock_fd, cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_CERVER,
				"Cerver %s current ON HOLD connections: %lu",
				cerver->info->name, cerver->stats->current_n_hold_connections
			);
			#endif

			retval = 0;
		}

		else if (idx < 0) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Cerver %s ON HOLD poll is full!",
				cerver->info->name
			);
			#endif
		}

		(void) pthread_mutex_unlock (cerver->on_hold_poll_lock);
	}

	return retval;

}

// removed a sock fd from the cerver's on hold poll array
// returns 0 on success, 1 on error
u8 on_hold_poll_unregister_sock_fd (
	Cerver *cerver, const i32 sock_fd
) {

	u8 retval = 1;

	if (cerver) {
		(void) pthread_mutex_lock (cerver->on_hold_poll_lock);

		i32 idx = on_hold_poll_get_idx_with_sock_fd (cerver, sock_fd);
		if (idx >= 0) {
			cerver->hold_fds[idx].fd = -1;
			cerver->hold_fds[idx].events = -1;
			cerver->hold_fds[idx].revents = -1;
			cerver->current_on_hold_nfds--;

			CERVER_STATS_SUBTRACT (cerver->stats->current_n_hold_connections, 1);

			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Removed sock fd <%d> from cerver %s ON HOLD poll, idx: %d",
				sock_fd, cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_CERVER,
				"Cerver %s current ON HOLD connections: %lu",
				cerver->info->name,
				cerver->stats->current_n_hold_connections
			);
			#endif

			retval = 0;
		}

		else {
			// #ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Sock fd <%d> was NOT found in cerver %s ON HOLD poll!",
				sock_fd, cerver->info->name
			);
			// #endif
		}

		(void) pthread_mutex_unlock (cerver->on_hold_poll_lock);
	}

	return retval;

}

// unregsiters a connection from the cerver's on hold poll array
// returns 0 on success, 1 on error
u8 on_hold_poll_unregister_connection (
	Cerver *cerver, Connection *connection
) {

	return (cerver && connection) ?
		on_hold_poll_unregister_sock_fd (cerver, connection->socket->sock_fd) : 1;

}

// the poll thread works with its own copy of the fds,
// because the accept thread registers new connections while it polls
static void on_hold_poll_copy_fds (
	Cerver *cerver, struct pollfd *polled_fds
) {

	(void) pthread_mutex_lock (cerver->on_hold_poll_lock);

	(void) memcpy (
		polled_fds, cerver->hold_fds,
		sizeof (struct pollfd) * cerver->max_on_hold_connections
	);

	(void) pthread_mutex_unlock (cerver->on_hold_poll_lock);

}

static void on_hold_poll_handle_fd (
	Cerver *cerver, char *packet_buffer,
	const struct pollfd *polled_fd
) {

	CerverReceive *cr = cerver_receive_create (
		RECEIVE_TYPE_ON_HOLD, cerver, polled_fd->fd
	);

	if (cr) {
		if (polled_fd->revents == POLLIN) {
			cerver_receive_internal (
				cr,
				packet_buffer,
				cerver->on_hold_receive_buffer_size
			);
		}

		else {
			cerver_receive_handle_failed (cr);
		}

		cerver_receive_delete (cr);
	}

}

static void on_hold_poll_expire_fd (
	Cerver *cerver, const i32 sock_fd, const time_t now
) {

	// a connection that was handled in this round is not on hold anymore
	Connection *connection = connection_get_with_sock_fd_from_on_hold (cerver, sock_fd);

	if (connection && ((now - connection->connected_timestamp) >= (time_t) cerver->on_hold_timeout)) {
		cerver_log_warning (
			"Cerver %s ON HOLD connection %d did not authenticate in %u secs - dropping...",
			cerver->info->name, sock_fd, cerver->on_hold_timeout
		);

		on_hold_connection_drop (cerver, connection);
	}

}

// drops the on hold connections that did not authenticate in time,
// only this thread handles them, so they are not in use
static void on_hold_poll_expire (
	Cerver *cerver, const struct pollfd *polled_fds
) {

	if (!cerver->on_hold_timeout) return;

	const time_t now = time (NULL);

	for (u32 idx = 0; idx < cerver->max_on_hold_connections; idx++) {
		if (polled_fds[idx].fd > -1) {
			on_hold_poll_expire_fd (cerver, polled_fds[idx].fd, now);
		}
	}

}

static inline void on_hold_poll_handle (
	Cerver *cerver, char *packet_buffer,
	const struct pollfd *polled_fds
) {

	// one or more fd(s) are readable, need to determine which ones they are
	for (u32 idx = 0; idx < cerver->max_on_hold_connections; idx++) {
		if ((polled_fds[idx].fd > -1) && polled_fds[idx].revents) {
			on_hold_poll_handle_fd (cerver, packet_buffer, &polled_fds[idx]);
		}
	}

}

// handles packets from the on hold clients until they authenticate
void *on_hold_poll (void *cerver_ptr) {

	if (cerver_ptr) {
		Cerver *cerver = (Cerver *) cerver_ptr;

		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
			"Cerver %s ON HOLD poll has started!",
			cerver->info->name
		);

		(void) thread_set_name ("%s-on-hold", cerver->info->alias);

		char *packet_buffer = (char *) calloc (
			cerver->on_hold_receive_buffer_size, sizeof (char)
		);

		struct pollfd *polled_fds = (struct pollfd *) calloc (
			cerver->max_on_hold_connections, sizeof (struct pollfd)
		);

		if (packet_buffer && polled_fds) {
			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Waiting for connections to put on hold..."
			);
			#endif

			int poll_retval = 0;
			while (cerver_is_running (cerver)) {
				on_hold_poll_copy_fds (cerver, polled_fds);

				poll_retval = poll (
					polled_fds,
					cerver->max_on_hold_connections,
					cerver->on_hold_poll_timeout
				);

				switch (poll_retval) {
					case -1: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_CERVER,
							"Cerver %s ON HOLD poll has failed!",
							cerver->info->name
						);

						perror ("Error");
						cerver_stop_request (cerver);
					} break;

					case 0: {
						// #ifdef AUTH_DEBUG
						// cerver_log (
						//     LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
						//     "Cerver %s ON HOLD poll timeout",
						// 	cerver->info->name
						// );
						// #endif
					} break;

					default: {
						on_hold_poll_handle (
							cerver,
							packet_buffer,
							polled_fds
						);
					} break;
				}

				on_hold_poll_expire (cerver, polled_fds);
			}

			#ifdef AUTH_DEBUG
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_NONE,
				"Cerver %s ON HOLD poll has stopped!",
				cerver->info->name
			);
			#endif

		}

		else {
			cerver_log_error (
				"Failed to allocate cerver ON HOLD poll's packet buffer!"
			);
		}

		free (packet_buffer);
		free (polled_fds);
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Can't handle ON HOLD clients on a NULL cerver!"
		);
	}

	return NULL;

}

#pragma endregion
