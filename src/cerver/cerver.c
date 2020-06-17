#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#include <poll.h>
#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/dllist.h"
#include "cerver/collections/avl.h"
#include "cerver/collections/pool.h"

#include "cerver/admin.h"
#include "cerver/auth.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/thpool.h"

#include "cerver/game/game.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static void cerver_clean (Cerver *cerver);

#pragma region info

static CerverInfo *cerver_info_new (void) {

    CerverInfo *cerver_info = (CerverInfo *) malloc (sizeof (CerverInfo));
    if (cerver_info) {
        memset (cerver_info, 0, sizeof (CerverInfo));
        cerver_info->name = NULL;
        cerver_info->welcome_msg = NULL;
        cerver_info->cerver_info_packet = NULL;
    }

    return cerver_info;

}

static void cerver_info_delete (CerverInfo *cerver_info) {

    if (cerver_info) {
        estring_delete (cerver_info->name);
        estring_delete (cerver_info->welcome_msg);
        packet_delete (cerver_info->cerver_info_packet);

        free (cerver_info);
    }

}

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
u8 cerver_set_welcome_msg (Cerver *cerver, const char *msg) {

    if (cerver) {
        if (cerver->info) {
            estring_delete (cerver->info->welcome_msg);
            cerver->info->welcome_msg = msg ? estring_new (msg) : NULL;
            return 0;
        }
    }

    return 1;

}

#pragma endregion

#pragma region stats

static CerverStats *cerver_stats_new (void) {

    CerverStats *cerver_stats = (CerverStats *) malloc (sizeof (CerverStats));
    if (cerver_stats) {
        memset (cerver_stats, 0, sizeof (CerverStats));
        cerver_stats->received_packets = packets_per_type_new ();
        cerver_stats->sent_packets = packets_per_type_new ();
    } 

    return cerver_stats;

}

static void cerver_stats_delete (CerverStats *cerver_stats) {

    if (cerver_stats) {
        packets_per_type_delete (cerver_stats->received_packets);
        packets_per_type_delete (cerver_stats->sent_packets);
        
        free (cerver_stats);
    } 

}

// sets the cerver stats threshold time (how often the stats get reset)
void cerver_stats_set_threshold_time (Cerver *cerver, time_t threshold_time) {

    if (cerver) {
        if (cerver->stats) cerver->stats->threshold_time = threshold_time;
    }

}

