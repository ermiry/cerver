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
#include "cerver/auth/hold.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/core/end.h"
#include "cerver/core/internal.h"
#include "cerver/core/serialization.h"
#include "cerver/core/start.h"
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

#pragma region init

// every address of the cerver's family
static void cerver_network_init_address_any (Cerver *cerver) {

	(void) memset (&cerver->address, 0, sizeof (struct sockaddr_storage));

	if (cerver->use_ipv6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &cerver->address;
		addr->sin6_family = AF_INET6;
		addr->sin6_addr = in6addr_any;
		addr->sin6_port = htons (cerver->port);
	}

	else {
		struct sockaddr_in *addr = (struct sockaddr_in *) &cerver->address;
		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = INADDR_ANY;
		addr->sin_port = htons (cerver->port);
	}

}

// the address of cerver_set_bind_address (), or every address of the cerver's family
// returns 0 on success, 1 on error
static u8 cerver_network_init_address_bind (Cerver *cerver) {

	u8 retval = 0;

	if (cerver->bind_address[0]) {
		retval = (u8) network_parse_address (cerver->bind_address, cerver->port, &cerver->address);
	}

	else {
		cerver_network_init_address_any (cerver);
	}

	return retval;

}

// an IPv6 socket always sets IPV6_V6ONLY,
// so the cerver does not depend on the net.ipv6.bindv6only sysctl
static void cerver_network_init_ipv6_only (const Cerver *cerver) {

	if (cerver->use_ipv6 && sock_set_ipv6_only (cerver->sock, cerver->ipv6_only)) {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"Failed to set cerver %s IPV6_V6ONLY to %d",
			cerver->info->name, cerver->ipv6_only
		);
	}

}

// inits the cerver's address & binds the socket to it
static u8 cerver_network_init_address (Cerver *cerver) {

	u8 retval = 1;

	if (cerver_network_init_address_bind (cerver)) {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Cerver %s bind address %s is not valid!",
			cerver->info->name, cerver->bind_address
		);

		cerver_close_socket (cerver);

		return retval;
	}

	cerver_network_init_ipv6_only (cerver);

	if (cerver->reusable) {
		if (sock_set_reusable (cerver->sock)) {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Failed to set cerver's sock fd reusable falgs"
			);
		}
	}

	if (cerver->reusable_port) {
		if (sock_set_reusable_port (cerver->sock)) {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Failed to set cerver's sock fd reusable port flag"
			);
		}
	}

	if (!bind (
		cerver->sock,
		(const struct sockaddr *) &cerver->address,
		sock_address_size (&cerver->address))
	) {
		retval = 0;       // success!!
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to bind cerver %s socket!",
			cerver->info->name
		);

		cerver_close_socket (cerver);
	}

	return retval;

}

// set cerver's socket blocking property based on cerver handler type
static u8 cerver_network_init_block_socket (Cerver *cerver) {

	u8 retval = 0;

	switch (cerver->handler_type) {
		case CERVER_HANDLER_TYPE_NONE: break;

		case CERVER_HANDLER_TYPE_POLL: {
			// set the socket to non blocking mode
			if (sock_set_blocking (cerver->sock, false)) {
				cerver->blocking = false;
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
					"Cerver %s socket set to non blocking mode.",
					cerver->info->name
				);
				#endif
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to set cerver %s socket to non blocking mode!",
					cerver->info->name
				);

				cerver_close_socket (cerver);

				retval = 1;     // error
			}
		} break;

		case CERVER_HANDLER_TYPE_THREADS: break;

		default: break;
	}

	return retval;

}

// creates the socket with the selected protocol
static i32 cerver_network_create_socket (const Cerver *cerver) {

	i32 sock = -1;

	switch (cerver->protocol) {
		case IPPROTO_TCP: {
			sock = socket (
				(cerver->use_ipv6 ? AF_INET6 : AF_INET),
				SOCK_STREAM, 0
			);
		} break;

		case IPPROTO_UDP: {
			sock = socket (
				(cerver->use_ipv6 ? AF_INET6 : AF_INET),
				SOCK_DGRAM, 0
			);
		} break;

		default:
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Unknown protocol type!"
			);
			break;
	}

	return sock;

}

