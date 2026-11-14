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
#include "cerver/admin/lifecycle.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/core/handler.h"
#include "cerver/core/internal.h"
#include "cerver/core/start.h"
#include "cerver/files/cerver.h"
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

#pragma region end

static void cerver_stop_wake_threads (Cerver *cerver) {

	if (cerver->stop_mutex && cerver->stop_cond) {
		(void) pthread_mutex_lock (cerver->stop_mutex);
		(void) pthread_cond_broadcast (cerver->stop_cond);
		(void) pthread_mutex_unlock (cerver->stop_mutex);
	}

}

// stops accepting new connections and wakes up cerver_start ()
// the listening socket is only closed by cerver_teardown (),
// so its fd can not be reused while another thread is inside accept ()
// a request before cerver_start () cancels the start
// it is called from signal handlers, so it keeps the errno of the thread that it interrupted,
// the shutdown () of an UDP socket that is not connected fails with ENOTCONN
void cerver_stop_request (Cerver *cerver) {

	const int saved_errno = errno;

	if (cerver) {
		__atomic_store_n (&cerver->stop_requested, true, __ATOMIC_SEQ_CST);

		cerver_set_running (cerver, false);

		// wakes up the threads blocked in accept () or poll (),
		// a socket created after this load is never listened in
		const i32 sock = __atomic_load_n (&cerver->sock, __ATOMIC_SEQ_CST);
		if (sock >= 0) {
			(void) shutdown (sock, SHUT_RDWR);
		}

		cerver_stop_wake_threads (cerver);
	}

	errno = saved_errno;

}

// stops accepting new connections and wakes up cerver_start ()
// it can be called after the cerver was stopped by a signal
// returns 0 on success, 1 on error
u8 cerver_shutdown (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_stop_request (cerver);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"The cerver %s has stopped accepting connections.",
			cerver->info->name
		);
		#endif

		retval = 0;
	}

	return retval;

}

void cerver_close_socket (Cerver *cerver) {

	if (cerver->sock >= 0) {
		if (close (cerver->sock)) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to close cerver %s socket!",
				cerver->info->name
			);
		}

		cerver->sock = -1;
	}

}

static void cerver_thread_join (pthread_t *thread_id) {

	if (*thread_id) {
		(void) pthread_join (*thread_id, NULL);
		*thread_id = 0;
	}

}

// the update threads end as soon as the cerver is stopped
static void cerver_stop_threads (Cerver *cerver) {

	cerver_thread_join (&cerver->update_thread_id);
	cerver_thread_join (&cerver->update_interval_thread_id);
	cerver_thread_join (&cerver->inactive_thread_id);
	cerver_thread_join (&cerver->on_hold_poll_thread_id);

}

static void cerver_shutdown_connection (void *connection_ptr, void *args) {

	(void) args;

	Connection *connection = (Connection *) connection_ptr;
	if (connection && connection->socket) {
		(void) socket_shutdown (connection->socket);
	}

}

// the packet that tells the clients that the cerver is going to teardown
typedef struct CerverTeardownNotice {

	Cerver *cerver;
	Client *client;
	const Packet *packet;

} CerverTeardownNotice;

static void cerver_notify_teardown_connection (void *connection_ptr, void *notice_ptr) {

	const CerverTeardownNotice *notice = (const CerverTeardownNotice *) notice_ptr;

	size_t sent = 0;
	(void) packet_send_to (
		notice->packet, &sent, false,
		notice->cerver, notice->client, (Connection *) connection_ptr, NULL
	);

}

// must be called with the clients tree locked
static void cerver_notify_teardown_clients (const AVLNode *node, CerverTeardownNotice *notice) {

	if (node) {
		cerver_notify_teardown_clients (node->left, notice);

		Client *client = (Client *) node->id;
		if (client && client->connections) {
			notice->client = client;
			(void) dlist_traverse (client->connections, cerver_notify_teardown_connection, notice);
		}

		cerver_notify_teardown_clients (node->right, notice);
	}

}

// the clients learn that the cerver is going to teardown
// before the drain shuts their connections down,
// a web cerver never writes a cerver packet into an HTTP connection
static void cerver_notify_teardown (Cerver *cerver) {

	if (
		(cerver->type != CERVER_TYPE_WEB)
		&& cerver->clients
		&& (CERVER_STATS_GET (cerver->stats->current_n_connected_clients) > 0)
	) {
		Packet *packet = packet_generate_request (PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_TEARDOWN, NULL, 0);
		if (packet) {
			CerverTeardownNotice notice = { cerver, NULL, packet };

			(void) pthread_mutex_lock (cerver->clients->mutex);

			cerver_notify_teardown_clients (cerver->clients->root, &notice);

			(void) pthread_mutex_unlock (cerver->clients->mutex);

			packet_delete (packet);
		}
	}

}