void cerver_stats_print (Cerver *cerver) {

    if (cerver) {
        if (cerver->stats) {
            printf ("\nCerver's %s stats: ", cerver->info->name->str);
            printf ("Threshold time:                %ld\n", cerver->stats->threshold_time);

            if (cerver->auth_required) {
                printf ("Client packets received:       %ld\n", cerver->stats->client_n_packets_received);
                printf ("Client receives done:          %ld\n", cerver->stats->client_receives_done);
                printf ("Client bytes received:         %ld\n\n", cerver->stats->client_bytes_received);

                printf ("On hold packets received:       %ld\n", cerver->stats->on_hold_n_packets_received);
                printf ("On hold receives done:          %ld\n", cerver->stats->on_hold_receives_done);
                printf ("On hold bytes received:         %ld\n\n", cerver->stats->on_hold_bytes_received);
            }

            printf ("\n");
            printf ("Total packets received:        %ld\n", cerver->stats->total_n_packets_received);
            printf ("Total receives done:           %ld\n", cerver->stats->total_n_receives_done);
            printf ("Total bytes received:          %ld\n\n", cerver->stats->total_bytes_received);

            printf ("\n");
            printf ("N packets sent:                %ld\n", cerver->stats->n_packets_sent);
            printf ("Total bytes sent:              %ld\n", cerver->stats->total_bytes_sent);

            printf ("\n");
            printf ("Current active client connections:         %ld\n", cerver->stats->current_active_client_connections);
            printf ("Current connected clients:                 %ld\n", cerver->stats->current_n_connected_clients);
            printf ("Current on hold connections:               %ld\n", cerver->stats->current_n_hold_connections);
            printf ("Total clients:                             %ld\n", cerver->stats->total_n_clients);
            printf ("Unique clients:                            %ld\n", cerver->stats->unique_clients);
            printf ("Total client connections:                  %ld\n", cerver->stats->total_client_connections);

            printf ("\nReceived packets:\n");
            packets_per_type_print (cerver->stats->received_packets);

            printf ("\nSent packets:\n");
            packets_per_type_print (cerver->stats->sent_packets);
        }

        else {
            char *status = c_string_create ("Cerver %s does not have a reference to cerver stats!",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
        }
    }

    else {
        cerver_log_msg (stderr, LOG_WARNING, LOG_CERVER, 
            "Cant print stats of a NULL cerver!");
    }

}

#pragma endregion

#pragma region main

Cerver *cerver_new (void) {

    Cerver *c = (Cerver *) malloc (sizeof (Cerver));
    if (c) {
        memset (c, 0, sizeof (Cerver));

        c->protocol = PROTOCOL_TCP;         // default protocol
        c->use_ipv6 = false;
        c->connection_queue = DEFAULT_CONNECTION_QUEUE;
        c->receive_buffer_size = RECEIVE_PACKET_BUFFER_SIZE;

        c->isRunning = false;
        c->blocking = true;

        c->cerver_data = NULL;
        c->delete_cerver_data = NULL;

        c->n_thpool_threads = 0;
        c->thpool = NULL;

        c->sockets_pool_init = DEFAULT_SOCKETS_INIT;
        c->sockets_pool = NULL;

        c->clients = NULL;
        c->client_sock_fd_map = NULL;
        c->on_client_connected = NULL;

        c->inactive_clients = false;

        c->fds = NULL;
        c->poll_lock = NULL;

        c->auth_required = false;
        c->auth = NULL;

        c->on_hold_connections = NULL;
        c->on_hold_connection_sock_fd_map = NULL;
        c->hold_fds = NULL;
        c->holding_connections = false;

        c->use_sessions = false;
        c->session_id_generator = NULL;

        c->handle_received_buffer = NULL;

        c->app_packet_handler = NULL;
        c->app_error_packet_handler = NULL;
        c->custom_packet_handler = NULL;

        // 10/05/2020
        c->handlers = NULL;
        c->handlers_lock = NULL;

        c->check_packets = false;

        c->update = NULL;
        c->update_args = NULL;

        c->update_interval = NULL;
        c->update_interval_args = NULL;

        c->admin = NULL;

        c->info = NULL;
        c->stats = NULL;
    }

    return c;

}

void cerver_delete (void *ptr) {

    if (ptr) {
        Cerver *cerver = (Cerver *) ptr;

        if (cerver->cerver_data) {
            if (cerver->delete_cerver_data) cerver->delete_cerver_data (cerver->cerver_data);
            else free (cerver->cerver_data);
        }

        pool_delete (cerver->sockets_pool);

        if (cerver->clients) avl_delete (cerver->clients);
        if (cerver->client_sock_fd_map) htab_destroy (cerver->client_sock_fd_map);

        if (cerver->fds) free (cerver->fds);

        // 28/05/2020
        if (cerver->poll_lock) {
            pthread_mutex_destroy (cerver->poll_lock);
            free (cerver->poll_lock);
        }

        if (cerver->auth) auth_delete (cerver->auth);

        if (cerver->on_hold_connections) avl_delete (cerver->on_hold_connections);
        if (cerver->on_hold_connection_sock_fd_map) htab_destroy (cerver->on_hold_connection_sock_fd_map);
        if (cerver->hold_fds) free (cerver->hold_fds);

        // 27/05/2020
        handler_delete (cerver->app_packet_handler);
        handler_delete (cerver->app_error_packet_handler);
        handler_delete (cerver->custom_packet_handler);

        // 10/05/2020
        if (cerver->handlers) {
            for (unsigned int idx = 0; idx < cerver->n_handlers; idx++) {
                handler_delete (cerver->handlers[idx]);
            }

            free (cerver->handlers);
        }

        if (cerver->handlers_lock) {
            pthread_mutex_destroy (cerver->handlers_lock);
            free (cerver->handlers_lock);
        }

        admin_cerver_delete (cerver->admin);

        cerver_info_delete (cerver->info);
        cerver_stats_delete (cerver->stats);

        free (cerver);
    }

}

// sets the cerver main network values
void cerver_set_network_values (Cerver *cerver, const u16 port, const Protocol protocol,
    bool use_ipv6, const u16 connection_queue) {

    if (cerver) {
        cerver->port = port;
        cerver->protocol = protocol;
        cerver->use_ipv6 = use_ipv6;
        cerver->connection_queue = connection_queue;
    }

}

// sets the cerver connection queue (how many connections to queue for accept)
void cerver_set_connection_queue (Cerver *cerver, const u16 connection_queue) {

    if (cerver) cerver->connection_queue = connection_queue;

}

// sets the cerver's receive buffer size used in recv method
void cerver_set_receive_buffer_size (Cerver *cerver, const u32 size) {

    if (cerver) cerver->receive_buffer_size = size;

}

// sets the cerver's data and a way to free it
void cerver_set_cerver_data (Cerver *cerver, void *data, Action delete_data) {

    if (cerver) {
        cerver->cerver_data = data;
        cerver->delete_cerver_data = delete_data;
    }

}

// sets the cerver's thpool number of threads
// this will enable the cerver's ability to handle received packets using multiple threads
// usefull if you want the best concurrency and effiency
// but we aware that you need to make your structures and data thread safe, as they might be accessed 
// from multiple threads at the same time
// by default, all received packets will be handle only in one thread
void cerver_set_thpool_n_threads (Cerver *cerver, u16 n_threads) {

    if (cerver) cerver->n_thpool_threads = n_threads;

}

// sets the initial number of sockets to be created in the cerver's sockets pool
// the defauult value is 10
void cerver_set_sockets_pool_init (Cerver *cerver, unsigned int n_sockets) {

    if (cerver) cerver->sockets_pool_init = n_sockets;

}

// sets an action to be performed by the cerver when a new client connects
void cerver_set_on_client_connected  (Cerver *cerver, Action on_client_connected) {

    if (cerver) cerver->on_client_connected = on_client_connected;

}

// 17/06/2020
// enables the ability to check for inactive clients - clients that have not been sent or received from a packet in x time
// will be automatically dropped from the cerver
// max_inactive_time - max secs allowed for a client to be inactive, 0 for default
// check_inactive_interval - how often to check for inactive clients in secs, 0 for default
void cerver_set_inactive_clients (Cerver *cerver, u32 max_inactive_time, u32 check_inactive_interval) {

    if (cerver) {
        cerver->inactive_clients = true;
        cerver->max_inactive_time = max_inactive_time ? max_inactive_time : DEFAULT_MAX_INACTIVE_TIME;
        cerver->check_inactive_interval = check_inactive_interval ? check_inactive_interval : DEFAULT_CHECK_INACTIVE_INTERVAL;
    }

}

// sets the cerver poll timeout
void cerver_set_poll_time_out (Cerver *cerver, const u32 poll_timeout) {

    if (cerver) cerver->poll_timeout = poll_timeout;

}

// init on hold client on hold structures and values
static u8 cerver_on_hold_init (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver->on_hold_connections = avl_init (connection_comparator, connection_delete);
        cerver->on_hold_connection_sock_fd_map = htab_create (poll_n_fds / 2, NULL, NULL);
        if (cerver->on_hold_connections && cerver->on_hold_connection_sock_fd_map) {
            cerver->max_on_hold_connections = poll_n_fds;
            cerver->hold_fds = (struct pollfd *) calloc (cerver->max_on_hold_connections, sizeof (struct pollfd));
            if (cerver->hold_fds) {
                memset (cerver->hold_fds, 0, sizeof (struct pollfd) * cerver->max_on_hold_connections);
                cerver->max_on_hold_connections = poll_n_fds;
                cerver->current_n_fds = 0;
                for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
                    cerver->hold_fds[i].fd = -1;

                cerver->holding_connections = false;

                cerver->current_on_hold_nfds = 0;

                retval = 0;
            }
        }
    }

    return retval;

}

// configures the cerver to require client authentication upon new client connections
// retuns 0 on success, 1 on error
u8 cerver_set_auth (Cerver *cerver, u8 max_auth_tries, delegate authenticate) {

    u8 retval = 1;

    if (cerver) {
        cerver->auth = auth_new ();
        if (cerver->auth) {
            cerver->auth->max_auth_tries = max_auth_tries;
            cerver->auth->authenticate = authenticate;
            cerver->auth->auth_packet = auth_packet_generate ();

            if (!cerver_on_hold_init (cerver)) {
                cerver->auth_required = true;
                retval = 0;
            } 
        }
    }

    return retval;

}

// configures the cerver to use client sessions
// retuns 0 on success, 1 on error
u8 cerver_set_sessions (Cerver *cerver, void *(*session_id_generator) (const void *)) {

    u8 retval = 1;

    if (cerver) {
        if (session_id_generator) {
            cerver->session_id_generator = session_id_generator;
            cerver->use_sessions = true;

            avl_set_comparator (cerver->clients, client_comparator_session_id);
        }
    }

    return retval;

}

