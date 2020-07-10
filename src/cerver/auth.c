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

static u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection);
static u8 on_hold_poll_register_connection (Cerver *cerver, Connection *connection);
u8 on_hold_poll_unregister_sock_fd (Cerver *cerver, const i32 sock_fd);
static u8 on_hold_poll_unregister_connection (Cerver *cerver, Connection *connection);

#pragma region data

static AuthData *auth_data_new (void) {

    AuthData *auth_data = (AuthData *) malloc (sizeof (AuthData));
    if (auth_data) {
        auth_data->token = NULL;

        auth_data->auth_data = NULL;
        auth_data->auth_data_size = 0;

        auth_data->data = NULL;
        auth_data->delete_data = NULL;
    }

    return auth_data;

}

static AuthData *auth_data_create (const char *token, void *data, size_t auth_data_size) {

    AuthData *auth_data = auth_data_new ();
    if (auth_data) {
        auth_data->token = token ? estring_new (token) : NULL;
        if (data) {
            auth_data->auth_data = malloc (auth_data_size);
            if (auth_data->auth_data) {
                memcpy (auth_data->auth_data, data, auth_data_size);
                auth_data->auth_data_size = auth_data_size;
            }

            else {
                free (auth_data);
                auth_data = NULL;
            }
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

#pragma endregion

#pragma region method

static AuthMethod *auth_method_new (void) {

    AuthMethod *auth_method = (AuthMethod *) malloc (sizeof (AuthMethod));
    if (auth_method) {
        auth_method->packet = NULL;
        auth_method->auth_data = NULL;

        auth_method->error_message = NULL;
    }

    return auth_method;

}

static AuthMethod *auth_method_create (Packet *packet, AuthData *auth_data) {

    AuthMethod *auth_method = auth_method_new ();
    if (auth_method) {
        auth_method->packet = packet;
        auth_method->auth_data = auth_data;
    }

    return auth_method;

}

static void auth_method_delete (AuthMethod *auth_method) {

    if (auth_method) {
        estring_delete (auth_method->error_message);

        free (auth_method);
    }

}

#pragma endregion

#pragma region auth

static void auth_send_success_packet (const Cerver *cerver, 
    const Client *client, const Connection *connection,
    SToken *token, size_t token_size) {

    if (cerver && connection) {
        Packet *success_packet = packet_generate_request (AUTH_PACKET, AUTH_PACKET_TYPE_SUCCESS, token, token_size);
        if (success_packet) {
            packet_set_network_values (success_packet, (Cerver *) cerver, (Client *) client, (Connection *) connection, NULL);
            packet_send (success_packet, 0, NULL, false);
            packet_delete (success_packet);
        }

        else {
            #ifdef AUTH_DEBUG
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

// creates a new client and registers the connection to it
static Client *auth_create_new_client (Packet *packet, AuthData *auth_data) {

    Client *client = NULL;

    if (packet) {
        client = client_create ();
        if (client) {
            connection_register_to_client (client, packet->connection);

            // client_set_data (client, auth_data->data, auth_data->delete_data);

            // if the cerver is configured to use sessions, we need to generate a new session id with the
            // session id generator method and add it to the client
            // this client has the first connection associated with the id & auth data
            // any new connections that authenticates using the session id (token), 
            // will be added to this client
            if (packet->cerver->use_sessions) {
                SessionData *session_data = session_data_new (packet, auth_data, client);

                char *session_id = (char *) packet->cerver->session_id_generator (session_data);
                if (session_id) {
                    #ifdef AUTH_DEBUG
                    char *s = c_string_create ("Generated client <%ld> session id: %s", 
                        client->id, session_id);
                    if (s) {
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, s);
                        free (s);
                    }
                    #endif
                    client_set_session_id (client, session_id);
                }

                else {
                    char *s = c_string_create ("auth_create_new_client () - failed to generate client's <%ld> session id!",
                        client->id);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }

                    client_connection_remove (client, packet->connection);
                    
                    // remove the connection from on hold structures to stop receiving data
                    on_hold_connection_drop (packet->cerver, packet->connection);

                    client_delete (client);
                    client = NULL;
                }

                session_data_delete (session_data);
            }
        }
    }

    return client;

}

// send an ERR_FAILED_AUTH to the connection and update connection stats
// if the connection's max auth tries has been reached, it will be dropped
static void auth_failed (Cerver *cerver, Connection *connection, const char *error_message) {

    if (cerver && connection) {
        // send failed auth packet to client
        Packet *error_packet = error_packet_generate (ERR_FAILED_AUTH, error_message);
        if (error_packet) {
            packet_set_network_values (error_packet, cerver, NULL, connection, NULL);
            packet_send (error_packet, 0, NULL, false);
            packet_delete (error_packet);
        }

        connection->auth_tries--;
        if (connection->auth_tries <= 0) {
            #ifdef AUTH_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, 
                "Connection reached max auth tries, dropping now...");
            #endif
            on_hold_connection_drop (cerver, connection);
        }
    }

}

static u8 auth_with_token_admin (const Packet *packet, const AuthData *auth_data) {

    u8 retval = 1;

    if (packet && auth_data) {
        // if we get a token, we search for an admin with the same token
        Admin *admin = admin_get_by_session_id (packet->cerver->admin, auth_data->token->str);

        // if we found a client, register the new connection to him
        if (admin) {
            #ifdef AUTH_DEBUG
            char *status = c_string_create ("Found an ADMIN with session id <%s> in cerver %s.",
                auth_data->token->str, packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, status);
                free (status);
            }
            #endif

            if (!connection_register_to_client (admin->client, packet->connection)) {
                retval = 0;
            }
        }

        else {
            char *status = c_string_create ("Failed to get ADMIN with matching session id <%s> in cerver %s",
                auth_data->token->str, packet->cerver->info->name->str);
            if (status) {
                cerver_log_error (status);
                free (status);
            }

            // if not, the token is invalid!
            auth_failed (packet->cerver, packet->connection, "Session id is invalid!");
        }
    }

    return retval;

}

static u8 auth_with_token_normal (const Packet *packet, const AuthData *auth_data) {

    u8 retval = 1;

    if (packet && auth_data) {
        // if we get a token, we search for a client with the same token
        Client *client = client_get_by_session_id (packet->cerver, auth_data->token->str);

        // if we found a client, register the new connection to him
        if (client) {
            #ifdef AUTH_DEBUG
            char *status = c_string_create ("Found a CLIENT with session id <%s> in cerver %s.",
                auth_data->token->str, packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, status);
                free (status);
            }
            #endif

            if (!connection_register_to_client (client, packet->connection)) {
                retval = 0;
            }
        }

        else {
            char *status = c_string_create ("Failed to get CLIENT with matching session id <%s> in cerver %s",
                auth_data->token->str, packet->cerver->info->name->str);
            if (status) {
                cerver_log_error (status);
                free (status);
            }

            // if not, the token is invalid!
            auth_failed (packet->cerver, packet->connection, "Session id is invalid!");
        }
    }

    return retval;

}

// authenticate a new connection using a session token
// if we find a client with that token, we register the connection to him;
// if we don't find a client, the token is invalid
// returns 0 on success, 1 on error
static u8 auth_with_token (const Packet *packet, const AuthData *auth_data, bool admin) {

    u8 retval = 1;

    if (packet && auth_data) {
        if (admin) {
            retval = auth_with_token_admin (packet, auth_data);
        }

        else {
            retval = auth_with_token_normal (packet, auth_data);
        }
    }

    return retval;

}

// calls the user defined method passing the auth data and creates a new client on success
// returns 0 on success, 1 on error
static u8 auth_with_defined_method (Packet *packet, delegate authenticate, AuthData *auth_data, Client **client) {

    u8 retval = 1;

    if (packet && auth_data) {
        AuthMethod *auth_method = auth_method_create (packet, auth_data);
        if (auth_method) {
            if (!authenticate (auth_method)) {
                #ifdef AUTH_DEBUG
                char *status = c_string_create ("Client authenticated successfully to cerver %s",
                    packet->cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, status);
                    free (status);
                }
                #endif

                *client = auth_create_new_client (packet, auth_data);
                if (*client) {
                    retval = 0;
                }
            }

            else {
                #ifdef AUTH_DEBUG
                char *status = c_string_create ("Client failed to authenticate to cerver %s",
                    packet->cerver->info->name->str);
                if (status) {
                    cerver_log_msg (stderr, LOG_DEBUG, LOG_CLIENT, status);
                    free (status);
                }
                #endif

                auth_failed (packet->cerver, packet->connection, auth_method->error_message->str);
            }   

            auth_method_delete (auth_method);
        }
    }

    return retval;

}