// a bind address decides the family of the socket,
// even if cerver_set_network_values () changed use_ipv6 after cerver_set_bind_address ()
static void cerver_network_init_family (Cerver *cerver) {

	struct sockaddr_storage parsed;
	if (
		cerver->bind_address[0]
		&& !network_parse_address (cerver->bind_address, cerver->port, &parsed)
	) {
		cerver->use_ipv6 = (parsed.ss_family == AF_INET6);
	}

}

// inits the cerver networking capabilities
// the socket is stored with __atomic, cerver_stop_request () can read it from another thread
static u8 cerver_network_init (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_network_init_family (cerver);

		__atomic_store_n (&cerver->sock, cerver_network_create_socket (cerver), __ATOMIC_SEQ_CST);

		if (cerver->sock >= 0) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Created cerver %s socket - <%d>!",
				cerver->info->name,
				cerver->sock
			);
			#endif

			if (!cerver_network_init_block_socket (cerver)) {
				retval = cerver_network_init_address (cerver);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to create cerver %s socket!",
				cerver->info->name
			);
		}
	}

	return retval;

}

static u8 cerver_init_poll_fds (Cerver *cerver) {

	u8 retval = 1;

	// max_n_fds is the initial size, from cerver_set_poll_fds ()
	cerver->fds = (struct pollfd *) calloc (cerver->max_n_fds, sizeof (struct pollfd));
	if (cerver->fds) {
		// set all fds as available spaces
		for (u32 i = 0; i < cerver->max_n_fds; i++) cerver->fds[i].fd = -1;

		cerver->current_n_fds = 0;

		retval = 0;     // success!!
	}

	else {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Failed to allocate cerver %s main fds!", cerver->info->name
		);
		#endif
	}

	return retval;

}

static u8 cerver_init_data_structures (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver->clients = avl_init (
			cerver->use_sessions ? client_comparator_session_id : client_comparator_client_id,
			client_delete
		);

		if (cerver->clients) {
			cerver->client_sock_fd_map = htab_create (cerver->max_n_fds, NULL, NULL);
			if (cerver->client_sock_fd_map) {
				u8 errors = 0;

				// init cerver handler type based values
				switch (cerver->handler_type) {
					case CERVER_HANDLER_TYPE_NONE: break;

					case CERVER_HANDLER_TYPE_POLL: {
						// initialize main pollfd structures
						errors |= cerver_init_poll_fds (cerver);
					} break;

					case CERVER_HANDLER_TYPE_THREADS: break;

					default: break;
				}

				retval = errors;
			}

			else {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to init clients sock fd map in cerver %s",
					cerver->info->name
				);
				#endif
			}
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init clients avl in cerver %s",
				cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

// inits a cerver of a given type
static u8 cerver_init_internal (Cerver *cerver) {

	int retval = 1;

	if (cerver) {
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Initializing cerver %s...", cerver->info->name
		);

		cerver_log_msg ("Cerver type: %s", cerver_type_to_string (cerver->type));
		cerver_log_msg ("Cerver handler type: %s", cerver_handler_type_to_string (cerver->handler_type));

		if (!cerver_network_init (cerver)) {
			if (!cerver_init_data_structures (cerver)) {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
					"Done initializing cerver %s network values & data structures!",
					cerver->info->name
				);
				#endif

				retval = 0;     // success!!
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to init cerver %s data structures!",
					cerver->info->name
				);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init cerver %s network!", cerver->info->name
			);
		}
	}

	return retval;

}

// only the threads handler type takes connections with the thpool,
// the poll () handles every connection in its own thread,
// a web cerver handles each request in its thpool with any handler type
static inline bool cerver_uses_thpool (const Cerver *cerver) {

	return (
		(cerver->type == CERVER_TYPE_WEB)
		|| (cerver->handler_type == CERVER_HANDLER_TYPE_THREADS)
	) && (cerver->n_thpool_threads > 0);

}

static void cerver_one_time_init_thpool_unused (const Cerver *cerver) {

	if (cerver->n_thpool_threads > 0) {
		cerver_log_msg (
			"Cerver %s does not create its thpool of %u threads with the %s handler type",
			cerver->info->name, cerver->n_thpool_threads,
			cerver_handler_type_to_string (cerver->handler_type)
		);
	}

}

