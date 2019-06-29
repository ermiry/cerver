#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/errors.h"
#include "cerver/handler.h"
#include "cerver/sessions.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/auth.h"
#include "cerver/threads/thpool.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static i32 on_hold_poll_get_idx_by_sock_fd (const Cerver *cerver, i32 sock_fd);
static void on_hold_poll_remove_sock_fd (Cerver *cerver, const i32 sock_fd);
static Connection *on_hold_connection_remove (const Cerver *cerver, const i32 sock_fd);
static void on_hold_connection_drop (Cerver *cerver, const i32 sock_fd);
static Connection *on_hold_connection_get_by_sock (const Cerver *cerver, const i32 sock_fd);

static AuthData *auth_data_new (const char *token, void *data, size_t auth_data_size) {

    AuthData *auth_data = (AuthData *) malloc (sizeof (AuthData));
    if (auth_data) {
        auth_data->data = NULL;
        auth_data->delete_data = NULL;

        auth_data->token = token ? str_new (token) : NULL;
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
        str_delete (auth_data->token);
        if (auth_data->auth_data) free (auth_data->auth_data);
        free (auth_data);
    }

}

Auth *auth_new (void) {

    Auth *auth = (Auth *) malloc (sizeof (Auth));
    if (auth) {
        memset (auth, 0, sizeof (Auth));
        auth->auth_packet = NULL;
        auth->authenticate = NULL;
    }

    return auth;

}

void auth_delete (Auth *auth) {

    if (auth) {
        packet_delete (auth->auth_packet);
        free (auth);
    }

}

// generates an authentication packet with client auth request
Packet *auth_packet_generate (void) { 
    
    return packet_generate_request (AUTH_PACKET, REQ_AUTH_CLIENT, NULL, 0); 
    
}

static void auth_send_success_packet (const Cerver *cerver, const i32 sock_fd) {

    if (cerver) {
        Packet *success_packet = packet_generate_request (AUTH_PACKET, SUCCESS_AUTH, NULL, 0);
        if (success_packet) {
            packet_set_network_values (success_packet, sock_fd, cerver->protocol);
            packet_send (success_packet, 0);
            packet_delete (success_packet);
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                c_string_create ("Failed to create success auth packet in cerver %s",
                cerver->name->str));
            #endif
        }
    }

}

// FIXME: if something fails in this functions, the client and its data should be completely destroyed
// creates a new lcient and registers to cerver with new data
static u8 auth_create_new_client (Packet *packet, AuthData *auth_data) {

    u8 retval = 1;

    if (packet) {
        Connection *connection = on_hold_connection_remove (packet->cerver, packet->sock_fd);
        if (connection) {
            Client *c = client_new ();
            if (c) {
                if (!client_connection_register (packet->cerver, c, connection)) {
                    if (packet->cerver->use_sessions) {
                        // FIXME: generate the new session id - token
                        SessionData *session_data = session_data_new (packet, auth_data, c);
                        String *session_id = packet->cerver->session_id_generator (session_data);
                        session_data_delete (session_data);
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
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER,
                            c_string_create ("Failed to register new lcient to cerver %s",
                            packet->cerver->name->str));
                        #endif

                        Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Internal cerver error!");
                        if (error_packet) {
                            packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                            packet_send (error_packet, 0);
                            packet_delete (error_packet);
                        }
                        
                        // close the connection
                        on_hold_connection_drop (packet->cerver, packet->sock_fd);
                    }
                }

                // cerver error -- failed to register connection to new client
                else {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER,
                        c_string_create ("Failed to register connection to client in cerver %s",
                        packet->cerver->name->str));
                    #endif

                    Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Internal cerver error!");
                    if (error_packet) {
                        packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                        packet_send (error_packet, 0);
                        packet_delete (error_packet);
                    }
                    
                    // close the connection
                    on_hold_connection_drop (packet->cerver, packet->sock_fd);
                }
            }

            // cerver error -- failed to allocate a new client
            else {
                #ifdef CERVER_DEBUG
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Failed to allocate a new client in cerver %s", 
                    packet->cerver->name->str));
                #endif
                
                Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Internal cerver error!");
                if (error_packet) {
                    packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                    packet_send (error_packet, 0);
                    packet_delete (error_packet);
                }

                // close the connection
                on_hold_connection_drop (packet->cerver, packet->sock_fd);
            }
        }

        // cerver error -- failed to remove connection from on hold structures
        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Failed to remove connection from cerver's %s on hold connections", 
                packet->cerver->name->str));
            #endif
            
            Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Internal cerver error!");
            if (error_packet) {
                packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                packet_send (error_packet, 0);
                packet_delete (error_packet);
            }

            // close the connection
            on_hold_connection_drop (packet->cerver, packet->sock_fd);
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
            auth_send_success_packet (packet->cerver, packet->sock_fd);
        }
    }

}

