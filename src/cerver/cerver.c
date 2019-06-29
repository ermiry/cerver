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
#include "cerver/types/string.h"

#include "cerver/threads/thpool.h"

#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/auth.h"
#include "cerver/handler.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/game/game.h"

#include "cerver/collections/dllist.h"
#include "cerver/collections/avl.h" 

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

static DoubleList *cervers = NULL;

Cerver *cerver_new (void) {

    Cerver *c = (Cerver *) malloc (sizeof (Cerver));
    if (c) {
        memset (c, 0, sizeof (Cerver));

        c->protocol = PROTOCOL_TCP;         // default protocol
        c->use_ipv6 = false;
        c->connection_queue = DEFAULT_CONNECTION_QUEUE;

        c->isRunning = false;
        c->blocking = true;

        c->cerver_data = NULL;
        c->delete_cerver_data = NULL;

        c->thpool = NULL;

        c->clients = NULL;
        c->client_sock_fd_map = NULL;
        c->on_client_connected = NULL;
        c->on_client_connected_data = NULL;
        c->delete_on_client_connected_data = NULL;

        c->fds = NULL;
        c->compress_clients = false;

        c->auth_required = false;
        c->auth = NULL;

        c->on_hold_connections = NULL;
        c->hold_fds = NULL;
        c->compress_on_hold = false;
        c->holding_connections = false;

        c->use_sessions = false;
        c->session_id_generator = NULL;

        c->app_packet_handler = NULL;
        c->app_error_packet_handler = NULL;
        c->custom_packet_handler = NULL;

        c->name = NULL;
        c->welcome_msg = NULL;
        c->cerver_info_packet = NULL;

        c->time_started = NULL;

        c->cerver_update = NULL;
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

        if (cerver->on_client_connected_data) {
            if (cerver->delete_on_client_connected_data) 
                cerver->delete_on_client_connected_data (cerver->on_client_connected_data);
            else free (cerver->on_client_connected_data);
        }

        if (cerver->auth) auth_delete (cerver->auth);

        packet_delete (cerver->cerver_info_packet);

        str_delete (cerver->name);
        str_delete (cerver->welcome_msg);

        if (cerver->time_started) free (cerver->time_started);

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

// sets the cerver's data and a way to free it
void cerver_set_cerver_data (Cerver *cerver, void *data, Action delete_data) {

    if (cerver) {
        cerver->cerver_data = data;
        cerver->delete_cerver_data = delete_data;
    }

}

// sets an action to be performed by the cerver when a new client connects
u8 cerver_set_on_client_connected  (Cerver *cerver, 
    Action on_client_connected, void *data, Action delete_data) {

    if (cerver) {
        cerver->on_client_connected = on_client_connected;
        cerver->on_client_connected_data = data;
        cerver->delete_on_client_connected_data = delete_data;
        return 0;
    }

    return 1;

}

// sets the cerver poll timeout
void cerver_set_poll_time_out (Cerver *cerver, const u32 poll_timeout) {

    if (cerver) cerver->poll_timeout = poll_timeout;

}

// init on hold client on hold structures and values
static u8 cerver_on_hold_init (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver->on_hold_connections = avl_init (client_connection_comparator, client_connection_delete);
        if (cerver->on_hold_connections) {
            cerver->max_on_hold_connections = poll_n_fds;
            cerver->hold_fds = (struct pollfd *) calloc (cerver->max_on_hold_connections, sizeof (struct pollfd));
            if (cerver->hold_fds) {
                memset (cerver->hold_fds, 0, sizeof (cerver->hold_fds));
                cerver->max_on_hold_connections = poll_n_fds;
                cerver->current_n_fds = 0;
                for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
                    cerver->hold_fds[i].fd = -1;

                cerver->compress_on_hold = false;
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

// sets a cutom app packet hanlder and a custom app error packet handler
void cerver_set_app_handlers (Cerver *cerver, Action app_handler, Action app_error_handler) {

    if (cerver) {
        cerver->app_packet_handler = app_handler;
        cerver->app_error_packet_handler = app_error_handler;
    }

}

// sets a custom packet handler
void cerver_set_custom_handler (Cerver *cerver, Action custom_handler) {

    if (cerver) cerver->custom_packet_handler = custom_handler;

}

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
u8 cerver_set_welcome_msg (Cerver *cerver, const char *msg) {

    if (cerver) {
        str_delete (cerver->welcome_msg);
        cerver->welcome_msg = msg ? str_new (msg) : NULL;
        return 0;
    }

    return 1;

}

// sets a custom cerver update function to be executed every n ticks
void cerver_set_update (Cerver *cerver, Action update, const u8 ticks) {

    if (cerver) {
        cerver->cerver_update = update;
        cerver->ticks = ticks;
    }

}

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
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Failed to create cerver %s socket!", cerver->name->str));
            return 1;
        }

        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
            c_string_create ("Created cerver %s socket!", cerver->name->str));
        #endif

        // set the socket to non blocking mode
        if (!sock_set_blocking (cerver->sock, cerver->blocking)) {
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Failed to set cerver %s socket to non blocking mode!", cerver->name->str));
            close (cerver->sock);
            return 1;
        }

        else {
            cerver->blocking = false;
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Cerver %s socket set to non blocking mode.", cerver->name->str));
            #endif
        }

        struct sockaddr_storage address;
        memset (&address, 0, sizeof (struct sockaddr_storage));

        if (cerver->use_ipv6) {
            struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &address;
            addr->sin6_family = AF_INET6;
            addr->sin6_addr = in6addr_any;
            addr->sin6_port = htons (cerver->port);
        } 

        else {
            struct sockaddr_in *addr = (struct sockaddr_in *) &address;
            addr->sin_family = AF_INET;
            addr->sin_addr.s_addr = INADDR_ANY;
            addr->sin_port = htons (cerver->port);
        }

        if ((bind (cerver->sock, (const struct sockaddr *) &address, sizeof (struct sockaddr_storage))) < 0) {
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Failed to bind cerver %s socket!", cerver->name->str));
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
        cerver->client_sock_fd_map = htab_init (poll_n_fds, NULL, NULL, NULL, false, NULL, NULL);

        // initialize main pollfd structures
        cerver->fds = (struct pollfd *) calloc (poll_n_fds, sizeof (struct pollfd));
        if (cerver->fds) {
            memset (cerver->fds, 0, sizeof (cerver->fds));
            // set all fds as available spaces
            for (u32 i = 0; i < poll_n_fds; i++) cerver->fds[i].fd = -1;

            cerver->max_n_fds = poll_n_fds;
            cerver->current_n_fds = 0;
            cerver->compress_clients = false;

            // initialize cerver's own thread pool
            cerver->thpool = thpool_create (cerver->name->str, DEFAULT_TH_POOL_INIT);
            if (cerver->thpool) {
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
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                    c_string_create ("Failed to init cerver %s thpool!", cerver->name->str));
                #endif

                avl_delete (cerver->clients);
                htab_destroy (cerver->client_sock_fd_map);

                free (cerver->fds);
            }
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                c_string_create ("Failed to allocate cerver %s main fds!", cerver->name->str));
            #endif

            avl_delete (cerver->clients);
            htab_destroy (cerver->client_sock_fd_map);
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
                #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                    "Done initializing cerver data structures and values!");
                #endif

                retval = 0;     // success!!
            }

            else {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Failed to init cerver %s data structures!",
                    cerver->name->str));
            } 
        }

        else {
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER,
                c_string_create ("Failed to init cerver %s network!", cerver->name->str));
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
            cerver->name = str_new (name);

            cerver_set_network_values (cerver, port, protocol, use_ipv6, connection_queue);
            cerver_set_poll_time_out (cerver, poll_timeout);

            if (!cerver_init (cerver)) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_NO_TYPE, 
                    c_string_create ("Initialized cerver %s!", cerver->name->str));
            }

            else {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                    c_string_create ("Failed to init cerver %s!", cerver->name->str));

                cerver_delete (cerver);
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

