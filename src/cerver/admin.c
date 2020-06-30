#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/dllist.h"

#include "cerver/admin.h"
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

AdminCredentials *admin_credentials_new (void) {

	AdminCredentials *credentials = (AdminCredentials *) malloc (sizeof (AdminCredentials));
	if (credentials) {
		credentials->username = NULL;
		credentials->password = NULL;
		credentials->logged_in = false;
	}

	return credentials;

}

void admin_credentials_delete (void *credentials_ptr) {

	if (credentials_ptr) {
		AdminCredentials *credentials = (AdminCredentials *) credentials_ptr;

		estring_delete (credentials->username);
		estring_delete (credentials->password);

		free (credentials);
	}

}

static int admin_credentials_comparator (const void *a, const void *b) {

	if (a && b) return strcmp (((AdminCredentials *) a)->username->str, ((AdminCredentials *) b)->username->str);

	else if (a && !b) return -1;
	else if (!a && b) return 1;
	return 0;

}

static AdminCredentials *admin_credentials_create (const char *username, const char *password) {

	AdminCredentials *admin_credentials = admin_credentials_new ();
	if (admin_credentials) {
		admin_credentials->username = username ? estring_new (username) : NULL;
		admin_credentials->password = password ? estring_new (password) : NULL;
	}

	return admin_credentials;

}

#pragma endregion

#pragma region admin

Admin *admin_new (void) {

	Admin *admin = (Admin *) malloc (sizeof (Admin));
	if (admin) {
		admin->id = NULL;

		admin->client = NULL;

		admin->data = NULL;
		admin->delete_data = NULL;

		admin->authenticated = false;
		admin->credentials = NULL;

		admin->bad_packets = 0;
	}

	return admin;

}

void admin_delete (void *admin_ptr) {

	if (admin_ptr) {
		Admin *admin = (Admin *) admin_ptr;

		estring_delete (admin->id);

		client_delete (admin->client);

		if (admin->data) {
			if (admin->delete_data) admin->delete_data (admin->data);
		}

		free (admin);
	}

}

Admin *admin_create_with_client (Client *client) {

	Admin *admin = NULL;

	if (client) {
		admin = admin_new ();
		if (admin) admin->client = client;
	}

	return client;

}

int admin_comparator_by_id (const void *a, const void *b) {

	if (a && b) return strcmp (((Admin *) a)->id->str, ((Admin *) b)->id->str);

	else if (a && !b) return -1;
	else if (!a && b) return 1;
	return 0;

}

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
void admin_set_data (Admin *admin, void *data, Action delete_data) {

	if (admin) {
		admin->data = data;
		admin->delete_data = delete_data;
	}

}

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

		admin_cerver->num_handlers_alive = 0;
		admin_cerver->num_handlers_working = 0;
		admin_cerver->handlers_lock = NULL;

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
		admin_cerver->credentials = dlist_init (admin_credentials_delete, admin_credentials_comparator);

		admin_cerver->admins = dlist_init (admin_delete, admin_comparator_by_id);

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

// sets the max number of poll fds for the admin cerver
void admin_cerver_set_max_fds (AdminCerver *admin_cerver, u32 max_n_fds) {

	if (admin_cerver) admin_cerver->max_n_fds = max_n_fds;

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

// registers new admin credentials
// returns 0 on success, 1 on error
u8 admin_cerver_register_admin_credentials (AdminCerver *admin_cerver,
	const char *username, const char *password) {

	u8 retval = 1;

	if (admin_cerver && username && password) {
		retval = dlist_insert_after (
			admin_cerver->credentials,
			dlist_end (admin_cerver->credentials),
			admin_credentials_create (
				username,
				password
			)
		);
	}

	return retval;

}

// removes a registered admin credentials
AdminCredentials *admin_cerver_unregister_admin_credentials (AdminCerver *admin_cerver, 
	const char *username) {

	AdminCredentials *retval = NULL;

	if (admin_cerver) {
		AdminCredentials *query = admin_credentials_create (username, NULL);
		if (query) {
			retval = (AdminCredentials *) dlist_remove (
				admin_cerver->credentials,
				query,
				NULL
			);
			
			admin_credentials_delete (query);
		}
	}

	return retval;

}

#pragma endregion

#pragma region start

static void *admin_poll (void *cerver_ptr);

static u8 admin_cerver_handlers_start (AdminCerver *admin_cerver) {

    u8 errors = 0;

    if (admin_cerver) {
        #ifdef CERVER_DEBUG
        char *s = c_string_create ("Initializing %s admin handlers...",
            admin_cerver->cerver->info->name->str);
        if (s) {
            cerver_log_debug (s);
            free (s);
        }
        #endif

        admin_cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
        pthread_mutex_init (admin_cerver->handlers_lock, NULL);

        errors |= admin_cerver_app_handlers_start (admin_cerver);

        errors |= admin_cerver_app_error_handler_start (admin_cerver);

        errors |= admin_cerver_custom_handler_start (admin_cerver);

        if (!errors) {
            #ifdef CERVER_DEBUG
            s = c_string_create ("Done initializing %s admin handlers!",
                admin_cerver->cerver->info->name->str);
            if (s) {
                cerver_log_debug (s);
                free (s);
            }
            #endif
        }
    }

    return errors;

}

u8 admin_cerver_start (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		if (thread_create_detachable (
		    &cerver->admin_thread_id,
		    admin_poll,
		    cerver->admin
		)) {
		    char *s = c_string_create ("Failed to create admin_poll () thread in cerver %s",
		        cerver->info->name->str);
		    if (s) {
		        cerver_log_error (s);
		        free (s);
		    }
		}
	}

	return retval;

}