// how to manage a failed auth to the cerver
static void auth_failed (const Packet *packet) {

    if (packet) {
        // send failed auth packet to client
        Packet *error_packet = error_packet_generate (ERR_FAILED_AUTH, "Failed to authenticate!");
        if (error_packet) {
            packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
            packet_send (error_packet, 0);
            packet_delete (error_packet);
        }

        // get the connection associated with the sock fd
        Connection *query = client_connection_new ();
        if (query) {
            query->sock_fd = packet->sock_fd;
            void *connection_data = avl_get_node_data (packet->cerver->on_hold_connections, query);
            if (connection_data) {
                Connection *connection = (Connection *) connection_data;
                connection->auth_tries--;
                if (connection->auth_tries <= 0) {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, 
                    "Connection reached max auth tries, dropping now...");
                    #endif
                    on_hold_connection_drop (packet->cerver, packet->sock_fd);
                }
            }

            else {
                cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                    c_string_create ("Failed to get on hold connection associated with sock: %i", 
                    packet->sock_fd));

                // send a cerver error packet back to the client
                Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Failed to authenticate!");
                if (error_packet) {
                    packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                    packet_send (error_packet, 0);
                    packet_delete (error_packet);
                }
                
                on_hold_connection_drop (packet->cerver, packet->sock_fd);
            }
        }

        else {
            // cerver error allocating memory -- this might not happen
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Failed to create connection query in cerver %s.",
                packet->cerver->name));
            #endif

            // send a cerver error packet back to the client
            Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Failed to authenticate!");
            if (error_packet) {
                packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                packet_send (error_packet, 0);
                packet_delete (error_packet);
            }

            // drop the connection as we are unable to authenticate it
            on_hold_connection_drop (packet->cerver, packet->sock_fd);
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
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, 
                c_string_create ("Found a client with session id %s in cerver %s.",
                auth_data->token->str, packet->cerver->name->str));
            #endif

            // get the connection
            // FIXME: make sure to remove the connection from the on hold
            Connection *connection = on_hold_connection_get_by_sock (packet->cerver, packet->sock_fd);
            if (connection) {
                if (!client_connection_register (packet->cerver, client, connection)) {
                    // FIXME: add the connection sock fd to the cerver poll fds

                    // remove the connection from the on hold structures
                    on_hold_poll_remove_sock_fd (packet->cerver, packet->sock_fd);

                    // send success auth packet to client
                    auth_send_success_packet (packet->cerver, packet->sock_fd);
                }
            }

            else {
                // we failed to get the connection associated with the socket!
                // send a cerver error packet back to the client
                Packet *error_packet = error_packet_generate (ERR_SERVER_ERROR, "Failed to authenticate!");
                if (error_packet) {
                    packet_set_network_values (error_packet, packet->sock_fd, packet->cerver->protocol);
                    packet_send (error_packet, 0);
                    packet_delete (error_packet);
                }
                
                on_hold_connection_drop (packet->cerver, packet->sock_fd);
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
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, 
                c_string_create ("Client authenticated successfully to cerver %s",
                packet->cerver->name->str));
            #endif

            auth_success (packet, auth_data);
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_DEBUG, LOG_CLIENT, 
                c_string_create ("Client failed to authenticate to cerver %s",
                packet->cerver->name->str));
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
            char *end = packet->packet;

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
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Failed to get auth data from packet in cerver %s",
                    packet->cerver->name->str));
                #endif

                auth_failed (packet);
            }
        }

        // no authentication method -- clients are not able to interact to the cerver!
        else {
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                c_string_create ("Cerver %s does not have an authenticate method!",
                packet->cerver->name->str));

            // close the on hold connection assocaited with sock fd 
            // and remove it from the cerver
            on_hold_connection_drop (packet->cerver, packet->sock_fd);
        }
    }

}