// strip out the auth data from the packet
static AuthData *auth_strip_auth_data (Packet *packet) {

    AuthData *auth_data = NULL;

    if (packet) {
        // check we have a big enough packet
        if (packet->data_size > sizeof (RequestData)) {
            char *end = (char *) packet->data;

            end += sizeof (RequestData);

            // check if we have a token
            if (packet->data_size == (sizeof (RequestData) + sizeof (SToken))) {
                SToken *s_token = (SToken *) (end);
                auth_data = auth_data_create (s_token->token, NULL, 0);
            }

            // we have custom data credentials
            else {
                size_t data_size = packet->data_size;
                auth_data = auth_data_create (NULL, end, data_size);
            }
        }
    }

    return auth_data;

}

static u8 auth_try_common (Packet *packet, delegate authenticate, Client **client, bool admin) {

    u8 retval = 1;

    if (packet) {
        // strip out the auth data from the packet
        AuthData *auth_data = auth_strip_auth_data (packet);
        if (auth_data) {
            // check that the cerver supports sessions
            if (packet->cerver->use_sessions) {
                if (auth_data->token) {
                    retval = auth_with_token (packet, auth_data, admin);
                }

                else {
                    // if not, we authenticate using the user defined method
                    retval = auth_with_defined_method (packet, authenticate, auth_data, client);
                }
            }

            else {
                // if not, we authenticate using the user defined method
                retval = auth_with_defined_method (packet, authenticate, auth_data, client);
            }

            auth_data_delete (auth_data);
        }

        // failed to get auth data form packet
        else {
            #ifdef AUTH_DEBUG
            char *status = c_string_create ("Failed to get auth data from packet in cerver %s",
                packet->cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
            #endif

            auth_failed (packet->cerver, packet->connection, "Missing auth data!");
        }
    }

    return retval;

}

// try to authenticate a connection using the values he sent to use
static void auth_try (Packet *packet) {

    if (packet) {
        if (packet->cerver->authenticate) {
            Client *client = NULL;
            if (!auth_try_common (packet, packet->cerver->authenticate, &client, false)) {
                // remove from on hold structures & poll array
                on_hold_connection_remove (packet->cerver, packet->connection);

                // reset connection's bad packets counter
                packet->connection->bad_packets = 0;

                if (client) {
                    // register the new client & its connection to the cerver main structures & poll
                    if (!client_register_to_cerver (packet->cerver, client)) {
                        // if we are successfull, send success packet
                        if (packet->cerver->use_sessions) {
                            SToken token = { 0 };
                            memcpy (token.token, client->session_id->str, TOKEN_SIZE);
                            
                            auth_send_success_packet (
                                packet->cerver, 
                                client, packet->connection,
                                &token, sizeof (SToken)
                            );
                        }

                        else {
                            auth_send_success_packet (
                                packet->cerver, 
                                client, packet->connection,
                                NULL, 0
                            );
                        }
                    }

                    else {
                        char *status = c_string_create ("admin_auth_try () - failed to register a new admin to cerver %s",
                            packet->cerver->info->name->str);
                        if (status) {
                            cerver_log_msg (stderr, LOG_ERROR, LOG_ADMIN, status);
                            free (status);
                        }

                        // send an error packet to the client
                        error_packet_generate_and_send (ERR_CERVER_ERROR, "Internal cerver error!",
                            packet->cerver, client, packet->connection);

                        // close the client's only connection and delete the client
                        client_connection_drop (
                            packet->cerver, 
                            client, 
                            (Connection *) client->connections->start->data
                        );

                        client_delete (client);
                    }
                }

                // added connection to client with matching id (token)
                else {
                    Client *match = client_get_by_sock_fd (packet->cerver, packet->connection->socket->sock_fd);

                    // add connection's sock fd to cerver's main poll array & to cerver structures
                    connection_add_to_cerver (packet->cerver, match, packet->connection);

                    // send success auth packet to client
                    auth_send_success_packet (packet->cerver, match, packet->connection, NULL, 0);
                }
            }
        }

        // no authentication method -- clients are not able to authenticate with to the cerver!
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

static void admin_auth_try (Packet *packet) {

    if (packet) {
        if (packet->cerver->admin) {
            if (packet->cerver->admin->authenticate) {
                Client *client = NULL;
                if (!auth_try_common (packet, packet->cerver->admin->authenticate, &client, true)) {
                    // remove from on hold structures & poll array
                    on_hold_connection_remove (packet->cerver, packet->connection);

                    // reset connection's bad packets counter
                    packet->connection->bad_packets = 0;

                    if (client) {
                        // create a new admin with client and register it to the admin
                        Admin *admin = admin_create_with_client (client);
                        if (!admin_cerver_register_admin (packet->cerver->admin, admin)) {
                            // if we are successfull, send success packet
                            if (packet->cerver->use_sessions) {
                                SToken token = { 0 };
                                memcpy (token.token, client->session_id->str, TOKEN_SIZE);
                                
                                auth_send_success_packet (
                                    packet->cerver, 
                                    client, packet->connection,
                                    &token, sizeof (SToken)
                                );
                            }

                            else {
                                auth_send_success_packet (
                                    packet->cerver, 
                                    client, packet->connection,
                                    NULL, 0
                                );
                            }
                        }

                        else {
                            char *status = c_string_create ("admin_auth_try () - failed to register a new admin to cerver %s",
                                packet->cerver->info->name->str);
                            if (status) {
                                cerver_log_msg (stderr, LOG_ERROR, LOG_ADMIN, status);
                                free (status);
                            }

                            // send an error packet to the client
                            error_packet_generate_and_send (ERR_CERVER_ERROR, "Internal cerver error!",
                                packet->cerver, client, packet->connection);

                            // close the admin's client only connection and delete the admin
                            client_connection_drop (
                                packet->cerver, 
                                admin->client, 
                                (Connection *) admin->client->connections->start->data
                            );

                            admin_delete (admin);
                        }
                    }

                    // added connection to client with matching id (token)
                    else {
                        Admin *admin = admin_get_by_sock_fd (packet->cerver->admin, packet->connection->socket->sock_fd);
                        if (admin) {
                            // register the new connection to the cerver admin's poll array
                            admin_cerver_poll_register_connection (packet->cerver->admin, packet->connection);

                            // send success auth packet to client
                            auth_send_success_packet (packet->cerver, admin->client, packet->connection, NULL, 0);
                        }
                    }
                }
            }

            // no authentication method -- clients are not able to authenticate with the cerver!
            else {
                char *status = c_string_create ("Cerver %s ADMIN does not have an authenticate method!",
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

        else {
            char *status = c_string_create ("admin_auth_try () - Cerver %s does NOT support ADMINS!",
                packet->cerver->info->name->str);
            if (status) {
                cerver_log_warning (status);
                free (status);
            }

            on_hold_connection_drop (packet->cerver, packet->connection);
        }
    }

}

#pragma endregion

#pragma region handler

static void cerver_on_hold_handle_max_bad_packets (Cerver *cerver, Connection *connection) {

    if (cerver && connection) {
        connection->bad_packets += 1;

        if (connection->bad_packets >= cerver->on_hold_max_bad_packets) {
            #ifdef AUTH_DEBUG
            cerver_log_debug ("ON HOLD Connection has reached max bad packets, dropping...");
            #endif

            on_hold_connection_drop (cerver, connection);
        }
    }

}

// handle an auth packet
static void cerver_auth_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->data_size > sizeof (RequestData)) {
            RequestData *req = (RequestData *) (packet->data);

            switch (req->type) {
                // the client sent use its data to authenticate itself
                case AUTH_PACKET_TYPE_CLIENT_AUTH: auth_try (packet); break;

                case AUTH_PACKET_TYPE_ADMIN_AUTH: admin_auth_try (packet); break;

                default: {
                    #ifdef AUTH_DEBUG
                    cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, 
                        "cerver_auth_packet_hanlder () -- got an unknwown request type");
                    #endif

                    cerver_on_hold_handle_max_bad_packets (packet->cerver, packet->connection);
                } break;
            }
        }

        else {
            // bad packet
            cerver_on_hold_handle_max_bad_packets (packet->cerver, packet->connection);
        }
    }

}

// handles an packet from an on hold connection
void on_hold_packet_handler (void *packet_ptr) {

    if (packet_ptr) {
        Packet *packet = (Packet *) packet_ptr;

        bool good = true;
        if (packet->cerver->on_hold_check_packets) {
            good = packet_check (packet);
        }

        if (good) {
            switch (packet->header->packet_type) {
                // handles authentication packets
                case AUTH_PACKET: cerver_auth_packet_handler (packet); break;

                // acknowledge the client we have received his test packet
                case TEST_PACKET: cerver_test_packet_handler (packet); break;

                default: {
                    #ifdef AUTH_DEBUG
                    char *status = c_string_create ("Got an ON HOLD packet of unknown type in cerver %s.", 
                        packet->cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, status);
                        free (status);
                    }
                    #endif

                    cerver_on_hold_handle_max_bad_packets (packet->cerver, packet->connection);
                } break;
            }
        }

        else {
            // bad packet
            cerver_on_hold_handle_max_bad_packets (packet->cerver, packet->connection);
        }

        packet_delete (packet);
    }

}

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess or failed authentication
// returns 0 on success, 1 on error
u8 on_hold_connection (Cerver *cerver, Connection *connection) {

    u8 retval = 1;

    if (cerver && connection) {
        if (cerver->on_hold_connections) {
            if (!on_hold_poll_register_connection (cerver, connection)) {
                avl_insert_node (cerver->on_hold_connections, connection);

                const void *key = &connection->socket->sock_fd;
                if (!htab_insert (cerver->on_hold_connection_sock_fd_map, 
                    key, sizeof (i32),
                    connection, sizeof (Connection)
                )) {
                    cerver_log_debug ("on_hold_connection () - inserted connection in on_hold_connection_sock_fd_map htab");

                    retval = 0;     // success
                }

                else {
                    cerver_log_error ("on_hold_connection () - failed to insert connection in on_hold_connection_sock_fd_map htab!");
                }
            }
        }
    }

    return retval;

}

// removes the connection from the on hold structures
// returns 0 on success, 1 on error
static u8 on_hold_connection_remove (const Cerver *cerver, Connection *connection) {

    u8 retval = 1;

    if (cerver && connection) {
        if (!on_hold_poll_unregister_connection ((Cerver *) cerver, connection)) {
            // remove the connection associated to the sock fd
            Connection *query = connection_new ();
            query->socket = socket_new ();
            if (query->socket) query->socket->sock_fd = connection->socket->sock_fd;

            if (avl_remove_node (cerver->on_hold_connections, query)) {
                cerver_log_debug ("on_hold_connection_remove () - removed connection from on_hold_connections avl");
            }

            else {
                cerver_log_error ("on_hold_connection_remove () - failed to remove connection from on_hold_connections avl!");
            }

            // remove connection from on hold map
            const void *key = &connection->socket->sock_fd;
            if (htab_remove (cerver->on_hold_connection_sock_fd_map, key, sizeof (i32))) {
                cerver_log_debug ("on_hold_connection_remove () - removed connection from on_hold_connection_sock_fd_map htab");
            }

            else {
                cerver_log_error ("on_hold_connection_remove () - failed to remove connection from on_hold_connection_sock_fd_map htab!");
            }

            connection_delete (query);

            retval = 0;
        }
    }

    return retval;

}

// closes the on hold connection and removes it from the cerver
void on_hold_connection_drop (const Cerver *cerver, Connection *connection) {

    if (cerver && connection) {
        // remove the connection from the on hold structures
        on_hold_connection_remove (cerver, connection);

        // close the connection socket
        connection_end (connection);

        cerver_sockets_pool_push ((Cerver *) cerver, connection->socket);
        connection->socket = NULL;

        // we can now safely delete the connection
        connection_delete (connection);
    }

}

#pragma endregion

#pragma region poll

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

// regsiters a connection to the cerver's on hold poll array
// returns 0 on success, 1 on error
static u8 on_hold_poll_register_connection (Cerver *cerver, Connection *connection) {

    u8 retval = 1;

    if (cerver && connection) {
        // pthread_mutex_lock (cerver->on_hold_poll_lock);

        i32 idx = on_hold_get_free_idx (cerver);
        if (idx >= 0) {
            cerver->hold_fds[idx].fd = connection->socket->sock_fd;
            cerver->hold_fds[idx].events = POLLIN;
            cerver->current_on_hold_nfds++;

            cerver->stats->current_n_hold_connections++;

            #ifdef AUTH_DEBUG
            char *s = c_string_create ("Added sock fd <%d> to cerver %s ON HOLD poll, idx: %i", 
                connection->socket->sock_fd, cerver->info->name->str, idx);
            if (s) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, s);
                free (s);
            }
            #endif

            #ifdef CERVER_STATS
            char *status = c_string_create ("Cerver %s current ON HOLD connections: %ld", 
                cerver->info->name->str, cerver->stats->current_n_hold_connections);
            if (status) {
                cerver_log_msg (stdout, LOG_CERVER, LOG_CERVER, status);
                free (status);
            }
            #endif

            retval = 0;
        }

        else if (idx < 0) {
            #ifdef AUTH_DEBUG
            char *s = c_string_create ("Cerver %s ON HOLD poll is full!", 
                cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_WARNING, LOG_CERVER, s);
                free (s);
            }
            #endif
        }

        // pthread_mutex_unlock (cerver->on_hold_poll_lock);
    }

    return retval;

}