// sets a custom method to handle the raw received buffer from the socker
void cerver_set_handle_recieved_buffer (Cerver *cerver, Action handle_received_buffer) {

    if (cerver) cerver->handle_received_buffer = handle_received_buffer;

}

// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
void cerver_set_app_handlers (Cerver *cerver, Handler *app_handler, Handler *app_error_handler) {

    if (cerver) {
        cerver->app_packet_handler = app_handler;
        if (cerver->app_packet_handler) {
            cerver->app_packet_handler->type = HANDLER_TYPE_CERVER;
            cerver->app_packet_handler->cerver = cerver;
        }

        cerver->app_error_packet_handler = app_error_handler;
        if (cerver->app_error_packet_handler) {
            cerver->app_error_packet_handler->type = HANDLER_TYPE_CERVER;
            cerver->app_error_packet_handler->cerver = cerver;
        }
    }

}

// sets a CUSTOM_PACKET packet type handler
void cerver_set_custom_handler (Cerver *cerver, Handler *custom_handler) {

    if (cerver) {
        cerver->custom_packet_handler = custom_handler;
        if (cerver->custom_packet_handler) {
            cerver->custom_packet_handler->type = HANDLER_TYPE_CERVER;
            cerver->custom_packet_handler->cerver = cerver;
        }
    }

}

// enables the ability of the cerver to have multiple app handlers
// returns 0 on success, 1 on error
int cerver_set_multiple_handlers (Cerver *cerver, unsigned int n_handlers) {

    int retval = 1;

    if (cerver) {
        cerver->multiple_handlers = true;
        cerver->n_handlers = n_handlers;

        cerver->handlers = (Handler **) calloc (cerver->n_handlers, sizeof (Handler *));
        if (cerver->handlers) {
            for (unsigned int idx = 0; idx < cerver->n_handlers; idx++)
                cerver->handlers[idx] = NULL;

            // 27/05/2020 -- moved to cerver_handlers_start ()
            // cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
            // pthread_mutex_init (cerver->handlers_lock, NULL);

            retval = 0;
        }
    }

    return retval;

}

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
void cerver_set_check_packets (Cerver *cerver, bool check_packets) {

    if (cerver) {
        cerver->check_packets = check_packets;
    }

}

// sets a custom cerver update function to be executed every n ticks
// a new thread will be created that will call your method each tick
void cerver_set_update (Cerver *cerver, Action update, void *update_args, const u8 fps) {

    if (cerver) {
        cerver->update = update;
        cerver->update_args = update_args;
        cerver->update_ticks = fps;
    }

}

// sets a custom cerver update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
void cerver_set_update_interval (Cerver *cerver, Action update, void *update_args, u32 interval) {

    if (cerver) {
        cerver->update_interval = update;
        cerver->update_interval_args = update_args;
        cerver->update_interval_secs = interval;
    }

}

// enables admin connections to cerver
// admin connections are handled in a different port and using a dedicated handler
// returns 0 on success, 1 on error
u8 cerver_admin_enable (Cerver *cerver, u16 port, bool use_ipv6) {

    u8 retval = 1;

    if (cerver) {
        cerver->admin = admin_cerver_create (port, use_ipv6);
        if (cerver->admin) {
            cerver->admin->cerver = cerver;
            retval = 0;
        }
    }

    return retval;

}

#pragma endregion

#pragma region sockets

static int cerver_sockets_pool_init (Cerver *cerver) {

    int retval = 1;

    if (cerver) {
        cerver->sockets_pool = pool_create (socket_delete);
        if (cerver->sockets_pool) {
            retval = pool_init (
                cerver->sockets_pool, 
                socket_create_empty, 
                cerver->sockets_pool_init
            );
        }
    }

    return retval;

}

int cerver_sockets_pool_push (Cerver *cerver, Socket *socket) {

    int retval = 1;
    
    if (cerver && socket) {
        retval = pool_push (cerver->sockets_pool, socket);
        // printf ("push!\n");
    }

    return retval;

}

Socket *cerver_sockets_pool_pop (Cerver *cerver) {

    Socket *retval = NULL;

    if (cerver) {
        void *value = pool_pop (cerver->sockets_pool);
        if (value) retval = (Socket *) value;
        // printf ("pop!\n");
    }

    return retval;

}

static void cerver_sockets_pool_end (Cerver *cerver) {

    if (cerver) {
        pool_delete (cerver->sockets_pool);
        cerver->sockets_pool = NULL;
    }

}

#pragma endregion

#pragma region handlers

// prints info about current handlers
void cerver_handlers_print_info (Cerver *cerver) {

    if (cerver) {
        char *status = NULL;

        status = c_string_create ("%d - current ACTIVE handlers in cerver %s",
            cerver->num_handlers_alive, cerver->info->name->str);
        if (status) {
            cerver_log_debug (status);
            free (status);
        }

        status = c_string_create ("%d - current WORKING handlers in cerver %s",
            cerver->num_handlers_working, cerver->info->name->str);
        if (status) {
            cerver_log_debug (status);
            free (status);
        }
    }

}

// adds a new handler to the cerver handlers array
// is the responsability of the user to provide a unique handler id, which must be < cerver->n_handlers
// returns 0 on success, 1 on error
int cerver_handlers_add (Cerver *cerver, Handler *handler) {

    int retval = 1;

    if (cerver && handler) {
        if (cerver->handlers) {
            cerver->handlers[handler->id] = handler;

            handler->type = HANDLER_TYPE_CERVER;
            handler->cerver = cerver;
        }
    }

    return retval;

}

// correctly destroys multiple app handlers, if any
static u8 cerver_multiple_app_handlers_destroy (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        if (cerver->handlers && (cerver->num_handlers_alive > 0)) {
            char *status = NULL;

            #ifdef CERVER_DEBUG
            status = c_string_create ("Stopping multiple app handlers in cerver %s...",
                cerver->info->name->str);
            if (status) {
                cerver_log_debug (status);
                free (status);
            }
            #endif

            #ifdef CERVER_DEBUG
            cerver_handlers_print_info (cerver);
            #endif

            // give x timeout to kill idle handlers
            double timeout = 1.0;
            time_t start = 0, end = 0;
            double time_passed = 0.0;
            time (&start);
            while (time_passed < timeout && cerver->num_handlers_alive) {
                for (unsigned int i = 0; i < cerver->n_handlers; i++) {
                    bsem_post_all (cerver->handlers[i]->job_queue->has_jobs);
                    time (&end);
                    time_passed = difftime (end, start);
                }
            }

            // poll remaining handlers
            while (cerver->num_handlers_alive) {
                for (unsigned int i = 0; i < cerver->n_handlers; i++) {
                    bsem_post_all (cerver->handlers[i]->job_queue->has_jobs);
                    sleep (1);
                }
            }

            retval = 0;
        }
    }

    return retval;

}