// FIXME: stop and start the cerver
// teardowns the cerver and creates a fresh new one with the same parameters
Cerver *cerver_restart (Cerver *cerver) {

    /* if (cerver) {
        cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, "Restarting the cerver...");

        Cerver temp = { 
            .use_ipv6 = cerver->use_ipv6, .protocol = cerver->protocol, .port = cerver->port,
            .connection_queue = cerver->connection_queue, .type = cerver->type,
            .poll_timeout = cerver->poll_timeout, .auth_required = cerver->auth_required,
            .use_sessions = cerver->use_sessions };

        temp.name = str_new (cerver->name->str);

        if (!cerver_teardown (cerver)) 
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, "Done with cerver teardown");
        else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Failed to teardown the cerver!");

        // what ever the output, create a new cerver --> restart
        Cerver *retServer = cerver_new (&temp);
        if (!cerver_init (retServer, NULL, temp.type)) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, "Cerver has restarted!");
            return retServer;
        }

        else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Unable to retstart the cerver!");
    }

    else cerver_log_msg (stdout, LOG_WARNING, LOG_CERVER, "Can't restart a NULL cerver!"); */
    
    return NULL;

}

// tell the cerver to start listening for connections and packets
u8 cerver_start (Cerver *cerver) {

    if (cerver->isRunning) {
        cerver_log_msg (stdout, LOG_WARNING, LOG_CERVER, 
            c_string_create ("The cerver %s is already running!",
            cerver->name->str));
        return 1;
    }

    u8 retval = 1;

    // one time only inits
    // if we have a game cerver, we might wanna load game data -> set by cerver admin
    if (cerver->type == GAME_CERVER) {
        GameCerver *game_data = (GameCerver *) cerver->cerver_data;
        if (game_data && game_data->load_game_data) {
            game_data->load_game_data (NULL);
        }

        else {
            cerver_log_msg (stdout, LOG_WARNING, LOG_GAME, 
                c_string_create ("Game cerver %s doesn't have a reference to a game data!",
                cerver->name->str));
        } 
    }

    cerver->cerver_info_packet = cerver_packet_generate (cerver);

    switch (cerver->protocol) {
        case PROTOCOL_TCP: {
            if (!cerver->blocking) {
                if (!listen (cerver->sock, cerver->connection_queue)) {
                    // register the cerver start time
                    // TODO:

                    // set up the initial listening socket     
                    cerver->fds[cerver->current_n_fds].fd = cerver->sock;
                    cerver->fds[cerver->current_n_fds].events = POLLIN;
                    cerver->current_n_fds++;

                    cerver->isRunning = true;

                    // cerver is not holding clients if there is not new connections
                    if (cerver->auth_required) cerver->holding_connections = false;

                    cerver_poll (cerver);

                    retval = 0;
                }

                else {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                        c_string_create ("Failed to listen in cerver %s socket!",
                        cerver->name->str));
                    close (cerver->sock);
                }
            }

            else {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Cerver %s socket is not set to non blocking!",
                    cerver->name->str));
            }
            
        } break;

        case PROTOCOL_UDP: /* TODO: */ break;

        default: 
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Cant't start cerver %s! Unknown protocol!",
                cerver->name->str));
            break;
    }

    return retval;

}