// removed a sock fd from the cerver's on hold poll array
// returns 0 on success, 1 on error
u8 on_hold_poll_unregister_sock_fd (Cerver *cerver, const i32 sock_fd) {

    u8 retval = 1;

    if (cerver) {
        // pthread_mutex_lock (cerver->on_hold_poll_lock);

        i32 idx = on_hold_poll_get_idx_by_sock_fd (cerver, sock_fd);
        if (idx >= 0) {
            cerver->hold_fds[idx].fd = -1;
            cerver->hold_fds[idx].events = -1;
            cerver->current_on_hold_nfds--;

            cerver->stats->current_n_hold_connections--;

            #ifdef AUTH_DEBUG
            char *s = c_string_create ("Removed sock fd <%d> from cerver %s ON HOLD poll, idx: %d",
                sock_fd, cerver->info->name->str, idx);
            if (s) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, s);
                free (s);
            }
            #endif

            #ifdef CERVER_STATS
            char *status = c_string_create ("Cerver %s current ON HOLD connections: %ld", 
                cerver->info->name->str, cerver->stats->current_n_hold_connections);
            if (status) {
                cerver_log_msg (stdout, LOG_CERVER, LOG_CERVER, status);
                free (status);
            }
            #endif

            retval = 0;
        }

        else {
            // #ifdef AUTH_DEBUG
            char *s = c_string_create ("Sock fd <%d> was NOT found in cerver %s ON HOLD poll!",
                sock_fd, cerver->info->name->str);
            if (s) {
                cerver_log_msg (stdout, LOG_WARNING, LOG_CERVER, s);
                free (s);
            }
            // #endif
        }

        // pthread_mutex_unlock (cerver->on_hold_poll_lock);
    }

    return retval;

}

