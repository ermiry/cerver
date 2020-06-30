#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/socket.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/errors.h"
#include "cerver/handler.h"
#include "cerver/sessions.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/auth.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/thpool.h"

#include "cerver/collections/htab.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static i32 on_hold_poll_get_idx_by_sock_fd (const Cerver *cerver, i32 sock_fd);
static void on_hold_poll_remove_sock_fd (Cerver *cerver, const i32 sock_fd);
static u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection);
void on_hold_connection_drop (const struct _Cerver *cerver, struct _Connection *connection);
static Connection *on_hold_connection_get_by_sock (const Cerver *cerver, const i32 sock_fd);

static AuthData *auth_data_new (const char *token, void *data, size_t auth_data_size) {

    AuthData *auth_data = (AuthData *) malloc (sizeof (AuthData));
    if (auth_data) {
        auth_data->data = NULL;
        auth_data->delete_data = NULL;

        auth_data->token = token ? estring_new (token) : NULL;
        if (data) {
            auth_data->auth_data = malloc (sizeof (auth_data_size));
            if (auth_data->auth_data) {
                memcpy (auth_data->auth_data, data, auth_data_size);
                auth_data->auth_data_size = auth_data_size;
            }

            else {
                free (auth_data);
                auth_data = NULL;
            }
        }

        else {
            auth_data->auth_data = NULL;
            auth_data->auth_data_size = 0;
        }
    }

    return auth_data;

}

static void auth_data_delete (AuthData *auth_data) {

    if (auth_data) {
        estring_delete (auth_data->token);
        if (auth_data->auth_data) free (auth_data->auth_data);
        free (auth_data);
    }

}

#pragma region auth

static void auth_send_success_packet (const Cerver *cerver, const Connection *connection) {

    if (cerver) {
        Packet *success_packet = packet_generate_request (AUTH_PACKET, SUCCESS_AUTH, NULL, 0);
        if (success_packet) {
            packet_set_network_values (success_packet, (Cerver *) cerver, NULL, (Connection *) connection, NULL);
            packet_send (success_packet, 0, NULL, false);
            packet_delete (success_packet);
        }

        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to create success auth packet in cerver %s",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, status);
                free (status);
            }
            #endif
        }
    }

}