static u8 cerver_app_handlers_destroy (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        if (cerver->multiple_handlers) {
            if (cerver->handlers && (cerver->num_handlers_alive > 0)) {
                if (!cerver_multiple_app_handlers_destroy (cerver)) {
                    #ifdef CERVER_DEBUG
                    char *status = c_string_create ("Done destroying multiple app handlers in cerver %s!",
                        cerver->info->name->str);
                    if (status) {
                        cerver_log_success (status);
                        free (status);
                    }
                    #endif
                }

                else {
                    char *status = c_string_create ("Failed to destroy multiple app handlers in cerver %s!",
                        cerver->info->name->str);
                    if (status) {
                        cerver_log_error (status);
                        free (status);
                    }
                }
            }
        }

        else {
            if (cerver->app_packet_handler) {
                if (!cerver->app_packet_handler->direct_handle) {
                    // stop app handler
                    bsem_post_all (cerver->app_packet_handler->job_queue->has_jobs);
                }
            }
        }
    }

    return errors;

}

static u8 cerver_app_error_handler_destroy (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        if (cerver->app_error_packet_handler) {
            if (!cerver->app_error_packet_handler->direct_handle) {
                // stop app error handler
                bsem_post_all (cerver->app_error_packet_handler->job_queue->has_jobs);
            }
        }
    }

    return errors;

}

static u8 cerver_custom_handler_destroy (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        if (cerver->custom_packet_handler) {
            if (!cerver->custom_packet_handler->direct_handle) {
                // stop custom handler
                bsem_post_all (cerver->custom_packet_handler->job_queue->has_jobs);
            }
        }
    }

    return errors;

}

// 27/05/2020
// correctly destroy cerver's custom handlers
static u8 cerver_handlers_destroy (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        #ifdef CERVER_DEBUG
        char *status = c_string_create ("Stopping handlers in cerver %s...",
            cerver->info->name->str);
        if (status) {
            cerver_log_debug (status);
            free (status);
        }
        #endif

        errors |= cerver_app_handlers_destroy (cerver);

        errors |= cerver_app_error_handler_destroy (cerver);

        errors |= cerver_custom_handler_destroy (cerver);

        // poll remaining handlers
        while (cerver->num_handlers_alive) {
            if (cerver->app_packet_handler)
                bsem_post_all (cerver->app_packet_handler->job_queue->has_jobs);

            if (cerver->app_error_packet_handler)
                bsem_post_all (cerver->app_error_packet_handler->job_queue->has_jobs);

            if (cerver->custom_packet_handler)
                bsem_post_all (cerver->custom_packet_handler->job_queue->has_jobs);
            
            sleep (1);
        }
    }

    return errors;

}

#pragma endregion

// inits the cerver networking capabilities
static u8 cerver_network_init (Cerver *cerver) {

    if (cerver) {
        // init the cerver with the selected protocol
        switch (cerver->protocol) {
            case IPPROTO_TCP: 
                cerver->sock = socket ((cerver->use_ipv6 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
                break;
            case IPPROTO_UDP:
                cerver->sock = socket ((cerver->use_ipv6 ? AF_INET6 : AF_INET), SOCK_DGRAM, 0);
                break;

            default: cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Unkonw protocol type!"); return 1;
        }
        
        if (cerver->sock < 0) {
            char *status = c_string_create ("Failed to create cerver %s socket!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }

            return 1;
        }

        #ifdef CERVER_DEBUG
        char *status = c_string_create ("Created cerver %s socket!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
            free (status);
        }
        #endif

        // set the socket to non blocking mode
        if (!sock_set_blocking (cerver->sock, cerver->blocking)) {
            char *status = c_string_create ("Failed to set cerver %s socket to non blocking mode!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }

            close (cerver->sock);

            return 1;
        }

        else {
            cerver->blocking = false;
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Cerver %s socket set to non blocking mode.", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                free (status);
            }
            #endif
        }

        memset (&cerver->address, 0, sizeof (struct sockaddr_storage));

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

        if ((bind (cerver->sock, (const struct sockaddr *) &cerver->address, sizeof (struct sockaddr_storage))) < 0) {
            char *status = c_string_create ("Failed to bind cerver %s socket!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }

            close (cerver->sock);

            return 1;
        }  

        return 0;       // success!!
    }

    return 1; 

}

static u8 cerver_init_data_structures (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver->clients = avl_init (client_comparator_client_id, client_delete);
        if (!cerver->clients) {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to init clients avl in cerver %s",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
            #endif

            return 1;
        }

        cerver->client_sock_fd_map = htab_create (poll_n_fds, NULL, NULL);
        if (!cerver->client_sock_fd_map) {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to init clients sock fd map in cerver %s",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
            #endif

            return 1;
        }

        // initialize main pollfd structures
        cerver->fds = (struct pollfd *) calloc (poll_n_fds, sizeof (struct pollfd));
        if (cerver->fds) {
            memset (cerver->fds, 0, sizeof (struct pollfd) * poll_n_fds);
            // set all fds as available spaces
            for (u32 i = 0; i < poll_n_fds; i++) cerver->fds[i].fd = -1;

            cerver->max_n_fds = poll_n_fds;
            cerver->current_n_fds = 0;

            switch (cerver->type) {
                case CUSTOM_CERVER: break;
                case FILE_CERVER: break;
                case GAME_CERVER: {
                    cerver->cerver_data = game_new ();
                    cerver->delete_cerver_data = game_delete;
                } break;
                case WEB_CERVER: break;
                
                default: break;
            }

            retval = 0;     // success!!
        }

        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to allocate cerver %s main fds!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, status);
                free (status);
            }
            #endif
        }
    }

    return retval;

}