// must be called with the clients tree locked
static void cerver_shutdown_clients_connections (const AVLNode *node) {

	if (node) {
		cerver_shutdown_clients_connections (node->left);

		const Client *client = (const Client *) node->id;
		if (client && client->connections) {
			(void) dlist_traverse (client->connections, cerver_shutdown_connection, NULL);
		}

		cerver_shutdown_clients_connections (node->right);
	}

}

// the clients keep their connections open, so their receive threads
// only finish when the sockets are shutdown
static void cerver_shutdown_clients (Cerver *cerver) {

	if (cerver->clients) {
		(void) pthread_mutex_lock (cerver->clients->mutex);

		cerver_shutdown_clients_connections (cerver->clients->root);

		(void) pthread_mutex_unlock (cerver->clients->mutex);
	}

}

// waits for the thpool and for the detachable connection threads,
// returns true if both finished before the timeout
static bool cerver_drain_wait (Cerver *cerver, const time_t timeout) {

	bool drained = true;

	if (cerver->thpool) {
		drained = !thpool_drain (
			cerver->thpool,
			(cerver->drain_policy == CERVER_DRAIN_POLICY_RUN),
			timeout
		);
	}

	if (thread_group_wait (&cerver->connection_threads, timeout)) {
		drained = false;
	}

	return drained;

}

// logs what is still alive after the drain grace, its data is not released
static void cerver_drain_report (Cerver *cerver) {

	cerver_log_warning (
		"Cerver %s was NOT drained in %u + %u secs - "
		"%u thpool connections working - %u queued - %u connection threads - "
		"%u event threads - %u handlers alive, the data they use will not be released",
		cerver->info->name, cerver->drain_timeout, CERVER_DRAIN_GRACE_TIMEOUT,
		cerver->thpool ? thpool_get_num_threads_working (cerver->thpool) : 0,
		cerver->thpool ? thpool_get_queued (cerver->thpool) : 0,
		thread_group_alive (&cerver->connection_threads),
		thread_group_alive (&cerver->event_threads),
		cerver_get_n_handlers_alive (cerver)
	);

}

// the WebSocket connections of a web cerver go away with 1001, or they are cut
static void cerver_drain_web_sockets (Cerver *cerver, const bool abort) {

	HttpCerver *http_cerver = (HttpCerver *) cerver->cerver_data;
	if (http_cerver && http_cerver->web_sockets) {
		if (abort) http_web_socket_registry_abort (http_cerver->web_sockets);
		else http_web_socket_registry_go_away (http_cerver->web_sockets);
	}

}

// waits for the connections in progress using the drain policy and timeout,
// if they do not finish, a short grace is given before reporting them
// a cerver configured with threads shuts its clients down and waits for them,
// also with the poll handler type, that does not create the thpool
static inline bool cerver_drain_needed (const Cerver *cerver, const bool connection_threads) {

	return cerver->thpool || (cerver->n_thpool_threads > 0) || connection_threads;

}

static void cerver_drain (Cerver *cerver) {

	cerver->drained = true;

	const bool connection_threads = thread_group_alive (&cerver->connection_threads) > 0;

	if (cerver_drain_needed (cerver, connection_threads)) {
		cerver_log_msg (
			"Draining cerver %s - %u working - %u queued - %u connection threads - %s policy - %u secs timeout",
			cerver->info->name,
			cerver->thpool ? thpool_get_num_threads_working (cerver->thpool) : 0,
			cerver->thpool ? thpool_get_queued (cerver->thpool) : 0,
			thread_group_alive (&cerver->connection_threads),
			cerver_drain_policy_to_string (cerver->drain_policy),
			cerver->drain_timeout
		);

		if (cerver->type != CERVER_TYPE_WEB) {
			cerver_shutdown_clients (cerver);
		}

		// the WebSocket connections are closed with 1001, their threads end with their handshake
		else {
			cerver_drain_web_sockets (cerver, false);
		}

		cerver->drained = cerver_drain_wait (cerver, (time_t) cerver->drain_timeout);

		if (!cerver->drained) {
			cerver_log_warning (
				"Cerver %s was NOT drained in %u secs - waiting %u more secs",
				cerver->info->name, cerver->drain_timeout, CERVER_DRAIN_GRACE_TIMEOUT
			);

			// the WebSocket connections that did not end are cut
			if (cerver->type == CERVER_TYPE_WEB) cerver_drain_web_sockets (cerver, true);

			cerver->drained = cerver_drain_wait (cerver, (time_t) CERVER_DRAIN_GRACE_TIMEOUT);
		}

		if (cerver->drained) {
			cerver_log_success ("Cerver %s was drained!", cerver->info->name);
		}

		else {
			cerver_drain_report (cerver);
		}
	}

}