static u8 cerver_one_time_init_thpool (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver_uses_thpool (cerver)) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Cerver %s is configured to use a thpool with %d threads",
				cerver->info->name, cerver->n_thpool_threads
			);
			#endif

			cerver->thpool = thpool_create (cerver->n_thpool_threads);
			if (cerver->thpool) {
				thpool_set_name (cerver->thpool, cerver->info->name);

				if (cerver->thpool_queue_size) {
					errors |= thpool_set_queue_size (cerver->thpool, cerver->thpool_queue_size);
				}

				errors |= thpool_init (cerver->thpool);
			}

			else {
				errors = 1;
			}

			if (errors) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to init cerver %s thpool!", cerver->info->name
				);
			}

			else {
				cerver_log_msg (
					"Cerver %s thpool: %u threads - %u queue size - %s overload policy",
					cerver->info->name,
					cerver->n_thpool_threads,
					thpool_get_queue_size (cerver->thpool),
					cerver_overload_policy_to_string (cerver->overload_policy)
				);
			}
		}

		else {
			cerver_one_time_init_thpool_unused (cerver);
		}
	}

	return errors;

}

static u8 cerver_one_time_init (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (!cerver_init_internal (cerver)) {
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"Initialized cerver %s!", cerver->info->name
			);

			// 28/05/2020
			cerver->poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
			pthread_mutex_init (cerver->poll_lock, NULL);

			// init the cerver thpool
			errors |= cerver_one_time_init_thpool (cerver);

			// perform one time init methods by cerver type
			switch (cerver->type) {
				case CERVER_TYPE_CUSTOM: break;

				case CERVER_TYPE_GAME: {
					GameCerver *game_data = (GameCerver *) cerver->cerver_data;
					game_data->cerver = cerver;
					if (game_data && game_data->load_game_data) {
						game_data->load_game_data (NULL);
					}

					else {
						cerver_log (
							LOG_TYPE_WARNING, LOG_TYPE_GAME,
							"Game cerver %s doesn't have a reference to a game data!",
							cerver->info->name
						);
					}
				} break;

				case CERVER_TYPE_WEB: {
					errors |= http_cerver_init ((HttpCerver *) cerver->cerver_data);
				} break;

				case CERVER_TYPE_FILES: break;

				default: break;
			}

			cerver->info->cerver_info_packet = cerver_packet_generate (cerver);
			if (!cerver->info->cerver_info_packet) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Failed to generate cerver %s info packet",
					cerver->info->name
				);

				errors |= 1;
			}
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to init cerver %s!", cerver->info->name
			);

			errors |= 1;
		}
	}

	return errors;

}

#pragma endregion

#pragma region start

// the thread that receives the packets of the on hold connections
// joined in cerver_stop_threads () before its fds are released
static void cerver_auth_start_poll (Cerver *cerver) {

	if (thread_create_joinable (&cerver->on_hold_poll_thread_id, on_hold_poll, cerver)) {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_NONE,
			"Failed to create cerver's %s on_hold_poll () thread!",
			cerver->info->name
		);
	}

}

// inits cerver's auth capabilities
static u8 cerver_auth_start (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver->auth_packet = packet_generate_request (PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_REQUEST_AUTH, NULL, 0);

		cerver->on_hold_connections = avl_init (connection_comparator, connection_delete);
		cerver->on_hold_connection_sock_fd_map = htab_create (cerver->max_on_hold_connections, NULL, NULL);
		if (cerver->on_hold_connections && cerver->on_hold_connection_sock_fd_map) {
			cerver->hold_fds = (struct pollfd *) calloc (cerver->max_on_hold_connections, sizeof (struct pollfd));
			if (cerver->hold_fds) {
				memset (cerver->hold_fds, 0, sizeof (struct pollfd) * cerver->max_on_hold_connections);

				for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
					cerver->hold_fds[i].fd = -1;

				cerver->current_on_hold_nfds = 0;

				cerver->on_hold_poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
				pthread_mutex_init (cerver->on_hold_poll_lock, NULL);

				// the peers of an UDP cerver authenticate in its receive loop (udp.c)
				if (cerver->protocol == PROTOCOL_TCP) {
					cerver_auth_start_poll (cerver);
				}

				retval = 0;
			}
		}
	}

	return retval;

}