// inits a cerver of a given type
static u8 cerver_init (Cerver *cerver) {

    int retval = 1;

    if (cerver) {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Initializing cerver...");
        #endif

        if (!cerver_network_init (cerver)) {
            if (!cerver_init_data_structures (cerver)) {
                // set the default receive handler
                cerver_set_handle_recieved_buffer (cerver, cerver_receive_handle_buffer);

                #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                    "Done initializing cerver data structures and values!");
                #endif

                retval = 0;     // success!!
            }

            else {
                char *status = c_string_create ("Failed to init cerver %s data structures!",
                    cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                    free (status);
                }
            } 
        }

        else {
            char *status = c_string_create ("Failed to init cerver %s network!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
        }
    }

    return retval;

}

// returns a new cerver with the specified parameters
Cerver *cerver_create (const CerverType type, const char *name, 
    const u16 port, const Protocol protocol, bool use_ipv6,
    u16 connection_queue, u32 poll_timeout) {

    Cerver *cerver = NULL;

    if (name) {
        cerver = cerver_new ();
        if (cerver) {
            cerver->type = type;
            cerver->info = cerver_info_new ();
            cerver->info->name = estring_new (name);
            cerver->stats = cerver_stats_new ();

            cerver_set_network_values (cerver, port, protocol, use_ipv6, connection_queue);
            cerver_set_poll_time_out (cerver, poll_timeout);

            if (!cerver_init (cerver)) {
                char *s = c_string_create ("Initialized cerver %s!", cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stdout, LOG_SUCCESS, LOG_NO_TYPE, s);
                    free (s);
                }
            }

            else {
                char *s = c_string_create ("Failed to init cerver %s!", cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                    free (s);
                }

                cerver_teardown (cerver);
                cerver = NULL;
            }
        }
    }

    else {
        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
            "A name is required to create a new cerver!");
    } 

    return cerver;

}

// teardowns the cerver and creates a fresh new one with the same parameters
// returns 0 on success, 1 on error
u8 cerver_restart (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        char *s = c_string_create ("Restarting the cerver %s...", cerver->info->name->str);
        if (s) {
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, s);
            free (s);
        }

        cerver_clean (cerver);      // clean the cerver's data structures

        if (!cerver_init (cerver)) {
            char *s = c_string_create ("Initialized cerver %s!", cerver->info->name->str);
            if (s) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_NO_TYPE, s);
                free (s);
            }
        }

        else {
            char *s = c_string_create ("Failed to init cerver %s!", cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                free (s);
            }

            cerver_delete (cerver);
            cerver = NULL;
        }
    }

    return retval;

}

static u8 cerver_one_time_init_thpool (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        if (cerver->n_thpool_threads) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Cerver %s is configured to use a thpool with %d threads",
                cerver->info->name->str, cerver->n_thpool_threads);
            if (s) {
                cerver_log_debug (s);
                free (s);
            }
            #endif

            cerver->thpool = thpool_create (cerver->n_thpool_threads);
            thpool_set_name (cerver->thpool, cerver->info->name->str);
            if (thpool_init (cerver->thpool)) {
                char *s = c_string_create ("Failed to init cerver %s thpool!", cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                    free (s);
                }

                errors = 1;
            }
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Cerver %s is configured to NOT use a thpool for receive methods",
                cerver->info->name->str);
            if (s) {
                cerver_log_debug (s);
                free (s);
            }
            #endif
        }
    }

    return errors;

}

static u8 cerver_one_time_init (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        // 29/05/2020
        errors |= cerver_sockets_pool_init (cerver);

        // 28/05/2020
        cerver->poll_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
        pthread_mutex_init (cerver->poll_lock, NULL);

        // init the cerver thpool
        errors |= cerver_one_time_init_thpool (cerver);

        // if we have a game cerver, we might wanna load game data -> set by cerver admin
        if (cerver->type == GAME_CERVER) {
            GameCerver *game_data = (GameCerver *) cerver->cerver_data;
            game_data->cerver = cerver;
            if (game_data && game_data->load_game_data) {
                game_data->load_game_data (NULL);
            }

            else {
                char *s = c_string_create ("Game cerver %s doesn't have a reference to a game data!",
                    cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stdout, LOG_WARNING, LOG_GAME, s);
                    free (s);
                }
            } 
        }

        cerver->info->cerver_info_packet = cerver_packet_generate (cerver);
        if (!cerver->info->cerver_info_packet) {
            char *s = c_string_create ("Failed to generate cerver %s info packet", 
                cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                free (s);
            }

            errors |= 1;
        }
    }

    return errors;

}

static u8 cerver_start_tcp (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        if (!cerver->blocking) {
            if (!listen (cerver->sock, cerver->connection_queue)) {
                // register the cerver start time
                time (&cerver->info->time_started);

                // set up the initial listening socket     
                cerver->fds[cerver->current_n_fds].fd = cerver->sock;
                cerver->fds[cerver->current_n_fds].events = POLLIN;
                cerver->current_n_fds++;

                // cerver is not holding clients if there is not new connections
                if (cerver->auth_required) cerver->holding_connections = false;

                // 21/01/2020 -- start the admin cerver
                if (cerver->admin) {
                    if (thread_create_detachable (
                        &cerver->admin_thread_id,
                        admin_cerver_start,
                        cerver->admin
                    )) {
                        char *s = c_string_create ("Failed to create admin_cerver_start () thread in cerver %s",
                            cerver->info->name->str);
                        if (s) {
                            cerver_log_error (s);
                            free (s);
                        }
                    }
                } 

                cerver_poll (cerver);

                retval = 0;
            }

            else {
                char *s = c_string_create ("Failed to listen in cerver %s socket!",
                    cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                    free (s);
                }

                close (cerver->sock);
            }
        }

        else {
            char *s = c_string_create ("Cerver %s socket is not set to non blocking!",
                cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                free (s);
            }
        }
    }

    return retval;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static void cerver_start_udp (Cerver *cerver) { /*** TODO: ***/ }

#pragma GCC diagnostic pop

static void cerver_inactive_check (AVLNode *node, Cerver *cerver, time_t current_time) {

    cerver_inactive_check (node->right, cerver, current_time);

    // check for client inactivity
    if (node->id) {
        Client *client = (Client *) node->id;

        if ((current_time - client->last_activity) >= cerver->max_inactive_time) {
            // the client should be dropped
            char *s = c_string_create ("Client %ld has been inactive more than %d secs and should be dropped",
                client->id, cerver->max_inactive_time);
            if (s) {
                cerver_log_warning (s);
                free (s);
            }
        }
    }

    cerver_inactive_check (node->left, cerver, current_time);

}

// 17/06/2020 - thread to check for inactive clients
static void *cerver_inactive_thread (void *args) {

    if (args) {
        Cerver *cerver = (Cerver *) args;

        u32 count = 0;
        while (cerver->isRunning) {
            if (count == cerver->check_inactive_interval) {
                count = 0;

                char *s = c_string_create ("Checking for inactive clients in cerver %s...",
                    cerver->info->name->str);
                if (s) {
                    cerver_log_debug (s);
                    free (s);
                }

                time_t current_time = time (NULL);
                cerver_inactive_check (cerver->clients->root, cerver, current_time);

                s = c_string_create ("Done checking for inactive clients in cerver %s",
                    cerver->info->name->str);
                if (s) {
                    cerver_log_debug (s);
                    free (s);
                }
            }

            sleep (1);
            count++;
        }
    }

    return NULL;

}