// get rid of any on hold connections
static void cerver_destroy_on_hold_connections (Cerver *cerver) {

	if (cerver) {
		if (cerver->auth_required) {
			// ends and deletes any on hold connection
			avl_delete (cerver->on_hold_connections);
			cerver->on_hold_connections = NULL;

			if (cerver->hold_fds) {
				free (cerver->hold_fds);
				cerver->hold_fds = NULL;
			}
		}
	}

}

// cleans up the client's structures in the current cerver
static void cerver_destroy_clients (Cerver *cerver) {

	if (cerver) {
		// the clients got the teardown packet before the drain (cerver_notify_teardown ())

		// destroy the sock fd client map
		htab_destroy (cerver->client_sock_fd_map);
		cerver->client_sock_fd_map = NULL;

		// this will end and delete client connections and then delete the client
		avl_delete (cerver->clients);
		cerver->clients = NULL;

		if (cerver->fds) {
			free (cerver->fds);
			cerver->fds = NULL;
		}
	}

}

static void cerver_clean_thpool (Cerver *cerver) {

	if (cerver->thpool) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Cerver %s active thpool threads: %u",
			cerver->info->name,
			thpool_get_num_threads_working (cerver->thpool)
		);
		#endif

		thpool_destroy (cerver->thpool);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Destroyed cerver %s thpool!", cerver->info->name
		);
		#endif

		cerver->thpool = NULL;
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Cerver %s does NOT have a thpool to destroy",
			cerver->info->name
		);
		#endif
	}

}

static void cerver_clean_data (Cerver *cerver);

// clean cerver data structures
// the connections are drained before anything they use is released
static void cerver_clean (Cerver *cerver) {

	if (cerver) {
		// stop accepting new connections
		cerver_stop_request (cerver);

		cerver_notify_teardown (cerver);

		// the lobbies stop their threads and give their connections back
		game_cerver_stop (cerver);

		cerver_stop_threads (cerver);

		cerver_drain (cerver);

		if (cerver->drained) {
			// stop any app / custom handler
			if (!cerver_handlers_destroy (cerver)) {
				cerver_log_success (
					"Done destroying handlers in cerver %s",
					cerver->info->name
				);
			}

			cerver_clean_thpool (cerver);

			cerver_clean_data (cerver);
		}

		cerver_close_socket (cerver);
	}

}

// releases the data that the connections use
// only after the thpool was drained and destroyed
static void cerver_clean_data (Cerver *cerver) {

	if (cerver) {
		switch (cerver->type) {
			case CERVER_TYPE_CUSTOM: break;

			case CERVER_TYPE_GAME: {
				if (cerver->cerver_data) {
					GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

					if (game_cerver->final_game_action)
						game_cerver->final_game_action (game_cerver->final_action_args);

					// the lobbies were torn down when the cerver stopped
					game_cerver_stop (cerver);

					game_delete (game_cerver);      // delete game cerver data
					cerver->cerver_data = NULL;
				}
			} break;

			case CERVER_TYPE_WEB: {
				if (cerver->cerver_data) {
					http_cerver_end ((HttpCerver *) cerver->cerver_data);
					http_cerver_delete (cerver->cerver_data);
					cerver->cerver_data = NULL;
				}
			} break;

			case CERVER_TYPE_FILES: {
				if (cerver->cerver_data) {
					file_cerver_delete (cerver->cerver_data);
					cerver->cerver_data = NULL;
				}
			} break;

			default: break;
		}

		// clean up on hold connections
		cerver_destroy_on_hold_connections (cerver);

		// the peers of an UDP cerver
		cerver_udp_end (cerver);

		// clean up cerver connected clients
		cerver_destroy_clients (cerver);
		#ifdef CERVER_DEBUG
		cerver_log (LOG_TYPE_DEBUG, LOG_TYPE_CERVER, "Done cleaning up clients.");
		#endif
	}

}

// teardown a cerver -> stop the cerver and clean all of its data
// returns 0 on success, 1 on error
u8 cerver_teardown (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Starting cerver %s teardown...", cerver->info->name
		);
		#endif

		cerver_event_trigger (
			CERVER_EVENT_TEARDOWN,
			cerver,
			NULL, NULL
		);

		cerver_clean (cerver);

		// correctly end admin connections & stop admin handlers
		admin_cerver_end (cerver->admin);

		if (cerver->drained) {
			// no connection or handler can trigger an event after this
			(void) thread_group_wait (&cerver->event_threads, THREAD_GROUP_WAIT_FOREVER);

			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_NONE,
				"Cerver %s teardown was successful!",
				cerver->info->name
			);

			cerver_delete (cerver);
		}

		// the connections that are still working use the cerver
		else {
			cerver_log_warning (
				"Cerver %s teardown ended without releasing its memory",
				cerver->info->name
			);
		}

		retval = 0;
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log (LOG_TYPE_ERROR, LOG_TYPE_CERVER, "Can't teardown a NULL cerver!");
		#endif
	}

	return retval;

}

#pragma endregion