// 11/05/2020 -- start cerver's multiple app handlers
static u8 cerver_multiple_app_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		cerver->num_handlers_alive = 0;
		cerver->num_handlers_working = 0;

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Initializing cerver %s multiple app handlers...",
			cerver->info->name
		);
		#endif

		// only the handlers that are not direct have a thread
		unsigned int started = 0;
		for (unsigned int i = 0; i < cerver->n_handlers; i++) {
			if (cerver->handlers[i] && !cerver->handlers[i]->direct_handle) {
				errors |= handler_start (cerver->handlers[i]);
				started += 1;
			}
		}

		if (!errors) {
			// wait for all handlers to be initialized,
			// they mark themselves alive with the handlers lock and signal its condition
			(void) pthread_mutex_lock (cerver->handlers_lock);
			while (cerver->num_handlers_alive < started) {
				(void) pthread_cond_wait (cerver->handlers_alive_cond, cerver->handlers_lock);
			}
			(void) pthread_mutex_unlock (cerver->handlers_lock);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"Cerver %s multiple app handlers are ready!",
				cerver->info->name
			);
			#endif
		}

		else {
			cerver_log_error (
				"Failed to init ALL multiple app handlers in cerver %s",
				cerver->info->name
			);
		}
	}

	return errors;

}

static u8 cerver_app_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		if (cerver->multiple_handlers) {
			errors |= cerver_multiple_app_handlers_start (cerver);
		}

		else {
			if (cerver->app_packet_handler) {
				if (!cerver->app_packet_handler->direct_handle) {
					// init single app packet handler
					if (!handler_start (cerver->app_packet_handler)) {
						#ifdef CERVER_DEBUG
						cerver_log_success (
							"Cerver %s app_packet_handler has started!",
							cerver->info->name
						);
						#endif
					}

					else {
						cerver_log_error (
							"Failed to start cerver %s app_packet_handler!",
							cerver->info->name
						);

						errors = 1;
					}
				}
			}

			else {
				switch (cerver->type) {
					case CERVER_TYPE_WEB: break;
					case CERVER_TYPE_FILES: break;

					default: {
						cerver_log_warning (
							"Cerver %s does not have an app_packet_handler",
							cerver->info->name
						);
					} break;
				}
			}
		}
	}

	return errors;

}

static u8 cerver_app_error_handler_start (Cerver *cerver) {

	u8 retval = 0;

	if (cerver) {
		if (cerver->app_error_packet_handler) {
			if (!cerver->app_error_packet_handler->direct_handle) {
				if (!handler_start (cerver->app_error_packet_handler)) {
					#ifdef CERVER_DEBUG
					cerver_log_success (
						"Cerver %s app_error_packet_handler has started!",
						cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start cerver %s app_error_packet_handler!",
						cerver->info->name
					);
				}
			}
		}

		else {
			switch (cerver->type) {
				case CERVER_TYPE_WEB: break;
				case CERVER_TYPE_FILES: break;

				default: {
					cerver_log_warning (
						"Cerver %s does not have an app_error_packet_handler",
						cerver->info->name
					);
				} break;
			}
		}
	}

	return retval;

}

static u8 cerver_custom_handler_start (Cerver *cerver) {

	u8 retval = 0;

	if (cerver) {
		if (cerver->custom_packet_handler) {
			if (!cerver->custom_packet_handler->direct_handle) {
				if (!handler_start (cerver->custom_packet_handler)) {
					#ifdef CERVER_DEBUG
					cerver_log_success (
						"Cerver %s custom_packet_handler has started!",
						cerver->info->name
					);
					#endif
				}

				else {
					cerver_log_error (
						"Failed to start cerver %s custom_packet_handler!",
						cerver->info->name
					);
				}
			}
		}

		else {
			switch (cerver->type) {
				case CERVER_TYPE_WEB: break;
				case CERVER_TYPE_FILES: break;

				default: {
					cerver_log_warning (
						"Cerver %s does not have a custom_packet_handler",
						cerver->info->name
					);
				} break;
			}
		}
	}

	return retval;

}

// 27/05/2020 -- starts all cerver's handlers
static u8 cerver_handlers_start (Cerver *cerver) {

	u8 errors = 0;

	if (cerver) {
		#ifdef CERVER_DEBUG
		cerver_log_debug (
			"Initializing %s handlers...",
			cerver->info->name
		);
		#endif

		cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
		pthread_mutex_init (cerver->handlers_lock, NULL);

		cerver->handlers_alive_cond = thread_cond_new ();

		errors |= cerver_app_handlers_start (cerver);

		errors |= cerver_app_error_handler_start (cerver);

		errors |= cerver_custom_handler_start (cerver);

		errors |= cerver_request_handlers_start (cerver);

		if (!errors) {
			#ifdef CERVER_DEBUG
			cerver_log_debug (
				"Done initializing %s handlers!",
				cerver->info->name
			);
			#endif
		}
	}

	return errors;

}

