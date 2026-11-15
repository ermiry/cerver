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
#include "cerver/core/update.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region main

AdminCerver *admin_cerver_new (void) {

	AdminCerver *admin_cerver = (AdminCerver *) malloc (sizeof (AdminCerver));
	if (admin_cerver) {
		admin_cerver->cerver = NULL;

		admin_cerver->admins = NULL;

		admin_cerver->authenticate = NULL;

		admin_cerver->max_admins = ADMIN_CERVER_DEFAULT_MAX_ADMINS;
		admin_cerver->max_admin_connections = ADMIN_CERVER_DEFAULT_MAX_ADMIN_CONNECTIONS;

		admin_cerver->n_bad_packets_limit = ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS;

		admin_cerver->receive_buffer_size = ADMIN_CERVER_DEFAULT_RECEIVE_BUFFER_SIZE;

		admin_cerver->fds = NULL;
		admin_cerver->max_n_fds = ADMIN_CERVER_DEFAULT_POLL_FDS;
		admin_cerver->current_n_fds = 0;
		admin_cerver->poll_timeout = ADMIN_CERVER_DEFAULT_POLL_TIMEOUT;
		admin_cerver->poll_lock = NULL;

		admin_cerver->app_packet_handler = NULL;
		admin_cerver->app_error_packet_handler = NULL;
		admin_cerver->custom_packet_handler = NULL;

		admin_cerver->num_handlers_alive = 0;
		admin_cerver->num_handlers_working = 0;
		admin_cerver->handlers_lock = NULL;

		admin_cerver->app_packet_handler_delete_packet = true;
		admin_cerver->app_error_packet_handler_delete_packet = true;
		admin_cerver->custom_packet_handler_delete_packet = true;

		admin_cerver->check_packets = ADMIN_CERVER_DEFAULT_CHECK_PACKETS;

		admin_cerver->update_thread_id = 0;
		admin_cerver->update = NULL;
		admin_cerver->update_args = NULL;
		admin_cerver->delete_update_args = NULL;
		admin_cerver->update_ticks = ADMIN_CERVER_DEFAULT_UPDATE_TICKS;

		admin_cerver->update_interval_thread_id = 0;
		admin_cerver->update_interval = NULL;
		admin_cerver->update_interval_args = NULL;
		admin_cerver->delete_update_interval_args = NULL;
		admin_cerver->update_interval_secs = ADMIN_CERVER_DEFAULT_UPDATE_INTERVAL_SECONDS;

		admin_cerver->stats = NULL;
	}

	return admin_cerver;

}

void admin_cerver_delete (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		dlist_delete (admin_cerver->admins);

		if (admin_cerver->fds) free (admin_cerver->fds);

		if (admin_cerver->poll_lock) {
			pthread_mutex_destroy (admin_cerver->poll_lock);
			free (admin_cerver->poll_lock);
		}

		handler_delete (admin_cerver->app_packet_handler);
		handler_delete (admin_cerver->app_error_packet_handler);
		handler_delete (admin_cerver->custom_packet_handler);

		if (admin_cerver->handlers_lock) {
			pthread_mutex_destroy (admin_cerver->handlers_lock);
			free (admin_cerver->handlers_lock);
		}

		admin_cerver_stats_delete (admin_cerver->stats);

		free (admin_cerver);
	}

}

AdminCerver *admin_cerver_create (void) {

	AdminCerver *admin_cerver = admin_cerver_new ();
	if (admin_cerver) {
		admin_cerver->admins = dlist_init (admin_delete, admin_comparator_with_id);

		admin_cerver->stats = admin_cerver_stats_new ();
	}

	return admin_cerver;

}

// sets the authentication methods to be used to successfully authenticate admin credentials
// must return 0 on success, 1 on error
void admin_cerver_set_authenticate (
	AdminCerver *admin_cerver, delegate authenticate
) {

	if (admin_cerver) admin_cerver->authenticate = authenticate;

}

// sets the max numbers of admins allowed at any given time
void admin_cerver_set_max_admins (
	AdminCerver *admin_cerver, u8 max_admins
) {

	if (admin_cerver) admin_cerver->max_admins = max_admins;

}

// sets the max number of connections allowed per admin
void admin_cerver_set_max_admin_connections (
	AdminCerver *admin_cerver, u8 max_admin_connections
) {

	if (admin_cerver) admin_cerver->max_admin_connections = max_admin_connections;

}

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
void admin_cerver_set_bad_packets_limit (
	AdminCerver *admin_cerver, i32 n_bad_packets_limit
) {

	if (admin_cerver) {
		admin_cerver->n_bad_packets_limit = n_bad_packets_limit > 0 ?
			n_bad_packets_limit : ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS;
	}

}