// unregsiters a connection from the cerver's on hold poll array
// returns 0 on success, 1 on error
static u8 on_hold_poll_unregister_connection (Cerver *cerver, Connection *connection) {

    return (cerver && connection) ? 
        on_hold_poll_unregister_sock_fd (cerver, connection->socket->sock_fd) : 1;

}

static inline void on_hold_poll_handle_actual (Cerver *cerver, const u32 idx, CerverReceive *cr) {

    switch (cerver->hold_fds[idx].revents) {
        // A connection setup has been completed or new data arrived
        case POLLIN: {
            printf ("on_hold_poll_handle () - Receive fd: %d\n", cerver->hold_fds[idx].fd);
                
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
            if (cerver->hold_fds[idx].revents != 0) {
                // 17/06/2020 -- 15:06 -- handle as failed any other signal
                // to avoid hanging up at 100% or getting a segfault
                cerver_switch_receive_handle_failed (cr);
            }
        } break;
    }

}

static inline void on_hold_poll_handle (Cerver *cerver) {

    if (cerver) {
        // pthread_mutex_lock (cerver->on_hold_poll_lock);

        // one or more fd(s) are readable, need to determine which ones they are
        for (u32 idx = 0; idx < cerver->max_on_hold_connections; idx++) {
            if (cerver->hold_fds[idx].fd > -1) {
                if (cerver->hold_fds[idx].fd != -1) {
                    CerverReceive *cr = cerver_receive_create (RECEIVE_TYPE_ON_HOLD, cerver, cerver->hold_fds[idx].fd);
                    if (cr) {
                        on_hold_poll_handle_actual (cerver, idx, cr);
                    }
                }
            }
        }

        // pthread_mutex_unlock (cerver->on_hold_poll_lock);
    }

}