static u8 cerver_update_start (Cerver *cerver) {

	u8 retval = 1;

	if (!thread_create_joinable (
		&cerver->update_thread_id,
		cerver_update,
		cerver
	)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Created cerver %s UPDATE thread!",
			cerver->info->name
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create cerver %s UPDATE thread!",
			cerver->info->name
		);
	}

	return retval;

}

static u8 cerver_update_interval_start (Cerver *cerver) {

	u8 retval = 1;

	if (!thread_create_joinable (
		&cerver->update_interval_thread_id,
		cerver_update_interval,
		cerver
	)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Created cerver %s UPDATE INTERVAL thread!",
			cerver->info->name
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to create cerver %s UPDATE INTERVAL thread!",
			cerver->info->name
		);
	}

	return retval;

}

// sleeps the seconds or until the cerver is stopped
void cerver_sleep (Cerver *cerver, u32 seconds) {

	struct timespec deadline = CERVER_ZERO_INITIALIZER;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += seconds;

	(void) pthread_mutex_lock (cerver->stop_mutex);

	int result = 0;
	while (cerver_is_running (cerver) && (result != ETIMEDOUT)) {
		result = pthread_cond_timedwait (cerver->stop_cond, cerver->stop_mutex, &deadline);
	}

	(void) pthread_mutex_unlock (cerver->stop_mutex);

}

// the ids of the clients that a check dropped, they are logged after the clients lock is released,
// and a reference to the socket of each of their connections, that are shut down after it
typedef struct CerverInactiveDropped {

	u64 ids[CERVER_INACTIVE_LOG_BATCH];
	unsigned int count;
	unsigned int total;

	Socket **sockets;
	size_t sockets_count;
	size_t sockets_size;

} CerverInactiveDropped;

// returns true if there is room for one more socket
static bool cerver_inactive_sockets_room (CerverInactiveDropped *dropped) {

	bool room = (dropped->sockets_count < dropped->sockets_size);
	if (!room) {
		const size_t size = dropped->sockets_size ?
			(dropped->sockets_size << 1) : CERVER_INACTIVE_SHUTDOWN_BATCH;

		Socket **sockets = (Socket **) realloc (dropped->sockets, size * sizeof (Socket *));
		if (sockets) {
			dropped->sockets = sockets;
			dropped->sockets_size = size;
			room = true;
		}
	}

	return room;

}

// takes a reference to the socket of a connection of an inactive client,
// without memory for it the socket is shut down right away, with the clients lock
static void cerver_inactive_take_socket (void *connection_ptr, void *dropped_ptr) {

	Connection *connection = (Connection *) connection_ptr;
	CerverInactiveDropped *dropped = (CerverInactiveDropped *) dropped_ptr;

	if (connection && connection->socket) {
		if (cerver_inactive_sockets_room (dropped)) {
			dropped->sockets[dropped->sockets_count] = socket_reference (connection->socket);
			dropped->sockets_count += 1;
		}

		else {
			(void) socket_shutdown (connection->socket);
		}
	}

}

// the sockets of the inactive clients are shut down without the clients lock,
// so their receive handlers end and remove them like any other closed connection,
// the close mutex of each socket keeps a shutdown from reaching an fd that was closed and reused
static void cerver_inactive_shutdown_sockets (CerverInactiveDropped *dropped) {

	for (size_t index = 0; index < dropped->sockets_count; index++) {
		(void) socket_shutdown (dropped->sockets[index]);
		socket_delete (dropped->sockets[index]);
	}

	free (dropped->sockets);
	dropped->sockets = NULL;
	dropped->sockets_count = 0;
	dropped->sockets_size = 0;

}

// with the clients lock, only the sockets of the connections of an inactive client are taken
static void cerver_inactive_check_client (
	const Client *client, const Cerver *cerver, time_t current_time,
	CerverInactiveDropped *dropped
) {

	const time_t last_activity = __atomic_load_n (&client->last_activity, __ATOMIC_RELAXED);

	if ((current_time - last_activity) >= (time_t) cerver->max_inactive_time) {
		if (client->connections) {
			(void) dlist_traverse (client->connections, cerver_inactive_take_socket, dropped);
		}

		if (dropped->count < CERVER_INACTIVE_LOG_BATCH) {
			dropped->ids[dropped->count] = client->id;
			dropped->count += 1;
		}

		dropped->total += 1;
	}

}