// handle an auth packet
static void cerver_auth_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->packet_size >= (sizeof (PacketHeader) + sizeof (RequestData))) {
            char *end = packet->packet;
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

                default:
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, 
                        c_string_create ("Got an on hold packet of unknown type in cerver %s.", 
                        packet->cerver->name->str));
                    #endif
                    break;
            }
        }

        packet_delete (packet);
    }

}

// reallocs on hold cerver poll fds
// returns 0 on success, 1 on error
static u8 cerver_realloc_on_hold_poll_fds (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver->max_on_hold_connections = cerver->max_on_hold_connections * 2;
        cerver->hold_fds = realloc (cerver->hold_fds, 
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

// we remove any fd that was set to -1 for what ever reason
static void cerver_on_hold_poll_compress (Cerver *cerver) {

    if (cerver) {
        cerver->compress_on_hold = false;

        for (u32 i = 0; i < cerver->max_on_hold_connections; i++) {
            if (cerver->hold_fds[i].fd == -1) {
                for (u32 j = i; j < cerver->max_on_hold_connections - 1; j++) 
                    cerver->hold_fds[j].fd = cerver->hold_fds[j + 1].fd;
                    
            }
        }
    }  

}

// handles packets from the on hold clients until they authenticate
static u8 on_hold_poll (void *ptr) {

    u8 retval = 1;

    if (ptr) {
        Cerver *cerver = (Cerver *) ptr;

        cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, 
            c_string_create ("Cerver %s on hold handler has started!", cerver->name->str));
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Waiting for connections to put on hold...");
        #endif

        int poll_retval = 0;
        while (cerver->isRunning && cerver->holding_connections) {
            poll_retval = poll (cerver->hold_fds, 
                cerver->max_on_hold_connections, cerver->poll_timeout);

            // poll failed
            if (poll_retval < 0) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Cerver %s on hold poll has failed!", cerver->name->str));
                perror ("Error");
                cerver->holding_connections = false;
                cerver->isRunning = false;
                break;
            }

            // if poll has timed out, just continue to the next loop... 
            if (poll_retval == 0) {
                // #ifdef CERVER_DEBUG
                // cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                //    c_string_create ("Cerver %s on hold poll timeout", cerver->name->str));
                // #endif
                continue;
            }

            // one or more fd(s) are readable, need to determine which ones they are
            for (u16 i = 0; i < cerver->current_on_hold_nfds; i++) {
                if (cerver->hold_fds[i].revents == 0) continue;
                if (cerver->hold_fds[i].revents != POLLIN) continue;

                if (thpool_add_work (cerver->thpool, cerver_receive, 
                    cerver_receive_new (cerver, cerver->hold_fds[i].fd, true))) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                        c_string_create ("Failed to add cerver_receive () to cerver's %s thpool!", 
                        cerver->name->str));
                }
            }
        }

        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, 
            c_string_create ("Cerver %s on hold poll has stopped!", cerver->name->str));
        #endif

        retval = 0;
    }

    else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Can't handle on hold clients on a NULL cerver!");

    return retval;

}

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess authentication or it failed to, so it is dropped
// returns 0 on success, 1 on error
u8 on_hold_connection (Cerver *cerver, Connection *connection) {

    u8 retval = 1;

    if (cerver && connection) {
        if (cerver->on_hold_connections) {
            i32 idx = on_hold_get_free_idx (cerver);
            if (idx >= 0) {
                cerver->hold_fds[idx].fd = connection->sock_fd;
                cerver->hold_fds[idx].events = POLLIN;
                cerver->current_on_hold_nfds++; 

                cerver->n_hold_clients++;

                avl_insert_node (cerver->on_hold_connections, connection);

                if (cerver->holding_connections == false) {
                    cerver->holding_connections = true;
                    if (thpool_add_work (cerver->thpool, (void *) on_hold_poll, cerver)) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                            c_string_create ("Failed to add on_hold_poll () to cerver's %s thpool!", 
                            cerver->name->str));
                        cerver->holding_connections = false;
                    }
                }

                #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER,
                        c_string_create ("Connection is on hold on cerver %s.",
                        cerver->name->str));
                #endif

                #ifdef CERVER_STATS
                    cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, 
                        c_string_create ("Cerver %s current on hold connections: %i.", 
                        cerver->name->str, cerver->n_hold_clients));
                #endif

                retval = 0;
            }

            else {
                #ifdef CERVER_DEBUG
                cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                    c_string_create ("Cerver %s on hold poll is full -- we need to realloc...", 
                    cerver->name->str));
                #endif
                if (cerver_realloc_on_hold_poll_fds (cerver)) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                        c_string_create ("Failed to realloc cerver %s on hold poll fds!", 
                        cerver->name->str));
                }

                else retval = on_hold_connection (cerver, connection);
            }
        }
    }

    return retval;

}