// handles packets from the on hold clients until they authenticate
void *on_hold_poll (void *cerver_ptr) {

    if (cerver_ptr) {
        Cerver *cerver = (Cerver *) cerver_ptr;

        char *status = c_string_create ("Cerver %s ON HOLD poll has started!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, status);
            free (status);
        }

        char *thread_name = c_string_create ("%s-on-hold", cerver->info->name->str);
        if (thread_name) {
            thread_set_name (thread_name);
            free (thread_name);
        }

        #ifdef AUTH_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Waiting for connections to put on hold...");
        #endif

        int poll_retval = 0;
        while (cerver->isRunning) {
            poll_retval = poll (cerver->hold_fds, cerver->max_on_hold_connections, cerver->on_hold_poll_timeout);

            switch (poll_retval) {
                case -1: {
                    char *status = c_string_create ("Cerver %s ON HOLD poll has failed!", cerver->info->name->str);
                    if (status) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                        free (status);
                    }

                    perror ("Error");
                    cerver->isRunning = false;
                } break;

                case 0: {
                    // #ifdef AUTH_DEBUG
                    // char *status = c_string_create ("Cerver %s ON HOLD poll timeout", cerver->info->name->str);
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

        #ifdef AUTH_DEBUG
        status = c_string_create ("Cerver %s ON HOLD poll has stopped!", cerver->info->name->str);
        if (status) {
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, status);
            free (status);
        }
        #endif
    }

    else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Can't handle ON HOLD clients on a NULL cerver!");

    return NULL;

}

#pragma endregion