// the recursion depth is the tree height, that is log (n) in an avl
// must be called with the clients tree locked
static void cerver_inactive_check (
	const AVLNode *node, const Cerver *cerver, time_t current_time,
	CerverInactiveDropped *dropped
) {

	if (node) {
		cerver_inactive_check (node->right, cerver, current_time, dropped);

		if (node->id) {
			cerver_inactive_check_client ((const Client *) node->id, cerver, current_time, dropped);
		}

		cerver_inactive_check (node->left, cerver, current_time, dropped);
	}

}

// the logs of a check are written without the clients lock
static void cerver_inactive_check_log (
	const Cerver *cerver, const CerverInactiveDropped *dropped
) {

	for (unsigned int index = 0; index < dropped->count; index++) {
		cerver_log_warning (
			"Client %lu has been inactive more than %u secs and will be dropped",
			dropped->ids[index], cerver->max_inactive_time
		);
	}

	if (dropped->total > dropped->count) {
		cerver_log_warning (
			"%u more inactive clients will be dropped from cerver %s",
			dropped->total - dropped->count, cerver->info->name
		);
	}

}

static void cerver_inactive_check_all (Cerver *cerver) {

	cerver_log_debug (
		"Checking for inactive clients in cerver %s...",
		cerver->info->name
	);

	time_t current_time = time (NULL);

	CerverInactiveDropped dropped;
	dropped.count = 0;
	dropped.total = 0;
	dropped.sockets = NULL;
	dropped.sockets_count = 0;
	dropped.sockets_size = 0;

	(void) pthread_mutex_lock (cerver->clients->mutex);

	cerver_inactive_check (cerver->clients->root, cerver, current_time, &dropped);

	(void) pthread_mutex_unlock (cerver->clients->mutex);

	cerver_inactive_shutdown_sockets (&dropped);

	cerver_inactive_check_log (cerver, &dropped);

	cerver_log_debug (
		"Done checking for inactive clients in cerver %s",
		cerver->info->name
	);

}

// 17/06/2020 - thread to check for inactive clients
static void *cerver_inactive_thread (void *args) {

	if (args) {
		Cerver *cerver = (Cerver *) args;

		while (cerver_is_running (cerver)) {
			cerver_sleep (cerver, cerver->check_inactive_interval);

			if (cerver_is_running (cerver) && cerver->clients) {
				cerver_inactive_check_all (cerver);
			}
		}
	}

	return NULL;

}

static u8 cerver_start_inactive (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_log_debug (
			"Cerver %s is set to check for inactive clients with max time of <%u> secs every <%u> secs",
			cerver->info->name,
			cerver->max_inactive_time,
			cerver->check_inactive_interval
		);

		if (!thread_create_joinable (
			&cerver->inactive_thread_id,
			cerver_inactive_thread,
			cerver
		)) {
			cerver_log_success (
				"Created cerver %s INACTIVE thread!",
				cerver->info->name
			);

			retval = 0;
		}

		else {
			cerver_log_error (
				"Failed to create cerver %s INACTIVE thread!",
				cerver->info->name
			);
		}
	}

	return retval;

}

static u8 cerver_start_tcp (Cerver *cerver) {

	u8 retval = 1;

	switch (cerver->handler_type) {
		case CERVER_HANDLER_TYPE_NONE: {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver's %s handler type has NOT been configured!",
				cerver->info->name
			);
		} break;

		case CERVER_HANDLER_TYPE_POLL: {
			if (!cerver->blocking) {
				if (!listen (cerver->sock, cerver->connection_queue)) {
					// register the cerver start time
					time (&cerver->info->time_started);

					// set up the initial listening socket
					cerver->fds[cerver->current_n_fds].fd = cerver->sock;
					cerver->fds[cerver->current_n_fds].events = POLLIN;
					cerver->current_n_fds++;

					cerver_event_trigger (
						CERVER_EVENT_STARTED,
						cerver,
						NULL, NULL
					);

					retval = cerver_poll (cerver);
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_CERVER,
						"Failed to listen in cerver %s socket!",
						cerver->info->name
					);

					cerver_close_socket (cerver);
				}
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Can't start cerver %s in CERVER_HANDLER_TYPE_POLL - socket is NOT set to non blocking!",
					cerver->info->name
				);
			}
		} break;

		case CERVER_HANDLER_TYPE_THREADS: {
			if (cerver->blocking) {
				if (!listen (cerver->sock, cerver->connection_queue)) {
					// register the cerver start time
					time (&cerver->info->time_started);

					cerver_event_trigger (
						CERVER_EVENT_STARTED,
						cerver,
						NULL, NULL
					);

					retval = cerver_threads (cerver);
				}

				else {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_CERVER,
						"Failed to listen in cerver %s socket!",
						cerver->info->name
					);

					cerver_close_socket (cerver);
				}
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"Can't start cerver %s in CERVER_HANDLER_TYPE_THREADS - socket is NOT blocking!",
					cerver->info->name
				);
			}
		} break;

		default: {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Cerver's %s handler type is invalid!",
				cerver->info->name
			);
		} break;
	}

	return retval;

}