// FIXME: if something fails in this functions, the client and its data should be completely destroyed
// creates a new lcient and registers to cerver with new data
static u8 auth_create_new_client (Packet *packet, AuthData *auth_data) {

    u8 retval = 1;

    if (packet) {
        if (!on_hold_connection_remove (packet->cerver, packet->connection)) {
            Client *c = client_create ();
            if (c) {
                if (!connection_register_to_client (c, packet->connection)) {
                    connection_register_to_cerver_poll (packet->cerver, c, packet->connection);

                    if (packet->cerver->use_sessions) {
                        // FIXME: generate the new session id - token
                        // SessionData *session_data = session_data_new (packet, auth_data, c);
                        // estring *session_id = (estring *) packet->cerver->session_id_generator (session_data);
                        // session_data_delete (session_data);
                    }

                    else {
                        // TODO: generate client id
                    }

                    // register the new client to the cerver
                    if (!client_register_to_cerver (packet->cerver, c)) {
                        // set the data to the client (eg. user data)
                        client_set_data (c, auth_data->data, auth_data->delete_data);

                        retval = 0;     // success!!!
                    }

                    // cerver error -- failed to register the client to cerver
                    else {
                        #ifdef CERVER_DEBUG
                        char *status = c_string_create ("Failed to register new lcient to cerver %s",
                            packet->cerver->info->name->str);
                        if (status) {
                            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                            free (status);
                        }
                        #endif

                        Packet *error_packet = error_packet_generate (ERR_CERVER_ERROR, "Internal cerver error!");
                        if (error_packet) {
                            packet_set_network_values (error_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
                            packet_send (error_packet, 0, NULL, false);
                            packet_delete (error_packet);
                        }
                        
                        // close the connection
                        client_drop (packet->cerver, c);
                    }
                }

                // cerver error -- failed to register connection to new client
                else {
                    #ifdef CERVER_DEBUG
                    char *status = c_string_create ("Failed to register connection to client in cerver %s",
                        packet->cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                        free (status);
                    }
                    #endif

                    Packet *error_packet = error_packet_generate (ERR_CERVER_ERROR, "Internal cerver error!");
                    if (error_packet) {
                        packet_set_network_values (error_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
                        packet_send (error_packet, 0, NULL, false);
                        packet_delete (error_packet);
                    }
                    
                    // close the connection
                    on_hold_connection_drop (packet->cerver, packet->connection);
                }
            }

            // cerver error -- failed to allocate a new client
            else {
                #ifdef CERVER_DEBUG
                char *status = c_string_create ("Failed to allocate a new client in cerver %s", 
                    packet->cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                    free (status);
                }
                #endif
                
                Packet *error_packet = error_packet_generate (ERR_CERVER_ERROR, "Internal cerver error!");
                if (error_packet) {
                    packet_set_network_values (error_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
                    packet_send (error_packet, 0, NULL, false);
                    packet_delete (error_packet);
                }

                // close the connection
                on_hold_connection_drop (packet->cerver, packet->connection);
            }
        }

        // cerver error -- failed to remove connection from on hold structures
        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to remove connection from cerver's %s on hold connections", 
                packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
            #endif
            
            Packet *error_packet = error_packet_generate (ERR_CERVER_ERROR, "Internal cerver error!");
            if (error_packet) {
                packet_set_network_values (error_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
                packet_send (error_packet, 0, NULL, false);
                packet_delete (error_packet);
            }

            // close the connection
            on_hold_connection_drop (packet->cerver, packet->connection);
        }
    }

    return retval;

}

// how to manage a successfull authentication to the cerver
// we have a new client authenticated with credentials
static void auth_success (Packet *packet, AuthData *auth_data) {

    if (packet) {
        // create a new lcient and register to cerver
        if (!auth_create_new_client (packet, auth_data)) {
            // if we are successfull, send success packet
            auth_send_success_packet (packet->cerver, packet->connection);
        }
    }

}

// how to manage a failed auth to the cerver
static void auth_failed (const Packet *packet) {

    if (packet) {
        // send failed auth packet to client
        Packet *error_packet = error_packet_generate (ERR_FAILED_AUTH, "Failed to authenticate!");
        if (error_packet) {
            packet_set_network_values (error_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
            packet_send (error_packet, 0, NULL, false);
            packet_delete (error_packet);
        }

        packet->connection->auth_tries--;
        if (packet->connection->auth_tries <= 0) {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, 
            "Connection reached max auth tries, dropping now...");
            #endif
            on_hold_connection_drop (packet->cerver, packet->connection);
        }
    }

}

// authenticate a new connection using a session token
// if we find a client with that token, we register the connection to him;
// if we don't find a client, the token is invalid
static void auth_with_token (const Packet *packet, const AuthData *auth_data) {

    if (packet && auth_data) {
        // if we get a token, we search for a client with the same token
        Client *client = client_get_by_session_id (packet->cerver, auth_data->token->str);

        // if we found a client, register the new connection to him
        if (client) {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Found a client with session id %s in cerver %s.",
                auth_data->token->str, packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, status);
                free (status);
            }
            #endif

            if (!connection_register_to_client (client, packet->connection)) {
                // add the connection sock fd to the cerver poll fds
                connection_register_to_cerver_poll (packet->cerver, client, packet->connection);

                // remove the connection from the on hold structures
                on_hold_poll_remove_sock_fd (packet->cerver, packet->connection->socket->sock_fd);

                // send success auth packet to client
                auth_send_success_packet (packet->cerver, packet->connection);
            }
        }

        else {
            // if not, the token is invalid!
            auth_failed (packet);
        }
    }

}

static void auth_with_defined_method (Packet *packet, AuthData *auth_data) {

    if (packet && auth_data) {
        AuthPacket auth_packet = { .packet = packet, .auth_data = auth_data };
        if (!packet->cerver->auth->authenticate (&auth_packet)) {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Client authenticated successfully to cerver %s",
                packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, status);
                free (status);
            }
            #endif

            auth_success (packet, auth_data);
        }

        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Client failed to authenticate to cerver %s",
                packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_DEBUG, LOG_CLIENT, status);
                free (status);
            }
            #endif

            auth_failed (packet);
        }   
    }

}

