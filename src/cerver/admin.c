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

#include "cerver/threads/thread.h"
#include "cerver/threads/bsem.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

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

void admin_cerver_stats_print (AdminCerverStats *stats) {

    if (stats) {
        printf ("threshold_time: %ld\n", stats->threshold_time);
        
        printf ("\n");
        printf ("Admin n packets received:               %ld\n", stats->admin_n_packets_received);
        printf ("Admin receives done:                    %ld\n", stats->admin_receives_done);
        printf ("Admin bytes received:                   %ld\n", stats->admin_bytes_received);

        printf ("\n");
        printf ("Total n receives done:                  %ld\n", stats->total_n_receives_done);
        printf ("Total n packets received:               %ld\n", stats->total_n_packets_received);
        printf ("Total bytes received:                   %ld\n", stats->total_bytes_received);

        printf ("\n");
        printf ("Total n packets sent:                   %ld\n", stats->total_n_packets_sent);
        printf ("Total bytes sent:                       %ld\n", stats->total_bytes_sent);

        printf ("\n");
        printf ("Current active admin connections:       %ld\n", stats->current_active_admin_connections);
        printf ("Current n connected admins:             %ld\n", stats->current_n_connected_admins);

        printf ("\n");
        printf ("Current auth active admin connections:  %ld\n", stats->current_auth_active_admin_connections);
        printf ("Current auth n connected admins:        %ld\n", stats->current_auth_n_connected_admins);

        printf ("\n");
        printf ("Total n admins:                         %ld\n", stats->total_n_admins);
        printf ("Unique admins:                          %ld\n", stats->unique_admins);
        printf ("Total admin connections:                %ld\n", stats->total_admin_connections);

        printf ("\nReceived packets:\n");
        packets_per_type_print (stats->received_packets);

        printf ("\nSent packets:\n");
        packets_per_type_print (stats->sent_packets);
    }

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

// sends a packet to the first connection of the specified admin
// returns 0 on success, 1 on error
u8 admin_send_packet (Admin *admin, Packet *packet) {

	u8 retval = 1;

	if (admin && packet) {
		if (admin->authenticated) {
			// printf ("admin client: %ld -- sock fd: %d\n", admin->client->id, ((Connection *) dlist_start (admin->client->connections))->sock_fd);

			packet_set_network_values (
				packet, 
				NULL, 
				admin->client, 
				(Connection *) dlist_start (admin->client->connections)->data, 
				NULL
			);

			retval = packet_send (packet, 0, NULL, false);
			if (retval) cerver_log_error ("Failed to send packet to admin!");
		}
	}

	return retval;

}

#pragma endregion

#pragma region main

AdminCerver *admin_cerver_new (void) {

	AdminCerver *admin_cerver = (AdminCerver *) malloc (sizeof (AdminCerver));
	if (admin_cerver) {
		admin_cerver->cerver = NULL;

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

		admin_cerver->stats = NULL;
	}

	return admin_cerver;

}

void admin_cerver_delete (AdminCerver *admin_cerver) {

	if (admin_cerver) {
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

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
u8 admin_cerver_broadcast_to_admins (AdminCerver *admin_cerver, Packet *packet) {

	u8 retval = 1;

	if (admin_cerver && packet) {
		u8 errors = 0;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			errors |= admin_send_packet ((Admin *) le->data, packet);
		}

		retval = errors;
	}

	return retval;

}

#pragma endregion

#pragma region start

static void *admin_poll (void *cerver_ptr);

// inits admin cerver's internal structures & values
static u8 admin_cerver_start_internal (AdminCerver *admin_cerver) {

    u8 retval = 1;

    if (admin_cerver) {
		admin_cerver->fds = (struct pollfd *) calloc (admin_cerver->max_n_fds, sizeof (struct pollfd));
		if (admin_cerver->fds) {
			memset (admin_cerver->fds, 0, sizeof (struct pollfd) * admin_cerver->max_n_fds);

			for (u32 i = 0; i < admin_cerver->max_n_fds; i++)
				admin_cerver->fds[i].fd = -1;

			admin_cerver->current_n_fds = 0;
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
                    char *s = c_string_create ("Admin cerver %s app_packet_handler has started!",
                        admin_cerver->cerver->info->name->str);
                    if (s) {
                        cerver_log_success (s);
                        free (s);
                    }
                    #endif
                }

                else {
                    char *s = c_string_create ("Failed to start admin cerver %s app_packet_handler!",
                        admin_cerver->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
			#ifdef ADMIN_DEBUG
            char *s = c_string_create ("Cerver %s does not have an app_packet_handler",
				admin_cerver->cerver->info->name->str);
			if (s) {
				cerver_log_warning (s);
				free (s);
			}
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
                    char *s = c_string_create ("Admin cerver %s app_error_packet_handler has started!",
                        admin_cerver->cerver->info->name->str);
                    if (s) {
                        cerver_log_success (s);
                        free (s);
                    }
                    #endif
                }

                else {
                    char *s = c_string_create ("Failed to start admin cerver %s app_error_packet_handler!",
                        admin_cerver->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
			#ifdef ADMIN_DEBUG
            char *s = c_string_create ("Cerver %s does not have an app_error_packet_handler",
				admin_cerver->cerver->info->name->str);
			if (s) {
				cerver_log_warning (s);
				free (s);
			}
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
                    char *s = c_string_create ("Admin cerver %s custom_packet_handler has started!",
                        admin_cerver->cerver->info->name->str);
                    if (s) {
                        cerver_log_success (s);
                        free (s);
                    }
                    #endif
                }

                else {
                    char *s = c_string_create ("Failed to start admin cerver %s custom_packet_handler!",
                        admin_cerver->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
			#ifdef ADMIN_DEBUG
            char *s = c_string_create ("Cerver %s does not have a custom_packet_handler",
				admin_cerver->cerver->info->name->str);
			if (s) {
				cerver_log_warning (s);
				free (s);
			}
			#endif
        }
    }

    return retval;

}

static u8 admin_cerver_handlers_start (AdminCerver *admin_cerver) {

    u8 errors = 0;

    if (admin_cerver) {
        #ifdef ADMIN_DEBUG
        char *s = c_string_create ("Initializing cerver %s admin handlers...",
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
            #ifdef ADMIN_DEBUG
            s = c_string_create ("Done initializing cerver %s admin handlers!",
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

static u8 admin_cerver_start_poll (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		if (!thread_create_detachable (
		    &cerver->admin_thread_id,
		    admin_poll,
		    cerver
		)) {
		    retval = 0;		// success
		}

		else {
			char *s = c_string_create ("Failed to create admin_poll () thread in cerver %s!",
		        cerver->info->name->str);
		    if (s) {
		        cerver_log_error (s);
		        free (s);
		    }
		}
	}

	return retval;

}

u8 admin_cerver_start (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		if (!admin_cerver_start_internal (cerver->admin)) {
			if (!admin_cerver_handlers_start (cerver->admin)) {
				if (!admin_cerver_start_poll (cerver)) {
					retval = 0;
				}
			}

			else {
				char *status = c_string_create ("admin_cerver_start () - failed to start cerver %s admin handlers!",
					cerver->info->name->str);
				if (status) {
					cerver_log_error (status);
					free (status);
				}
			}
		}

		else {
			char *status = c_string_create ("admin_cerver_start () - failed to start cerver %s admin internal!",
				cerver->info->name->str);
			if (status) {
				cerver_log_error (status);
				free (status);
			}
		}
	}

	return retval;

}

#pragma endregion

#pragma region end

static u8 admin_cerver_app_handler_destroy (AdminCerver *admin_cerver) {

    u8 errors = 0;

    if (admin_cerver) {
        if (admin_cerver->app_packet_handler) {
            if (!admin_cerver->app_packet_handler->direct_handle) {
                // stop app handler
                bsem_post_all (admin_cerver->app_packet_handler->job_queue->has_jobs);
            }
        }
    }

    return errors;

}

static u8 admin_cerver_app_error_handler_destroy (AdminCerver *admin_cerver) {

    u8 errors = 0;

    if (admin_cerver) {
        if (admin_cerver->app_error_packet_handler) {
            if (!admin_cerver->app_error_packet_handler->direct_handle) {
                // stop app error handler
                bsem_post_all (admin_cerver->app_error_packet_handler->job_queue->has_jobs);
            }
        }
    }

    return errors;

}

static u8 admin_cerver_custom_handler_destroy (AdminCerver *admin_cerver) {

    u8 errors = 0;

    if (admin_cerver) {
        if (admin_cerver->custom_packet_handler) {
            if (!admin_cerver->custom_packet_handler->direct_handle) {
                // stop custom handler
                bsem_post_all (admin_cerver->custom_packet_handler->job_queue->has_jobs);
            }
        }
    }

    return errors;

}

// correctly destroy admin cerver's custom handlers
static u8 admin_cerver_handlers_end (AdminCerver *admin_cerver) {

    u8 errors = 0;

    if (admin_cerver) {
        #ifdef ADMIN_DEBUG
        char *status = c_string_create ("Stopping handlers in cerver %s admin...",
            admin_cerver->cerver->info->name->str);
        if (status) {
            cerver_log_debug (status);
            free (status);
        }
        #endif

        errors |= admin_cerver_app_handlers_destroy (admin_cerver);

        errors |= admin_cerver_app_error_handler_destroy (admin_cerver);

        errors |= admin_cerver_custom_handler_destroy (admin_cerver);

        // poll remaining handlers
        while (admin_cerver->num_handlers_alive) {
            if (admin_cerver->app_packet_handler)
                bsem_post_all (admin_cerver->app_packet_handler->job_queue->has_jobs);

            if (admin_cerver->app_error_packet_handler)
                bsem_post_all (admin_cerver->app_error_packet_handler->job_queue->has_jobs);

            if (admin_cerver->custom_packet_handler)
                bsem_post_all (admin_cerver->custom_packet_handler->job_queue->has_jobs);
            
            sleep (1);
        }
    }

    return errors;

}

static u8 admin_cerver_disconnect_admins (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		if (dlist_size (admin_cerver->admins)) {
			// send a cerver teardown packet to all clients connected to cerver
            Packet *packet = packet_generate_request (CERVER_PACKET, CERVER_TEARDOWN, NULL, 0);
            if (packet) {
                errors |= admin_cerver_broadcast_to_admins (admin_cerver, packet);
                packet_delete (packet);
            }
		}
	}

	return errors;

}

u8 admin_cerver_end (AdminCerver *admin_cerver) {

	u8 errors = 0;

	if (admin_cerver) {
		char *status = NULL;

		#ifdef ADMIN_DEBUG
		status = c_string_create ("Staring cerver %s admin teardown...",
			admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_debug (status);
			free (status);
		}
		#endif

		errors |= admin_cerver_handlers_end (admin_cerver);

		errors |= admin_cerver_disconnect_admins (admin_cerver);

		status = c_string_create ("Cerver %s admin teardown was successful!",
			admin_cerver->cerver->info->name->str);
		if (status) {
			cerver_log_success (status);
			free (status);
		}
	}

	return errors;

}

#pragma endregion

#pragma region handler

// handles an APP_PACKET packet type
static void admin_app_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->cerver->admin->app_packet_handler) {
            if (packet->cerver->admin->app_packet_handler->direct_handle) {
                // printf ("app_packet_handler - direct handle!\n");
                packet->cerver->admin->app_packet_handler->handler (packet);
                if (packet->cerver->admin->app_packet_handler_delete_packet) packet_delete (packet);
            }

            else {
                // add the packet to the handler's job queueu to be handled
                // as soon as the handler is available
                if (job_queue_push (
                    packet->cerver->admin->app_packet_handler->job_queue,
                    job_create (NULL, packet)
                )) {
                    char *s = c_string_create ("Failed to push a new job to cerver's %s ADMIN app_packet_handler!",
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            #ifdef ADMIN_DEBUG
            char *s = c_string_create ("Cerver %s ADMIN does not have an app_packet_handler!",
                packet->cerver->info->name->str);
            if (s) {
                cerver_log_warning (s);
                free (s);
            }
            #endif
        }
    }

}

// handles an APP_ERROR_PACKET packet type
static void admin_app_error_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->cerver->admin->app_error_packet_handler) {
            if (packet->cerver->admin->app_error_packet_handler->direct_handle) {
                // printf ("app_error_packet_handler - direct handle!\n");
                packet->cerver->admin->app_error_packet_handler->handler (packet);
                if (packet->cerver->admin->app_error_packet_handler_delete_packet) packet_delete (packet);
            }

            else {
                // add the packet to the handler's job queueu to be handled
                // as soon as the handler is available
                if (job_queue_push (
                    packet->cerver->admin->app_error_packet_handler->job_queue,
                    job_create (NULL, packet)
                )) {
                    char *s = c_string_create ("Failed to push a new job to cerver's %s ADMIN app_error_packet_handler!",
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            #ifdef ADMIN_DEBUG
            char *s = c_string_create ("Cerver %s ADMIN does not have an app_error_packet_handler!",
                packet->cerver->info->name->str);
            if (s) {
                cerver_log_warning (s);
                free (s);
            }
            #endif
        }
    }

}

// handles a CUSTOM_PACKET packet type
static void admin_custom_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->cerver->admin->custom_packet_handler) {
            if (packet->cerver->admin->custom_packet_handler->direct_handle) {
                // printf ("custom_packet_handler - direct handle!\n");
                packet->cerver->admin->custom_packet_handler->handler (packet);
                if (packet->cerver->admin->custom_packet_handler_delete_packet) packet_delete (packet);
            }

            else {
                // add the packet to the handler's job queueu to be handled
                // as soon as the handler is available
                if (job_queue_push (
                    packet->cerver->admin->custom_packet_handler->job_queue,
                    job_create (NULL, packet)
                )) {
                    char *s = c_string_create ("Failed to push a new job to cerver's %s ADMIN custom_packet_handler!",
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            #ifdef ADMIN_DEBUG
            char *s = c_string_create ("Cerver %s ADMIN does not have an custom_packet_handler!",
                packet->cerver->info->name->str);
            if (s) {
                cerver_log_warning (s);
                free (s);
            }
            #endif
        }
    }

}

// TODO: handle stats
// handles a packet from an admin
void admin_packet_handler (Packet *packet) {

    if (packet) {
        if (!packet_check (packet)) {
            switch (packet->header->packet_type) {
                case APP_PACKET: admin_app_packet_handler (packet); break;

                case APP_ERROR_PACKET: admin_app_error_packet_handler (packet); break;

                case CUSTOM_PACKET: admin_custom_packet_handler (packet); break;

                default: {
                    // packet->cerver->stats->received_packets->n_bad_packets += 1;
                    // packet->client->stats->received_packets->n_bad_packets += 1;
                    // packet->connection->stats->received_packets->n_bad_packets += 1;
                    #ifdef ADMIN_DEBUG
                    char *s = c_string_create ("Got a packet of unknown type in cerver %s admin handler", 
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, s);
                        free (s);
                    }
                    #endif
                    packet_delete (packet);
                } break;
            }
        }
    }

}

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

        #ifdef ADMIN_DEBUG
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