static u8 cerver_start_inactive (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        char *s = c_string_create (
            "Cerver %s is set to check for inactive clients with max time of <%d> secs every <%d> secs",
            cerver->info->name->str,
            cerver->max_inactive_time,
            cerver->check_inactive_interval
        );
        if (s) {
            cerver_log_debug (s);
            free (s);
        }

        if (!thread_create_detachable (
            &cerver->inactive_thread_id,
            (void *(*) (void *)) cerver_inactive_thread,
            cerver
        )) {
            char *s = c_string_create ("Created cerver %s INACTIVE thread!",
                cerver->info->name->str);
            if (s) {
                cerver_log_success (s);
                free (s);
            }

            retval = 0;
        }

        else {
            char *s = c_string_create ("Failed to create cerver %s INACTIVE thread!",
                cerver->info->name->str);
            if (s) {
                cerver_log_error (s);
                free (s);
            }
        }
    }

    return retval;

}

static CerverUpdate *cerver_update_new (Cerver *cerver, void *args) {

    CerverUpdate *cu = (CerverUpdate *) malloc (sizeof (CerverUpdate));
    if (cu) {
        cu->cerver = cerver;
        cu->args = args;
    }

    return cu;

}

static inline void cerver_update_delete (void *cerver_update_ptr) {

    if (cerver_update_ptr) free (cerver_update_ptr);

}

// 31/01/2020 -- called in a dedicated thread only if a user method was set
// executes methods every tick
static void cerver_update (void *args) {

    if (args) {
        Cerver *cerver = (Cerver *) args;
        
        #ifdef CERVER_DEBUG
        cerver_log_success ("cerver_update () has started!");
        #endif

        CerverUpdate *cu = cerver_update_new (cerver, cerver->update_args);

        u32 time_per_frame = 1000000 / cerver->update_ticks;
        // printf ("time per frame: %d\n", time_per_frame);
        u32 temp = 0;
        i32 sleep_time = 0;
        u64 delta_time = 0;

        u64 delta_ticks = 0;
        u32 fps = 0;
        struct timespec start, middle, end;

        while (cerver->isRunning) {
            clock_gettime (CLOCK_MONOTONIC_RAW, &start);

            // do stuff
            if (cerver->update) cerver->update (cu);

            // limit the fps
            clock_gettime (CLOCK_MONOTONIC_RAW, &middle);
            temp = (middle.tv_nsec - start.tv_nsec) / 1000;
            // printf ("temp: %d\n", temp);
            sleep_time = time_per_frame - temp;
            // printf ("sleep time: %d\n", sleep_time);
            if (sleep_time > 0) {
                usleep (sleep_time);
            } 

            // count fps
            clock_gettime (CLOCK_MONOTONIC_RAW, &end);
            delta_time = (end.tv_nsec - start.tv_nsec) / 1000000;
            delta_ticks += delta_time;
            fps++;
            // printf ("delta ticks: %ld\n", delta_ticks);
            if (delta_ticks >= 1000) {
                // printf ("cerver %s update fps: %i\n", cerver->info->name->str, fps);
                delta_ticks = 0;
                fps = 0;
            }
        }

        cerver_update_delete (cu);
    }

}

// 31/01/2020 -- called in a dedicated thread only if a user method was set
// executes methods every x seconds
static void cerver_update_interval (void *args) {

    if (args) {
        Cerver *cerver = (Cerver *) args;
        
        #ifdef CERVER_DEBUG
        cerver_log_success ("cerver_update_interval () has started!");
        #endif

        CerverUpdate *cu = cerver_update_new (cerver, cerver->update_interval_args);

        while (cerver->isRunning) {
            if (cerver->update_interval) cerver->update_interval (cu);

            sleep (cerver->update_interval_secs);
        }

        cerver_update_delete (cu);
    }

}