// sets the admin cerver's receive buffer size used for recv ()
void admin_cerver_set_receive_buffer_size (
	AdminCerver *admin_cerver, const size_t buffer_size
) {

	if (admin_cerver) {
		admin_cerver->receive_buffer_size = buffer_size;
	}

}

// sets the max number of poll fds for the admin cerver
void admin_cerver_set_max_fds (
	AdminCerver *admin_cerver, u32 max_n_fds
) {

	// the poll array is created with the cerver, after it the size can not change
	if (admin_cerver && max_n_fds && !admin_cerver->fds) admin_cerver->max_n_fds = max_n_fds;

}

// sets a custom poll time out to use for admins
void admin_cerver_set_poll_timeout (
	AdminCerver *admin_cerver, u32 poll_timeout
) {

	if (admin_cerver) admin_cerver->poll_timeout = poll_timeout;

}

// sets customs PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packet types handlers
void admin_cerver_set_app_handlers (
	AdminCerver *admin_cerver, Handler *app_handler, Handler *app_error_handler
) {

	if (admin_cerver) {
		admin_cerver->app_packet_handler = app_handler;
		if (admin_cerver->app_packet_handler) {
			admin_cerver->app_packet_handler->type = HANDLER_TYPE_ADMIN;
			admin_cerver->app_packet_handler->cerver = admin_cerver->cerver;
		}

		admin_cerver->app_error_packet_handler = app_error_handler;
		if (admin_cerver->app_error_packet_handler) {
			admin_cerver->app_error_packet_handler->type = HANDLER_TYPE_ADMIN;
			admin_cerver->app_error_packet_handler->cerver = admin_cerver->cerver;
		}
	}

}

// sets option to automatically delete PACKET_TYPE_APP packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void admin_cerver_set_app_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
) {

	if (admin_cerver) admin_cerver->app_packet_handler_delete_packet = delete_packet;

}

// sets option to automatically delete PACKET_TYPE_APP_ERROR packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void admin_cerver_set_app_error_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
) {

	if (admin_cerver) admin_cerver->app_error_packet_handler_delete_packet = delete_packet;

}

// sets a PACKET_TYPE_CUSTOM packet type handler
void admin_cerver_set_custom_handler (
	AdminCerver *admin_cerver, Handler *custom_handler
) {

	if (admin_cerver) {
		admin_cerver->custom_packet_handler = custom_handler;
		if (admin_cerver->custom_packet_handler) {
			admin_cerver->custom_packet_handler->type = HANDLER_TYPE_ADMIN;
			admin_cerver->custom_packet_handler->cerver = admin_cerver->cerver;
		}
	}

}

// sets option to automatically delete PACKET_TYPE_CUSTOM packets after use
// if set to false, user must delete the packets manualy
// by the default, packets are deleted by cerver
void admin_cerver_set_custom_handler_delete (
	AdminCerver *admin_cerver, bool delete_packet
) {

	if (admin_cerver) admin_cerver->custom_packet_handler_delete_packet = delete_packet;

}

// returns the total number of handlers currently alive (ready to handle packets)
unsigned int admin_cerver_get_n_handlers_alive (
	AdminCerver *admin_cerver
) {

	unsigned int retval = 0;

	if (admin_cerver) {
		(void) pthread_mutex_lock (admin_cerver->handlers_lock);
		retval = admin_cerver->num_handlers_alive;
		(void) pthread_mutex_unlock (admin_cerver->handlers_lock);
	}

	return retval;

}

// returns the total number of handlers currently working (handling a packet)
unsigned int admin_cerver_get_n_handlers_working (
	AdminCerver *admin_cerver
) {

	unsigned int retval = 0;

	// the handlers count themselves with __atomic while they handle a packet
	if (admin_cerver) {
		retval = __atomic_load_n (&admin_cerver->num_handlers_working, __ATOMIC_SEQ_CST);
	}

	return retval;

}

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
void admin_cerver_set_check_packets (
	AdminCerver *admin_cerver, bool check_packets
) {

	if (admin_cerver) {
		admin_cerver->check_packets = check_packets;
	}

}

// sets a custom update function to be executed every n ticks
// a new thread will be created that will call your method each tick
// the update args will be passed to your method as a CerverUpdate &
// will only be deleted at cerver teardown if you set the delete_update_args ()
void admin_cerver_set_update (
	AdminCerver *admin_cerver,
	Action update, void *update_args, void (*delete_update_args)(void *),
	const u8 fps
) {

	if (admin_cerver) {
		admin_cerver->update = update;
		admin_cerver->update_args = update_args;
		admin_cerver->delete_update_args = delete_update_args;
		admin_cerver->update_ticks = fps;
	}

}