// strip out the auth data from the packet
static AuthData *auth_strip_auth_data (Packet *packet) {

    AuthData *auth_data = NULL;

    if (packet) {
        // check we have a big enough packet
        if (packet->packet_size > sizeof (PacketHeader)) {
            char *end = (char *) packet->packet;

            // check if we have a token
            if (packet->packet_size == (sizeof (PacketHeader) + sizeof (SToken))) {
                SToken *s_token = (SToken *) (end += sizeof (PacketHeader));
                auth_data = auth_data_new (s_token->token, NULL, 0);
            }

            // we have custom data credentials
            else {
                end += sizeof (PacketHeader);
                size_t data_size = packet->packet_size - sizeof (PacketHeader);
                auth_data = auth_data_new (NULL, end, data_size);
            }
        }
    }

    return auth_data;

}

// try to authenticate a connection using the values he sent to use
static void auth_try (Packet *packet) {

    if (packet) {
        if (packet->cerver->auth->authenticate) {
            // strip out the auth data from the packet
            AuthData *auth_data = auth_strip_auth_data (packet);
            if (auth_data) {
                // check that the cerver supports sessions
                if (packet->cerver->use_sessions) {
                    if (auth_data->token) {
                        auth_with_token (packet, auth_data);
                    }

                    else {
                        // if not, we authenticate using the user defined method
                        auth_with_defined_method (packet, auth_data);
                    }
                }

                else {
                    // if not, we authenticate using the user defined method
                    auth_with_defined_method (packet, auth_data);
                }

                auth_data_delete (auth_data);
            }

            // failed to get auth data form packet
            else {
                #ifdef CERVER_DEBUG
                char *status = c_string_create ("Failed to get auth data from packet in cerver %s",
                    packet->cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                    free (status);
                }
                #endif

                auth_failed (packet);
            }
        }

        // no authentication method -- clients are not able to interact to the cerver!
        else {
            char *status = c_string_create ("Cerver %s does not have an authenticate method!",
                packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }

            // close the on hold connection assocaited with sock fd 
            // and remove it from the cerver
            on_hold_connection_drop (packet->cerver, packet->connection);
        }
    }

}

#pragma endregion

#pragma region handler

// handle an auth packet
static void cerver_auth_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->packet_size >= (sizeof (PacketHeader) + sizeof (RequestData))) {
            char *end = (char *) packet->packet;
            RequestData *req = (RequestData *) (end += sizeof (PacketHeader));

            switch (req->type) {
                // the client sent use its data to authenticate itself
                case CLIENT_AUTH_DATA: auth_try (packet); break;

                default: {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, 
                        "cerver_auth_packet_hanlder () -- got an unknwown request type");
                    #endif
                } break;
            }
        }
    }

}