// 11/05/2020 -- start cerver's multiple app handlers
static u8 cerver_multiple_app_handlers_start (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        cerver->num_handlers_alive = 0;
        cerver->num_handlers_working = 0;

        #ifdef CERVER_DEBUG
        cerver_log_debug ("Initializing multiple app handlers...");
        #endif

        for (unsigned int i = 0; i < cerver->n_handlers; i++) {
            if (cerver->handlers[i]) {
                if (!cerver->handlers[i]->direct_handle) {
                    errors |= handler_start (cerver->handlers[i]);
                }
            }
        }

        if (!errors) {
            // wait for all handlers to be initialized
            while (cerver->num_handlers_alive != cerver->n_handlers) {}

            #ifdef CERVER_DEBUG
            cerver_log_success ("Multiple app handlers are ready!");
            #endif
        }

        else {
            char *s = c_string_create ("Failed to init ALL multiple app handlers in cerver %s",
                cerver->info->name->str);
            if (s) {
                cerver_log_error (s);
                free (s);
            }
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
                        char *s = c_string_create ("Cerver %s app_packet_handler has started!",
                            cerver->info->name->str);
                        if (s) {
                            cerver_log_success (s);
                            free (s);
                        }
                        #endif
                    }

                    else {
                        char *s = c_string_create ("Failed to start cerver %s app_packet_handler!",
                            cerver->info->name->str);
                        if (s) {
                            cerver_log_error (s);
                            free (s);
                        }

                        errors = 1;
                    }
                }
            }
            
            else {
                switch (cerver->type) {
                    case WEB_CERVER: break;

                    default: {
                        char *s = c_string_create ("Cerver %s does not have an app_packet_handler",
                            cerver->info->name->str);
                        if (s) {
                            cerver_log_warning (s);
                            free (s);
                        }
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
                    char *s = c_string_create ("Cerver %s app_error_packet_handler has started!",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_success (s);
                        free (s);
                    }
                    #endif
                }

                else {
                    char *s = c_string_create ("Failed to start cerver %s app_error_packet_handler!",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            switch (cerver->type) {
                case WEB_CERVER: break;

                default: {
                    char *s = c_string_create ("Cerver %s does not have an app_error_packet_handler",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_warning (s);
                        free (s);
                    }
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
                    char *s = c_string_create ("Cerver %s custom_packet_handler has started!",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_success (s);
                        free (s);
                    }
                    #endif
                }

                else {
                    char *s = c_string_create ("Failed to start cerver %s custom_packet_handler!",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            // char *s = c_string_create ("Cerver %s does not have an custom_packet_handler",
            //     cerver->info->name->str);
            // if (s) {
            //     cerver_log_warning (s);
            //     free (s);
            // }
        }
    }

    return retval;

}

// 27/05/2020 -- starts all cerver's handlers
static u8 cerver_handlers_start (Cerver *cerver) {

    u8 errors = 0;

    if (cerver) {
        #ifdef CERVER_DEBUG
        char *s = c_string_create ("Initializing %s handlers...",
            cerver->info->name->str);
        if (s) {
            cerver_log_debug (s);
            free (s);
        }
        #endif

        cerver->handlers_lock = (pthread_mutex_t *) malloc (sizeof (pthread_mutex_t));
        pthread_mutex_init (cerver->handlers_lock, NULL);

        errors |= cerver_app_handlers_start (cerver);

        errors |= cerver_app_error_handler_start (cerver);

        errors |= cerver_custom_handler_start (cerver);

        if (!errors) {
            #ifdef CERVER_DEBUG
            s = c_string_create ("Done initializing %s handlers!",
                cerver->info->name->str);
            if (s) {
                cerver_log_debug (s);
                free (s);
            }
            #endif
        }
    }

    return errors;

}

// tell the cerver to start listening for connections and packets
// initializes cerver's structures like thpool (if any) 
// and any other processes that have been configured before
// returns 0 on success, 1 on error
u8 cerver_start (Cerver *cerver) {

    u8 retval = 1;

    if (!cerver->isRunning) {
        cerver->isRunning = true;

        if (!cerver_one_time_init (cerver)) {
            u8 errors = 0;

            // 17/06/2020
            // check for inactive will be handled in its own thread
            // to avoid messing with other dedicated update threads timings
            if (cerver->inactive_clients) {
                errors |= cerver_start_inactive (cerver);
            }

            if (cerver->update) {
                if (thread_create_detachable (
                    &cerver->update_thread_id,
                    (void *(*) (void *)) cerver_update,
                    cerver
                )) {
                    char *s = c_string_create ("Failed to create cerver %s UPDATE thread!",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }

                    errors |= 1;
                }
            }

            if (cerver->update_interval) {
                if (thread_create_detachable (
                    &cerver->update_interval_thread_id,
                    (void *(*) (void *)) cerver_update_interval,
                    cerver
                )) {
                    char *s = c_string_create ("Failed to create cerver %s UPDATE INTERVAL thread!",
                        cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }

                    errors |= 1;
                }
            }

            errors |= cerver_handlers_start (cerver);

            if (!errors) {
                switch (cerver->protocol) {
                    case PROTOCOL_TCP: {
                        retval = cerver_start_tcp (cerver);
                    } break;

                    case PROTOCOL_UDP: {
                        // retval = cerver_start_udp (cerver);
                        char *s = c_string_create ("Cerver %s - udp server is not yet implemented!",
                            cerver->info->name->str);
                        if (s) {
                            cerver_log_warning (s);
                            free (s);
                        }
                    } break;

                    default: {
                        char *s = c_string_create ("Cant't start cerver %s! Unknown protocol!",
                            cerver->info->name->str);
                        if (s) {
                            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                            free (s);
                        }
                    }
                    break;
                }
            }
        }

        else {
            char *s = c_string_create ("Failed cerver_one_time_init () for cerver %s!",
                cerver->info->name->str);
            if (s) {
                cerver_log_error (s);
                free (s);
            }
        } 
    }

    else {
        char *s = c_string_create ("The cerver %s is already running!",
            cerver->info->name->str);
        if (s) {
            cerver_log_msg (stdout, LOG_WARNING, LOG_CERVER, s);
            free (s);
        }
    }

    return retval;

}

#pragma region end

// disable socket I/O in both ways and stop any ongoing job
// returns 0 on success, 1 on error
u8 cerver_shutdown (Cerver *cerver) {

    if (cerver->isRunning) {
        cerver->isRunning = false; 

        // close the cerver socket
        if (!close (cerver->sock)) {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("The cerver %s socket has been closed.",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                free (status);
            }
            #endif

            return 0;
        }

        else {
            char *status = c_string_create ("Failed to close cerver %s socket!",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
        } 
    } 

    return 1;

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
        if (cerver->stats->current_n_connected_clients > 0) {
            // send a cerver teardown packet to all clients connected to cerver
            Packet *packet = packet_generate_request (CERVER_PACKET, CERVER_TEARDOWN, NULL, 0);
            if (packet) {
                client_broadcast_to_all_avl (cerver->clients->root, cerver, packet);
                packet_delete (packet);
            }
        }

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

// clean cerver data structures
static void cerver_clean (Cerver *cerver) {

    if (cerver) {
        switch (cerver->type) {
            case CUSTOM_CERVER: break;
            case FILE_CERVER: break;
            case GAME_CERVER: {
                if (cerver->cerver_data) {
                    GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

                    if (game_cerver->final_game_action)
                        game_cerver->final_game_action (game_cerver->final_action_args);

                    Lobby *lobby = NULL;
                    for (ListElement *le = dlist_start (game_cerver->current_lobbys); le; le = le->next) {
                        lobby = (Lobby *) le->data;
                        lobby->running = false;
                    }
                    
                    game_delete (game_cerver);      // delete game cerver data
                    cerver->cerver_data = NULL;
                }
            } break;
            case WEB_CERVER: break;

            default: break;
        }

        // clean up on hold connections
        cerver_destroy_on_hold_connections (cerver);

        // clean up cerver connected clients
        cerver_destroy_clients (cerver);
        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Done cleaning up clients.");
        #endif

        // disable socket I/O in both ways and stop any ongoing job
        if (!cerver_shutdown (cerver)) {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Cerver %s has been shutted down.", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, status);
                free (status);
            }
            #endif
        }

        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to shutdown cerver %s!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
            #endif
        } 

        // stop any app / custom handler
        if (!cerver_handlers_destroy (cerver)) {
            char *s = c_string_create ("Done destroying handlers in cerver %s",
                cerver->info->name->str);
            if (s) {
                cerver_log_success (s);
                free (s);
            }
        }
        
        if (cerver->thpool) {
            char *status = NULL;

            #ifdef CERVER_DEBUG
            status = c_string_create ("Cerver %s active thpool threads: %i", 
                cerver->info->name->str,
                thpool_get_num_threads_working (cerver->thpool)
            );
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                free (status);
            }
            #endif

            thpool_destroy (cerver->thpool);

            #ifdef CERVER_DEBUG
            status = c_string_create ("Destroyed cerver %s thpool!", cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                free (status);
            }
            #endif

            cerver->thpool = NULL;
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Cerver %s does NOT have a thpool to destroy",
                cerver->info->name->str);
            if (s) {
                cerver_log_debug (s);
                free (s);
            }
            #endif
        }
    }

}

// teardown a cerver -> stop the cerver and clean all of its data
// returns 0 on success, 1 on error
u8 cerver_teardown (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        char *status = NULL;

        #ifdef CERVER_DEBUG
        status = c_string_create ("Starting cerver %s teardown...", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
            free (status);
        }
        #endif

        cerver_clean (cerver);

        // 22/01/2020 -- 10:05 -- correctly end admin connections
        admin_cerver_teardown (cerver->admin);

        // 29/05/2020
        cerver_sockets_pool_end (cerver);

        status = c_string_create ("Cerver %s teardown was successfull!", 
            cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_NO_TYPE, status);
            free (status);
        }

        cerver_delete (cerver);

        retval = 0;
    }

    else {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_ERROR, LOG_CERVER, "Can't teardown a NULL cerver!");
        #endif
    }

    return retval;

}