static void on_hold_poll_remove_sock_fd (Cerver *cerver, const i32 sock_fd) {

    if (cerver) {
        i32 idx = on_hold_poll_get_idx_by_sock_fd (cerver, sock_fd);
        if (idx) {
            cerver->hold_fds[idx].fd = -1;
            cerver->hold_fds[idx].events = -1;
            cerver->current_on_hold_nfds--;

            #ifdef CERVER_STATS
                cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, 
                    c_string_create ("Cerver %s current on hold connections: %i.", 
                    cerver->name->str, cerver->n_hold_clients));
            #endif

            // check if we are holding any more connections, if not, we stop the on hold poll
            if (cerver->current_on_hold_nfds <= 0) {
                #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER,
                    c_string_create ("Stoping cerver's %s on hold poll.",
                    cerver->name->str));
                #endif
                cerver->holding_connections = false;
            }
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Couldn't find %i sock fd in cerver's %s on hold poll fds.",
                sock_fd, cerver->name->str));
            #endif
        }
    }

}

// removes the connection from the on hold structures
static Connection *on_hold_connection_remove (const Cerver *cerver, const i32 sock_fd) {

    Connection *connection = NULL;

    if (cerver) {
        // remove the connection associated to the sock fd
        Connection *query = client_connection_new ();
        query->sock_fd = sock_fd;
        void *connection_data = avl_remove_node (cerver->on_hold_connections, query);
        if (connection_data) {
            connection = (Connection *) connection_data;

            // unregister the fd from the on hold structures
            on_hold_poll_remove_sock_fd ((Cerver *) cerver, sock_fd);
        }
    }

    return connection;

}

// close the on hold connection assocaited with sock fd 
// and remove it from the cerver
static void on_hold_connection_drop (Cerver *cerver, const i32 sock_fd) {

    if (cerver) {
        // remove the connection associated to the sock fd
        Connection *query = client_connection_new ();
        query->sock_fd = sock_fd;
        void *connection_data = avl_remove_node (cerver->on_hold_connections, query);
        if (connection_data) {
            Connection *connection = (Connection *) connection_data;

            // close the connection socket
            client_connection_end (connection);

            // unregister the fd from the on hold structures
            on_hold_poll_remove_sock_fd (cerver, sock_fd);

            // we can now safely delete the connection
            client_connection_delete (connection);
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Couldn't find a connection associated with %i sock fd in cerver's %s on hold connections tree.",
                sock_fd, cerver->name->str));
            #endif

            // unregister the fd from the on hold structures
            on_hold_poll_remove_sock_fd (cerver, sock_fd);

            close (sock_fd);        // just close the socket
        }
    }

}

static Connection *on_hold_connection_get_by_sock (const Cerver *cerver, const i32 sock_fd) {

    Connection *connection = NULL;

    if (cerver) {
        Connection *query = client_connection_new ();
        if (query) {
            query->sock_fd = sock_fd;
            void *connection_data = avl_get_node_data (cerver->on_hold_connections, query);
            if (connection_data) {
                connection = (Connection *) connection_data;
            }

            else {
                cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                    c_string_create ("Failed to get on hold connection associated with sock: %i", 
                    sock_fd));
            }
        }

        else {
            // cerver error allocating memory -- this might not happen
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Failed to create connection query in cerver %s.",
                cerver->name->str));
            #endif
        }    
    }

    return connection;

}