// TODO: add cerver stats
// handles an packet from an on hold connection
void on_hold_packet_handler (void *ptr) {

    if (ptr) {
        Packet *packet = (Packet *) ptr;
        if (!packet_check (packet)) {
            switch (packet->header->packet_type) {
                // handles an error from the client
                case ERROR_PACKET: /* TODO: */ break;

                // handles authentication packets
                case AUTH_PACKET: cerver_auth_packet_handler (packet); break;

                // acknowledge the client we have received his test packet
                case TEST_PACKET: cerver_test_packet_handler (packet); break;

                default: {
                    #ifdef CERVER_DEBUG
                    char *status = c_string_create ("Got an on hold packet of unknown type in cerver %s.", 
                        packet->cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, status);
                        free (status);
                    }
                    #endif
                } break;
            }
        }

        packet_delete (packet);
    }

}

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess authentication or it failed to, so it is dropped
// returns 0 on success, 1 on error
u8 on_hold_connection (Cerver *cerver, Connection *connection) {

    u8 retval = 1;

    if (cerver && connection) {
        if (cerver->on_hold_connections) {
            i32 idx = on_hold_get_free_idx (cerver);
            if (idx >= 0) {
                cerver->hold_fds[idx].fd = connection->socket->sock_fd;
                cerver->hold_fds[idx].events = POLLIN;
                cerver->current_on_hold_nfds++; 

                cerver->stats->current_n_hold_connections += 1;

                avl_insert_node (cerver->on_hold_connections, connection);
                const void *key = &connection->socket->sock_fd;
                htab_insert (cerver->on_hold_connection_sock_fd_map, key, sizeof (i32),
                    connection, sizeof (Connection));

                if (cerver->holding_connections == false) {
                    cerver->holding_connections = true;

                    if (thread_create_detachable (&cerver->on_hold_poll_id, on_hold_poll, cerver)) {
                        char *status = c_string_create ("Failed to create cerver's %s on_hold_poll () thread!", 
                            cerver->info->name->str);
                        if (status) {
                            cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, status);
                            free (status);
                        }
                        
                        cerver->holding_connections = false;
                    }
                }

                char *status = NULL;
                #ifdef CERVER_DEBUG
                status = c_string_create ("Connection is on hold on cerver %s.", cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                    free (status);
                }
                #endif

                #ifdef CERVER_STATS
                status = c_string_create ("Cerver %s current on hold connections: %i.", 
                    cerver->info->name->str, cerver->stats->current_n_hold_connections);
                if (status) {
                    cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
                    free (status);
                }
                #endif

                connection->active = true;

                retval = 0;
            }

            else {
                #ifdef CERVER_DEBUG
                char *status = c_string_create ("Cerver %s on hold poll is full -- we need to realloc...", 
                    cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, status);
                    free (status);
                }
                #endif
                if (cerver_realloc_on_hold_poll_fds (cerver)) {
                    char *status = c_string_create ("Failed to realloc cerver %s on hold poll fds!", 
                        cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, status);
                        free (status);
                    }
                }

                else retval = on_hold_connection (cerver, connection);
            }
        }
    }

    return retval;

}

// removes the connection from the on hold structures
static u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection) {

    u8 retval = 1;

    if (cerver && connection) {
        // remove the connection associated to the sock fd
        Connection *query = connection_new ();
        // query->socket->sock_fd = connection->socket->sock_fd;
        query->socket = socket_create (connection->socket->sock_fd);
        avl_remove_node (cerver->on_hold_connections, query);

        // remove connection from on hold map
        const void *key = &connection->socket->sock_fd;
        htab_remove (cerver->on_hold_connection_sock_fd_map, key, sizeof (i32));

        // unregister the fd from the on hold structures
        on_hold_poll_remove_sock_fd ((Cerver *) cerver, connection->socket->sock_fd);

        retval = 0;
    }

    return retval;

}

// closes the on hold connection and removes it from the cerver
void on_hold_connection_drop (const Cerver *cerver, Connection *connection) {

    if (cerver && connection) {
        // close the connection socket
        connection_end (connection);

        // remove the connection from the on hold structures
        on_hold_connection_remove (cerver, connection);

        // we can now safely delete the connection
        connection_delete (connection);
    }

}

static Connection *on_hold_connection_get_by_sock (const Cerver *cerver, const i32 sock_fd) {

    Connection *connection = NULL;

    if (cerver) {
        Connection *query = connection_new ();
        if (query) {
            // query->sock_fd = sock_fd;
            query->socket = socket_create (sock_fd);
            void *connection_data = avl_get_node_data (cerver->on_hold_connections, query, NULL);
            if (connection_data) {
                connection = (Connection *) connection_data;
            }

            else {
                char *status = c_string_create ("Failed to get on hold connection associated with sock: %i", 
                    sock_fd);
                if (status) {
                    cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, status);
                    free (status);
                }
            }
        }

        else {
            // cerver error allocating memory -- this might not happen
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Failed to create connection query in cerver %s.",
                cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, status);
                free (status);
            }
            #endif
        }    
    }

    return connection;

}