// sets a custom update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
// the update args will be passed to your method as a CerverUpdate &
// will only be deleted at cerver teardown if you set the delete_update_args ()
void admin_cerver_set_update_interval (
	AdminCerver *admin_cerver,
	Action update, void *update_args, void (*delete_update_args)(void *),
	const u32 interval
) {

	if (admin_cerver) {
		admin_cerver->update_interval = update;
		admin_cerver->update_interval_args = update_args;
		admin_cerver->delete_update_interval_args = delete_update_args;
		admin_cerver->update_interval_secs = interval;
	}

}

// returns the current number of connected admins
u8 admin_cerver_get_current_admins (AdminCerver *admin_cerver) {

	return admin_cerver ? (u8) dlist_size (admin_cerver->admins) : 0;

}

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins (
	AdminCerver *admin_cerver, Packet *packet
) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		// the admins are registered and removed by other threads
		(void) pthread_mutex_lock (admin_cerver->admins->mutex);

		u8 errors = 0;
		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			errors |= admin_send_packet ((Admin *) le->data, packet);
		}

		(void) pthread_mutex_unlock (admin_cerver->admins->mutex);

		retval = errors;
	}

	return retval;

}

// broadcasts a packet to all connected admins in an admin cerver using packet_send_to_split ()
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins_split (
	AdminCerver *admin_cerver, Packet *packet
) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		(void) pthread_mutex_lock (admin_cerver->admins->mutex);

		u8 errors = 0;

		for (
			ListElement *le = dlist_start (admin_cerver->admins);
			le; le = le->next
		) {
			errors |= admin_send_packet_split ((Admin *) le->data, packet);
		}

		(void) pthread_mutex_unlock (admin_cerver->admins->mutex);

		retval = errors;
	}

	return retval;

}

// broadcasts a packet to all connected admins in an admin cerver using packet_send_pieces ()
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins_pieces (
	AdminCerver *admin_cerver, Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces
) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		(void) pthread_mutex_lock (admin_cerver->admins->mutex);

		u8 errors = 0;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			errors |= admin_send_packet_pieces (
				(Admin *) le->data, packet, pieces, sizes, n_pieces
			);
		}

		(void) pthread_mutex_unlock (admin_cerver->admins->mutex);

		retval = errors;
	}

	return retval;

}

// registers a newly created admin to the admin cerver structures (internal & poll)
// this will allow the admin cerver to start handling admin's packets
// returns 0 on success, 1 on error
u8 admin_cerver_register_admin (
	AdminCerver *admin_cerver, Admin *admin
) {

	u8 retval = 1;

	if (admin_cerver && admin) {
		if (!admin_cerver_poll_register_connection (
			admin_cerver,
			(Connection *) dlist_start (admin->client->connections)->data
		)) {
			(void) dlist_insert_at_end (admin_cerver->admins, admin);

			admin->admin_cerver = admin_cerver;

			admin_cerver->stats->current_connected_admins += 1;
			admin_cerver->stats->total_n_admins += 1;

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connected admins: %lu",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connected_admins
			);
			#endif

			retval = 0;     // success
		}
	}

	return retval;

}

// unregisters an existing admin from the admin cerver structures (internal & poll)
// returns 0 on success, 1 on error
u8 admin_cerver_unregister_admin (
	AdminCerver *admin_cerver, Admin *admin
) {

	u8 retval = 1;

	if (admin_cerver && admin) {
		if (dlist_remove (admin_cerver->admins, admin, NULL)) {
			// unregister all his active connections from the poll array
			for (
				ListElement *le = dlist_start (admin->client->connections);
				le; le = le->next
			) {
				admin_cerver_poll_unregister_connection (
					admin_cerver, (Connection *) le->data
				);
			}

			admin->admin_cerver = NULL;

			admin_cerver->stats->current_connected_admins -= 1;

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_ADMIN,
				"Cerver %s ADMIN current connected admins: %lu",
				admin_cerver->cerver->info->name,
				admin_cerver->stats->current_connected_admins
			);
			#endif

			retval = 0;
		}
	}

	return retval;

}

// unregisters an admin from the cerver & then deletes it
// returns 0 on success, 1 on error
u8 admin_cerver_drop_admin (
	AdminCerver *admin_cerver, Admin *admin
) {

	u8 retval = 1;

	if (admin_cerver && admin) {
		retval = admin_cerver_unregister_admin (admin_cerver, admin);
		admin_delete (admin);
	}

	return retval;

}

#pragma endregion
