#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <poll.h>

#include "cerver/types/types.h"

#include "cerver/collections/dllist.h"

#include "cerver/cerver.h"
#include "cerver/handler.h"
#include "cerver/packets.h"

#pragma region stats

static AdminCerverStats *admin_cerver_stats_new (void) {

    AdminCerverStats *admin_cerver_stats = (AdminCerverStats *) malloc (sizeof (AdminCerverStats));
    if (admin_cerver_stats) {
        memset (admin_cerver_stats, 0, sizeof (AdminCerverStats));
        admin_cerver_stats->received_packets = packets_per_type_new ();
        admin_cerver_stats->sent_packets = packets_per_type_new ();
    } 

    return admin_cerver_stats;

}

static void admin_cerver_stats_delete (AdminCerverStats *admin_cerver_stats) {

    if (admin_cerver_stats) {
        packets_per_type_delete (admin_cerver_stats->received_packets);
        packets_per_type_delete (admin_cerver_stats->sent_packets);
        
        free (admin_cerver_stats);
    } 

}

#pragma endregion

#pragma region credentials



#pragma endregion

#pragma region main

AdminCerver *admin_cerver_new (void) {

	AdminCerver *admin_cerver = (AdminCerver *) malloc (sizeof (AdminCerver));
	if (admin_cerver) {
		admin_cerver->cerver = NULL;

		admin_cerver->credentials = NULL;

		admin_cerver->admins = NULL;

		admin_cerver->max_admins = DEFAULT_MAX_ADMINS;
		admin_cerver->max_admin_connections = DEFAULT_MAX_ADMIN_CONNECTIONS;

		admin_cerver->n_bad_packets_limit = DEFAULT_N_BAD_PACKETS_LIMIT;
		admin_cerver->n_bad_packets_limit_auth = DEFAULT_N_BAD_PACKETS_LIMIT_AUTH;

		admin_cerver->fds = NULL;
		admin_cerver->max_n_fds = DEFAULT_ADMIN_MAX_N_FDS;
		admin_cerver->current_n_fds = 0;
		admin_cerver->poll_timeout = DEFAULT_ADMIN_POLL_TIMEOUT;

		admin_cerver->on_admin_fail_connection = NULL;
		admin_cerver->on_admin_success_connection = NULL;

		admin_cerver->app_packet_handler = NULL;
		admin_cerver->app_error_packet_handler = NULL;
		admin_cerver->custom_packet_handler = NULL;

		admin_cerver->app_packet_handler_delete_packet = true;
		admin_cerver->app_error_packet_handler_delete_packet = true;
		admin_cerver->custom_packet_handler_delete_packet = true;

		admin_cerver->update_thread_id = 0;

		admin_cerver->stats = NULL;
	}

	return admin_cerver;

}

void admin_cerver_delete (AdminCerver *admin_cerver) {

	if (admin_cerver) {
		dlist_delete (admin_cerver->credentials);

		dlist_delete (admin_cerver->admins);

		if (admin_cerver->fds) free (admin_cerver->fds);

		handler_delete (admin_cerver->app_packet_handler);
        handler_delete (admin_cerver->app_error_packet_handler);
        handler_delete (admin_cerver->custom_packet_handler);

		admin_cerver_stats_delete (admin_cerver->stats);

		free (admin_cerver);
	}

}

// FIXME:
AdminCerver *admin_cerver_create (void) {

	AdminCerver *admin_cerver = admin_cerver_new ();
	if (admin_cerver) {
		admin_cerver->credentials = dlist_init (NULL, NULL);

		admin_cerver->admins = dlist_init (NULL, NULL);

		admin_cerver->stats = admin_cerver_stats_new ();
	}

	return admin_cerver;

}

// sets the max numbers of admins allowed at any given time
void admin_cerver_set_max_admins (AdminCerver *admin_cerver, u8 max_admins) {

	if (admin_cerver) admin_cerver->max_admins = max_admins;

}

// sets the max number of connections allowed per admin
void admin_cerver_set_max_admin_connections (AdminCerver *admin_cerver, u8 max_admin_connections) {

	if (admin_cerver) admin_cerver->max_admin_connections = max_admin_connections;

}

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
void admin_cerver_set_bad_packets_limit (AdminCerver *admin_cerver, 
	i32 n_bad_packets_limit, i32 n_bad_packets_limit_auth) {

	if (admin_cerver) {
		admin_cerver->n_bad_packets_limit = n_bad_packets_limit > 0 ? n_bad_packets_limit : DEFAULT_N_BAD_PACKETS_LIMIT;
		admin_cerver->n_bad_packets_limit_auth = n_bad_packets_limit_auth > 0 ? n_bad_packets_limit : DEFAULT_N_BAD_PACKETS_LIMIT_AUTH;
	}

}

// sets a custom poll time out to use for admins
void admin_cerver_set_poll_timeout (AdminCerver *admin_cerver, u32 poll_timeout) {

	if (admin_cerver) admin_cerver->poll_timeout = poll_timeout;

}

// sets an action to be performed when a new admin failed to authenticate
void admin_cerver_set_on_fail_connection (AdminCerver *admin_cerver, Action on_fail_connection) {

	if (admin_cerver) admin_cerver->on_admin_fail_connection = on_fail_connection;

}

// sets an action to be performed when a new admin authenticated successfully
// a struct _OnAdminConnection will be passed as the argument
void admin_cerver_set_on_success_connection (AdminCerver *admin_cerver, Action on_success_connection) {

	if (admin_cerver) admin_cerver->on_admin_success_connection = on_success_connection;

}

// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
void admin_cerver_set_app_handlers (AdminCerver *admin_cerver, Handler *app_handler, Handler *app_error_handler) {

    if (admin_cerver) {
        admin_cerver->app_packet_handler = app_handler;
        if (admin_cerver->app_packet_handler) {
            admin_cerver->app_packet_handler->type = HANDLER_TYPE_CERVER;
            admin_cerver->app_packet_handler->cerver = admin_cerver->cerver;
        }

        admin_cerver->app_error_packet_handler = app_error_handler;
        if (admin_cerver->app_error_packet_handler) {
            admin_cerver->app_error_packet_handler->type = HANDLER_TYPE_CERVER;
            admin_cerver->app_error_packet_handler->cerver = admin_cerver->cerver;
        }
    }

}

// sets option to automatically delete APP_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
void admin_cerver_set_app_handler_delete (AdminCerver *admin_cerver, bool delete_packet) {

    if (admin_cerver) admin_cerver->app_packet_handler_delete_packet = delete_packet;

}

// sets option to automatically delete APP_ERROR_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
void admin_cerver_set_app_error_handler_delete (AdminCerver *admin_cerver, bool delete_packet) {

    if (admin_cerver) admin_cerver->app_error_packet_handler_delete_packet = delete_packet;

}

// sets a CUSTOM_PACKET packet type handler
void admin_cerver_set_custom_handler (AdminCerver *admin_cerver, Handler *custom_handler) {

    if (admin_cerver) {
        admin_cerver->custom_packet_handler = custom_handler;
        if (admin_cerver->custom_packet_handler) {
            admin_cerver->custom_packet_handler->type = HANDLER_TYPE_CERVER;
            admin_cerver->custom_packet_handler->cerver = admin_cerver->cerver;
        }
    }

}

// sets option to automatically delete CUSTOM_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
void admin_cerver_set_custom_handler_delete (AdminCerver *admin_cerver, bool delete_packet) {

    if (admin_cerver) admin_cerver->custom_packet_handler_delete_packet = delete_packet;

}

#pragma endregion