#pragma endregion

#pragma region poll

// reallocs on hold cerver poll fds
// returns 0 on success, 1 on error
static u8 cerver_realloc_on_hold_poll_fds (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver->max_on_hold_connections = cerver->max_on_hold_connections * 2;
        cerver->hold_fds = (struct pollfd *) realloc (cerver->hold_fds, 
            cerver->max_on_hold_connections * sizeof (struct pollfd));
        if (cerver->hold_fds) retval = 0;
    }

    return retval;

}

static i32 on_hold_get_free_idx (Cerver *cerver) {

    if (cerver) {
        for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
            if (cerver->hold_fds[i].fd == -1) return i;
    }

    return -1;

}

static i32 on_hold_poll_get_idx_by_sock_fd (const Cerver *cerver, i32 sock_fd) {

    if (cerver) {
        for (u32 i = 0; i < cerver->max_on_hold_connections; i++)
            if (cerver->hold_fds[i].fd == sock_fd) return i;
    }

    return -1;

}

static void on_hold_poll_remove_sock_fd (Cerver *cerver, const i32 sock_fd) {

    if (cerver) {
        i32 idx = on_hold_poll_get_idx_by_sock_fd (cerver, sock_fd);
        if (idx) {
            cerver->hold_fds[idx].fd = -1;
            cerver->hold_fds[idx].events = -1;
            cerver->current_on_hold_nfds--;

            #ifdef CERVER_STATS
            char *status = c_string_create ("Cerver %s current on hold connections: %i.", 
                cerver->info->name->str, cerver->stats->current_n_hold_connections);
            if (status) {
                cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
                free (status);
            }
            #endif

            // check if we are holding any more connections, if not, we stop the on hold poll
            if (cerver->current_on_hold_nfds <= 0) {
                #ifdef CERVER_DEBUG
                char *status = c_string_create ("Stoping cerver's %s on hold poll...",
                    cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                    free (status);
                }
                #endif
                cerver->holding_connections = false;
            }
        }

        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Couldn't find %i sock fd in cerver's %s on hold poll fds.",
                sock_fd, cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, status);
                free (status);
            }
            #endif
        }
    }

}

static inline void on_hold_poll_handle (Cerver *cerver) {

    if (cerver) {
        // one or more fd(s) are readable, need to determine which ones they are
        for (u32 i = 0; i < cerver->max_on_hold_connections; i++) {
            if (cerver->fds[i].fd > -1) {
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

// handles packets from the on hold clients until they authenticate
void *on_hold_poll (void *cerver_ptr) {

    if (cerver_ptr) {
        Cerver *cerver = (Cerver *) cerver_ptr;

        char *status = c_string_create ("Cerver %s on hold poll has started!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, status);
            free (status);
        }

        char *thread_name = c_string_create ("%s-on-hold", cerver->info->name->str);
        if (thread_name) {
            thread_set_name (thread_name);
            free (thread_name);
        }

        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Waiting for connections to put on hold...");
        #endif

        int poll_retval = 0;
        while (cerver->isRunning) {
            poll_retval = poll (cerver->hold_fds, cerver->max_on_hold_connections, cerver->poll_timeout);

            switch (poll_retval) {
                case -1: {
                    char *status = c_string_create ("Cerver %s on hold poll has failed!", cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                        free (status);
                    }

                    perror ("Error");
                    cerver->holding_connections = false;
                    cerver->isRunning = false;
                } break;

                case 0: {
                    // #ifdef CERVER_DEBUG
                    // char *status = c_string_create ("Cerver %s on hold poll timeout", cerver->info->name->str);
                    // if (status) {
                    //     cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                    //     free (status);
                    // }
                    // #endif
                } break;

                default: {
                    on_hold_poll_handle (cerver);
                } break;
            }
        }

        #ifdef CERVER_DEBUG
        status = c_string_create ("Cerver %s on hold poll has stopped!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
            free (status);
        }
        #endif
    }

    else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Can't handle on hold clients on a NULL cerver!");

    return NULL;

}

#pragma endregion