#pragma endregion

#pragma region end

#pragma endregion

#pragma region poll

static inline void admin_poll_handle (Cerver *cerver) {

    if (cerver) {
        // one or more fd(s) are readable, need to determine which ones they are
        for (u32 i = 0; i < cerver->admin->max_n_fds; i++) {
            if (cerver->fds[i].fd > -1) {
				// FIXME: pass admin type to both methods
                Socket *socket = socket_get_by_fd (cerver, cerver->fds[i].fd, true);
                CerverReceive *cr = cerver_receive_new (cerver, socket, true, NULL);

                switch (cerver->hold_fds[i].revents) {
                    // A connection setup has been completed or new data arrived
                    case POLLIN: {
                        // printf ("Receive fd: %d\n", cerver->fds[i].fd);
                            
                        // if (cerver->thpool) {
                            // pthread_mutex_lock (socket->mutex);

                            // handle received packets using multiple threads
                            // if (thpool_add_work (cerver->thpool, cerver_receive, cr)) {
                            //     char *s = c_string_create ("Failed to add cerver_receive () to cerver's %s thpool!", 
                            //         cerver->info->name->str);
                            //     if (s) {
                            //         cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                            //         free (s);
                            //     }
                            // }

                            // 28/05/2020 -- 02:43 -- handling all recv () calls from the main thread
                            // and the received buffer handler method is the one that is called 
                            // inside the thread pool - using this method we were able to get a correct behaviour
                            // however, we still may have room form improvement as we original though ->
                            // by performing reading also inside the thpool
                            // cerver_receive (cr);

                            // pthread_mutex_unlock (socket->mutex);
                        // }

                        // else {
                            // handle all received packets in the same thread
                            cerver_receive (cr);
                        // }
                    } break;

                    default: {
                        if (cerver->fds[i].revents != 0) {
                            // 17/06/2020 -- 15:06 -- handle as failed any other signal
                            // to avoid hanging up at 100% or getting a segfault
							// FIXME:
                            // cerver_switch_receive_handle_failed (cr);
                        }
                    } break;
                }
            }
        }
    }

}

// handles packets from the admin clients
static void *admin_poll (void *cerver_ptr) {

    if (cerver_ptr) {
        Cerver *cerver = (Cerver *) cerver_ptr;
		AdminCerver *admin_cerver = cerver->admin;

        char *status = c_string_create ("Admin cerver %s poll has started!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, status);
            free (status);
        }

        char *thread_name = c_string_create ("%s-admin", cerver->info->name->str);
        if (thread_name) {
            thread_set_name (thread_name);
            free (thread_name);
        }

        int poll_retval = 0;
        while (cerver->isRunning) {
            poll_retval = poll (
				admin_cerver->fds,
				admin_cerver->max_n_fds,
				admin_cerver->poll_timeout
			);

            switch (poll_retval) {
                case -1: {
                    char *status = c_string_create ("Admin cerver %s poll has failed!", cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                        free (status);
                    }

                    perror ("Error");
                    cerver->isRunning = false;
                } break;

                case 0: {
                    // #ifdef CERVER_DEBUG
                    // char *status = c_string_create ("Admin cerver %s poll timeout", cerver->info->name->str);
                    // if (status) {
                    //     cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                    //     free (status);
                    // }
                    // #endif
                } break;

                default: {
                    admin_poll_handle (cerver);
                } break;
            }
        }

        #ifdef CERVER_DEBUG
        status = c_string_create ("Admin cerver %s poll has stopped!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
            free (status);
        }
        #endif
    }

    else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Can't handle admins on a NULL cerver!");

    return NULL;

}

#pragma endregion