// every datagram is a packet, the peers are logical connections (udp.c)
// the admins have connections of their own, they can not connect to an UDP cerver
static u8 cerver_start_udp (Cerver *cerver) {

	if (cerver->admin) {
		cerver_log_error (
			"Cerver %s can not handle admins with UDP!",
			cerver->info->name
		);

		return 1;
	}

	(void) time (&cerver->info->time_started);

	cerver_event_trigger (
		CERVER_EVENT_STARTED,
		cerver,
		NULL, NULL
	);

	return cerver_udp_start (cerver);

}

// tell the cerver to start listening for connections and packets
// a stop requested before cerver_start (), or while it was setting the running flag, cancels it
// cerver_stop_request () sets stop_requested before it clears the running flag,
// so with sequential consistency every order of the two ends with the cerver stopped
static bool cerver_start_cancelled (Cerver *cerver) {

	const bool cancelled = __atomic_load_n (&cerver->stop_requested, __ATOMIC_SEQ_CST);

	if (cancelled) {
		cerver_set_running (cerver, false);

		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"The cerver %s was stopped before it started!",
			cerver->info->name
		);
	}

	return cancelled;

}

// initializes cerver's structures like thpool (if any)
// and any other processes that have been configured before
// a cerver is started only once, a stop requested before cancels it
// returns 0 on success, 1 on error
u8 cerver_start (Cerver *cerver) {

	u8 retval = 1;

	if (!cerver_is_running (cerver)) {
		cerver_set_running (cerver, true);

		if (cerver_start_cancelled (cerver)) return 0;

		cerver_start_ignore_sigpipe (cerver);

		if (!cerver_one_time_init (cerver)) {
			u8 errors = 0;

			errors |= cerver_handlers_start (cerver);

			if (cerver->auth_required) {
				cerver_log_debug (
					"Cerver %s requires authentication",
					cerver->info->name
				);

				errors |= cerver_auth_start (cerver);
			}

			if (cerver->use_sessions) {
				cerver_log_debug (
					"Cerver %s supports sessions",
					cerver->info->name
				);
			}

			// start the admin cerver
			if (cerver->admin) {
				cerver_log_debug (
					"Cerver %s can handle admins",
					cerver->info->name
				);

				errors |= admin_cerver_start (cerver->admin);
			}

			if (cerver->update) {
				errors |= cerver_update_start (cerver);
			}

			if (cerver->update_interval) {
				errors |= cerver_update_interval_start (cerver);
			}

			// 17/06/2020
			// check for inactive will be handled in its own thread
			// to avoid messing with other dedicated update threads timings
			if (cerver->inactive_clients) {
				errors |= cerver_start_inactive (cerver);
			}

			if (!errors) {
				switch (cerver->protocol) {
					case PROTOCOL_TCP: {
						retval = cerver_start_tcp (cerver);
					} break;

					case PROTOCOL_UDP: {
						retval = cerver_start_udp (cerver);
					} break;

					default: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_CERVER,
							"Cant't start cerver %s! Unknown protocol!",
							cerver->info->name
						);
					}
					break;
				}
			}
		}

		else {
			cerver_log_error (
				"Failed cerver_one_time_init () for cerver %s!",
				cerver->info->name
			);
		}

		// the accept loop has ended or the cerver failed to start
		cerver_stop_request (cerver);
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"The cerver %s is already running!",
			cerver->info->name
		);
	}

	return retval;

}

#pragma endregion