#pragma endregion

#pragma region serialization

static inline SCerver *scerver_new (void) {

    SCerver *scerver = (SCerver *) malloc (sizeof (SCerver));
    if (scerver) memset (scerver, 0, sizeof (SCerver));
    return scerver;

}

static inline void scerver_delete (void *ptr) { if (ptr) free (ptr); }

// srealizes the cerver 
static SCerver *cerver_serliaze (Cerver *cerver) {

    if (cerver) {
        SCerver *scerver = scerver_new ();
        if (scerver) {
            scerver->use_ipv6 = cerver->use_ipv6;
            scerver->protocol = cerver->protocol;
            scerver->port = cerver->port;

            strncpy (scerver->name, cerver->info->name->str, 32);
            scerver->type = cerver->type;
            scerver->auth_required = cerver->auth_required;
            scerver->uses_sessions = cerver->use_sessions;
        }

        return scerver;
    }

    return NULL;

}

// creates a cerver info packet ready to be sent
Packet *cerver_packet_generate (Cerver *cerver) {

    Packet *packet = NULL;

    if (cerver) {
        SCerver *scerver = cerver_serliaze (cerver);
        if (scerver) {
            packet = packet_generate_request (CERVER_PACKET, CERVER_INFO, scerver, sizeof (SCerver));
            scerver_delete (scerver);
        }
    }

    return packet;

}

#pragma endregion

#pragma region report 

static CerverReport *cerver_report_new (void) {

    CerverReport *cerver_report = (CerverReport *) malloc (sizeof (Cerver));
    if (cerver_report) {
        memset (cerver_report, 0, sizeof (CerverReport));
        cerver_report->ip = NULL;
        cerver_report->name = NULL;
        cerver_report->token = NULL;
    }

    return cerver_report;

}

void cerver_report_delete (void *ptr) {

    if (ptr) {
        CerverReport *cerver_report = (CerverReport *) ptr;

        estring_delete (cerver_report->name);
        estring_delete (cerver_report->ip);

        free (cerver_report);
    }

}

static CerverReport *cerver_report_deserialize (SCerverReport *scerver) {
    
    CerverReport *cerver = NULL;

    if (scerver) {
        cerver = cerver_report_new ();
        if (cerver) {
            cerver->use_ipv6 = scerver->use_ipv6;
            cerver->protocol = scerver->protocol;
            cerver->port = scerver->port;

            cerver->name = estring_new (scerver->name);
            cerver->type = scerver->type;
            cerver->auth_required = scerver->auth_required;
            cerver->uses_sessions = scerver->uses_sessions;
        }
    }

    return cerver;

}

// compare the info the server sent us with the one we expected 
// and ajust our connection values if necessary
static u8 cerver_check_info (CerverReport *cerver_report, Connection *connection) {

    u8 retval = 1;

    if (cerver_report && connection) {
        #ifdef CERVER_DEBUG
        char *status = c_string_create ("Connected to cerver %s.", cerver_report->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, status);
            free (status);
        }
        
        switch (cerver_report->protocol) {
            case PROTOCOL_TCP: 
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver using TCP protocol."); 
                break;
            case PROTOCOL_UDP: 
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver using UDP protocol.");
                break;

            default: 
                cerver_log_msg (stdout, LOG_WARNING, LOG_NO_TYPE, "Cerver using unknown protocol."); 
                break;
        }
        #endif

        if (cerver_report->use_ipv6) {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver is configured to use ipv6");
            #endif
        }

        #ifdef CERVER_DEBUG
        switch (cerver_report->type) {
            case CUSTOM_CERVER:
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver is of type: custom cerver");
                break;
            case FILE_CERVER:
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver is of type: file cerver");
                break;
            case WEB_CERVER:
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver is of type: web cerver");
                break;
            case GAME_CERVER:
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver is of type: game cerver");
                break;

            default: 
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Cerver is of unknown type."); 
                break;
        }
        #endif

        if (cerver_report->auth_required) {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver requires authentication.");
            #endif
            // FIXME:
            // if (connection->auth_data) {
            //     #ifdef CERVER_DEBUG
            //     cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Sending auth data to cerver...");
            //     #endif

            //     if (!connection->auth_packet) connection_generate_auth_packet (connection);

            //     if (packet_send (connection->auth_packet, 0, NULL)) {
            //         cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to send connection auth packet!");
            //     }

            //     retval = 0;
            // }
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Cerver does not requires authentication.");
            #endif
        }
        
        retval = 0;
    }

    return retval;

}

// handles cerver type packets
void client_cerver_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->data_size >= sizeof (RequestData)) {
            char *end = (char *) packet->data;
            RequestData *req = (RequestData *) (end);

            switch (req->type) {
                case CERVER_INFO: {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "Received a cerver info packet.");
                    #endif
                    packet->connection->cerver_report = cerver_report_deserialize ((SCerverReport *) (end += sizeof (RequestData)));
                    if (!cerver_check_info (packet->connection->cerver_report, packet->connection)) {
                        packet->connection->connected_to_cerver = true;
                    }

                    else {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Failed to correctly check cerver info!");
                    }
                } break;

                // the cerves is going to be teardown, we have to disconnect
                case CERVER_TEARDOWN:
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_WARNING, LOG_NO_TYPE, "---> Server teardown! <---");
                    #endif
                    client_connection_end (packet->client, packet->connection);
                    // client_event_trigger (packet->client, EVENT_DISCONNECTED);
                    break;

                case CERVER_INFO_STATS:
                    break;

                case CERVER_GAME_STATS:
                    break;

                default: 
                    cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, "Unknown cerver type packet."); 
                    break;
            }
        }
    }

}

#pragma endregion