// disable socket I/O in both ways and stop any ongoing job
u8 cerver_shutdown (Cerver *cerver) {

    if (cerver->isRunning) {
        cerver->isRunning = false; 

        // close the cerver socket
        if (!close (cerver->sock)) {
            #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                    c_string_create ("The cerver %s socket has been closed.",
                    cerver->name->str));
            #endif

            return 0;
        }

        else {
            cerver_log_msg (stdout, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Failed to close cerver %s socket!",
                cerver->name->str));
        } 
    } 

    return 1;

}

// FIXME: get rid of any on hold connections
static void cerver_destroy_on_hold_connections (Cerver *cerver) {

    if (cerver) {

    }

}

// FIXME:
// cleans up the client's structures in the current cerver
// if ther are clients connected, we send a cerver teardown packet
static void cerver_destroy_clients (Cerver *cerver) {

    if (cerver) {
        // TODO: generate a cerver teardwon packet
        // create cerver teardown packet
        // size_t packetSize = sizeof (PacketHeader);
        // void *packet = generatePacket (SERVER_TEARDOWN, packetSize);

        // FIXME:
        // send a packet to any active client
        // broadcastToAllClients (cerver->clients->root, cerver, packet, packetSize);
        
        // clear the client's poll  -> to stop any connection
        // this may change to a for if we have a dynamic poll structure
        // memset (cerver->fds, 0, sizeof (cerver->fds));

        // destroy the active clients tree
        // avl_clear_tree (&cerver->clients->root, cerver->clients->destroy);
        // FIXME: unregister the clients from the cerver to properly close the connections
        avl_delete (cerver->clients);
    }

}

// teardown a cerver -> stop the cerver and clean all of its data
u8 cerver_teardown (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, 
                c_string_create ("Starting cerver %s teardown...", cerver->name->str));
        #endif

        switch (cerver->type) {
            case CUSTOM_CERVER: break;
            case FILE_CERVER: break;
            case GAME_CERVER: {
                GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;
                if (game_cerver->final_game_action)
                    game_cerver->final_game_action (game_cerver->final_action_args);
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
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, 
                c_string_create ("Cerver %s has been shutted down.", cerver->name->str));
            #endif
        }
            
        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Failed to shutdown cerver %s!", cerver->name->str));
            #endif
        } 
        
        if (cerver->thpool) {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Cerver %s active thpool threads: %i", 
                cerver->name->str,
                thpool_num_threads_working (cerver->thpool)));
            #endif

            thpool_destroy (cerver->thpool);

            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Destroyed cerver %s thpool!", cerver->name->str));
            #endif
        } 

        cerver_delete (cerver);

        cerver_log_msg (stdout, LOG_SUCCESS, LOG_NO_TYPE, 
            c_string_create ("Cerver %s teardown was successfull!", 
                cerver->name->str));

        retval = 0;
    }

    else {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_ERROR, LOG_CERVER, "Can't teardown a NULL cerver!");
        #endif
    }

    return retval;

}

/*** serialization ***/

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

            strncpy (scerver->name, cerver->name->str, 32);
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
            packet = packet_create (SERVER_INFO, &scerver, sizeof (SCerver));
            packet_generate (packet);
            scerver_delete (scerver);
        }
    }

    return packet;

}