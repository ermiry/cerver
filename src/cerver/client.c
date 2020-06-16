#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <errno.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/auth.h"
#include "cerver/handler.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dllist.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

void client_receive (Client *client, Connection *connection);

static u64 next_client_id = 0;

#pragma region stats

static ClientStats *client_stats_new (void) {

    ClientStats *client_stats = (ClientStats *) malloc (sizeof (ClientStats));
    if (client_stats) {
        memset (client_stats, 0, sizeof (ClientStats));
        client_stats->received_packets = packets_per_type_new ();
        client_stats->sent_packets = packets_per_type_new ();
    } 

    return client_stats;

}

static inline void client_stats_delete (ClientStats *client_stats) { 
    
    if (client_stats) {
        packets_per_type_delete (client_stats->received_packets);
        packets_per_type_delete (client_stats->sent_packets);

        free (client_stats); 
    } 
    
}

#pragma endregion

#pragma region main

Client *client_new (void) {

    Client *client = (Client *) malloc (sizeof (Client));
    if (client) {
        memset (client, 0, sizeof (Client));

        client->session_id = NULL;

        client->connections = NULL;

        client->drop_client = false;

        client->data = NULL;
        client->delete_data = NULL;

        client->running = false;

        client->app_packet_handler = NULL;
        client->app_error_packet_handler = NULL;
        client->custom_packet_handler = NULL;

        client->stats = NULL;   
    }

    return client;

}

void client_delete (void *ptr) {

    if (ptr) {
        Client *client = (Client *) ptr;

        estring_delete (client->session_id);

        dlist_delete (client->connections);

        if (client->data) {
            if (client->delete_data) client->delete_data (client->data);
            else free (client->data);
        }

        client_stats_delete (client->stats);

        free (client);
    }

}

void client_delete_dummy (void *ptr) {}

// creates a new client and inits its values
Client *client_create (void) {

    Client *client = client_new ();
    if (client) {
        // init client values
        client->id = next_client_id;
        next_client_id += 1;
        time (&client->connected_timestamp);
        client->connections = dlist_init (connection_delete, connection_comparator);
        client->stats = client_stats_new ();
    }

    return client;

}

// creates a new client and registers a new connection
Client *client_create_with_connection (Cerver *cerver, 
    const i32 sock_fd, const struct sockaddr_storage address) {

    Client *client = client_create ();
    if (client) {
        Connection *connection = connection_create (sock_fd, address, cerver->protocol);
        if (connection) connection_register_to_client (client, connection);
        else {
            // failed to create a new connection
            client_delete (client);
            client = NULL;
        }
    }

    return client;

}

// sets the client's session id
void client_set_session_id (Client *client, const char *session_id) {

    if (client) {
        if (client->session_id) estring_delete (client->session_id);
        client->session_id = session_id ? estring_new (session_id) : NULL;
    }

}

// returns the client's app data
void *client_get_data (Client *client) { return (client ? client->data : NULL); }

// sets client's data and a way to destroy it
// deletes the previous data of the client
void client_set_data (Client *client, void *data, Action delete_data) {

    if (client) {
        if (client->data) {
            if (client->delete_data) client->delete_data (client->data);
            else free (client->data);
        }

        client->data = data;
        client->delete_data = delete_data;
    }

}

// sets the client packet handlers
void client_set_handlers (Client *client, 
    Action app_handler, Action app_error_handler, Action custom_handler) {

    if (client) {
        client->app_packet_handler = app_handler;
        client->app_error_packet_handler = app_error_handler;
        client->custom_packet_handler = custom_handler;
    }

}

// compare clients based on their client ids
int client_comparator_client_id (const void *a, const void *b) {

    if (a && b) {
        Client *client_a = (Client *) a;
        Client *client_b = (Client *) b;

        if (client_a->id < client_b->id) return -1;
        else if (client_a->id == client_b->id) return 0;
        else return 1;
    }

    return 0;

}

// compare clients based on their session ids
int client_comparator_session_id (const void *a, const void *b) {

    if (a && b) return estring_compare (((Client *) a)->session_id, ((Client *) b)->session_id);
    if (a && !b) return -1;
    if (!a && b) return 1;

    return 0;

}

// closes all client connections
u8 client_disconnect (Client *client) {

    u8 retval = 1;

    if (client) {
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            connection_end (connection);
        }

        retval = 0;
    }

    return retval;

}

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
void client_drop (Cerver *cerver, Client *client) {

    if (cerver && client) {
        client_unregister_from_cerver (cerver, client);
        client_delete (client);
    }

}

// adds a new connection to the end of the client to the client's connection list
// without adding it to any other structure
// returns 0 on success, 1 on error
u8 client_add_connection (Client *client, Connection *connection) {

    u8 retval = 1;

    if (client && connection) {
        retval = dlist_insert_after (client->connections, dlist_end (client->connections), connection);
    }

    return retval;

}

// removes the connection from the client
// and also checks if there is another active connection in the client, if not it will be dropped
// returns 0 on success, 1 on error
u8 client_remove_connection (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        // check if the connection actually belongs to the client
        if (connection_check_owner (client, connection)) {
            connection_unregister_from_client (cerver, client, connection);

            // if the client does not have any active connection, drop it
            if (client->connections->size <= 0) client_drop (cerver, client);

            retval = 0;
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("client_remove_connection () - Client with id" 
                "%ld does not have a connection related to sock fd %d",
                client->id, connection->socket->sock_fd);
            if (s) {
                cerver_log_msg (stderr, LOG_WARNING, LOG_CLIENT, s);
                free (s);
            }
            #endif
        }
    }

    return retval;

}

// removes the connection from the client referred to by the sock fd
// and also checks if there is another active connection in the client, if not it will be dropped
// returns 0 on success, 1 on error
u8 client_remove_connection_by_sock_fd (Cerver *cerver, Client *client, i32 sock_fd) {

    u8 retval = 1;

    if (cerver && client) {
        Connection *connection = NULL;
        switch (client->connections->size) {
            case 0: {
                #ifdef CERVER_DEBUG
                char *s = c_string_create ("client_remove_connection_by_sock_fd () - Client with id " 
                    "%ld does not have ANY connection - removing him from cerver...",
                    client->id);
                if (s) {
                    cerver_log_warning (s);
                    free (s);
                }
                #endif
                
                client_remove_from_cerver (cerver, client);
                client_delete (client);
            } break;

            case 1: {
                connection = (Connection *) client->connections->start->data;

                retval = connection_remove (
                    cerver,
                    client,
                    connection,
                    NULL
                );

                // no connections left in cerver, just remove and delete
                client_remove_from_cerver (cerver, client);
                client_delete (client);
            } break;

            default: {
                // search the connection in the client
                connection = connection_get_by_sock_fd_from_client (client, sock_fd);
                if (connection) {
                    retval = connection_remove (
                        cerver,
                        client,
                        connection,
                        NULL
                    );
                }

                else {
                    // the connection may not belong to this client
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("client_remove_connection_by_sock_fd () - Client with id " 
                        "%ld does not have a connection related to sock fd %d",
                        client->id, sock_fd);
                    if (s) {
                        cerver_log_msg (stderr, LOG_WARNING, LOG_CLIENT, s);
                        free (s);
                    }
                    #endif
                }
            } break;
        }
    }

    return retval;

}

// registers all the active connections from a client to the cerver's structures (like maps)
// returns 0 on success registering at least one, 1 if all connections failed
u8 client_register_connections_to_cerver (Cerver *cerver, Client *client) {

    u8 retval = 1;

    if (cerver && client) {
        u8 n_failed = 0;          // n connections that failed to be registered

        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            if (connection_register_to_cerver (cerver, client, connection))
                n_failed++;
        }

         // check how many connections have failed
        if (n_failed == client->connections->size) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Failed to register all the connections for client %ld (id) to cerver %s",
                client->id, cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, s);
                free (s);
            }
            #endif

            client_drop (cerver, client);       // drop the client ---> no active connections
        }

        else retval = 0;        // at least one connection is active
    }

    return retval;

}

// unregiters all the active connections from a client from the cerver's structures (like maps)
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
u8 client_unregister_connections_from_cerver (Cerver *cerver, Client *client) {

    u8 retval = 1;

    if (cerver && client) {
        u8 n_failed = 0;        // n connections that failed to unregister

        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            if (connection_unregister_from_cerver (cerver, client, connection))
                n_failed++;
        }

        // check how many connections have failed
        if ((n_failed > 0) && (n_failed == client->connections->size)) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Failed to unregister all the connections for client %ld (id) from cerver %s",
                client->id, cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, s);
                free (s);
            }
            #endif

            // client_drop (cerver, client);       // drop the client ---> no active connections
        }

        else retval = 0;        // at least one connection is active
    }

    return retval;

}

// registers all the active connections from a client to the cerver's poll
// returns 0 on success registering at least one, 1 if all connections failed
u8 client_register_connections_to_cerver_poll (Cerver *cerver, Client *client) {

    u8 retval = 1;

    if (cerver && client) {
        u8 n_failed = 0;          // n connections that failed to be registered

        // register all the client connections to the cerver poll
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            if (connection_register_to_cerver_poll (cerver, client, connection))
                n_failed++;
        }

        // check how many connections have failed
        if (n_failed == client->connections->size) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Failed to register all the connections for client %ld (id) to cerver %s poll",
                client->id, cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, s);
                free (s);
            }
            #endif

            client_drop (cerver, client);       // drop the client ---> no active connections
        }

        else retval = 0;        // at least one connection is active
    }

    return retval;

}

// unregisters all the active connections from a client from the cerver's poll 
// returns 0 on success unregistering at least 1 connection, 1 failed to unregister all
u8 client_unregister_connections_from_cerver_poll (Cerver *cerver, Client *client) {

    u8 retval = 1;

    if (cerver && client) {
        u8 n_failed = 0;        // n connections that failed to unregister

        // unregister all the client connections from the cerver poll
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            if (connection_unregister_from_cerver_poll (cerver, client, connection))
                n_failed++;
        }

        // check how many connections have failed
        if (n_failed == client->connections->size) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Failed to unregister all the connections for client %ld (id) from cerver %s poll",
                client->id, cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, s);
                free (s);
            }
            #endif

            client_drop (cerver, client);       // drop the client ---> no active connections
        }

        else retval = 0;        // at least one connection is active
    }

    return retval;

}

// 07/06/2020
// removes the client from cerver data structures, not taking into account its connections
Client *client_remove_from_cerver (Cerver *cerver, Client *client) {

    Client *retval = NULL;

    if (cerver && client) {
        void *client_data = avl_remove_node (cerver->clients, client);
        if (client_data) {
            retval = (Client *) client_data;

            char *s = NULL;
            #ifdef CERVER_DEBUG
            s = c_string_create ("Unregistered a client from cerver %s.", cerver->info->name->str);
            if (s) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, s);
                free (s);
            }
            #endif

            cerver->stats->current_n_connected_clients--;
            #ifdef CERVER_STATS
            s = c_string_create ("Connected clients to cerver %s: %i.", 
                cerver->info->name->str, cerver->stats->current_n_connected_clients);
            if (s) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, s);
                free (s);
            }
            #endif
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Received NULL ptr when attempting to remove a client from cerver's %s client tree.", 
                cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                free (s);
            }
            #endif
        }
    }

    return retval;

}

// registers a client to the cerver --> add it to cerver's structures
// registers all of the current active client connections to the cerver poll
// returns 0 on success, 1 on error
u8 client_register_to_cerver (Cerver *cerver, Client *client) {

    u8 retval = 1;

    if (cerver && client) {
        if (!client_register_connections_to_cerver (cerver, client) 
            && !client_register_connections_to_cerver_poll (cerver, client)) {
            // register the client to the cerver client's
            avl_insert_node (cerver->clients, client);

            char *s = NULL;
            #ifdef CERVER_DEBUG
            s = c_string_create ("Registered a new client to cerver %s.", cerver->info->name->str);
            if (s) {
                cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, s);
                free (s);
            }
            #endif
            
            cerver->stats->total_n_clients++;
            cerver->stats->current_n_connected_clients++;
            #ifdef CERVER_STATS
            s = c_string_create ("Connected clients to cerver %s: %i.", 
                cerver->info->name->str, cerver->stats->current_n_connected_clients);
            if (s) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, s);
                free (s);
            }
            #endif

            retval = 0;
        }
    }

    return retval;

}

// unregisters a client from a cerver -- removes it from cerver's structures
Client *client_unregister_from_cerver (Cerver *cerver, Client *client) {

    Client *retval = NULL;

    if (cerver && client) {
        if (client->connections->size > 0) {
            // unregister the connections from the cerver
            client_unregister_connections_from_cerver (cerver, client);

            // unregister all the client connections from the cerver
            // client_unregister_connections_from_cerver (cerver, client);
            Connection *connection = NULL;
            for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
                connection = (Connection *) le->data;
                connection_unregister_from_cerver_poll (cerver, client, connection);
            }
        }

        // remove the client from the cerver's clients
        retval = client_remove_from_cerver (cerver, client);
    }

    return retval;

}

// gets the client associated with a sock fd using the client-sock fd map
Client *client_get_by_sock_fd (Cerver *cerver, i32 sock_fd) {

    Client *client = NULL;

    if (cerver) {
        const i32 *key = &sock_fd;
        void *client_data = htab_get (
            cerver->client_sock_fd_map, 
            key, sizeof (i32)
        );
        
        if (client_data) client = (Client *) client_data;
    }

    return client;

}

// searches the avl tree to get the client associated with the session id
// the cerver must support sessions
Client *client_get_by_session_id (Cerver *cerver, char *session_id) {

    Client *client = NULL;

    if (session_id) {
        // create our search query
        Client *client_query = client_new ();
        if (client_query) {
            client_set_session_id (client_query, session_id);

            void *data = avl_get_node_data (cerver->clients, client_query, NULL);
            if (data) client = (Client *) data;     // found

            client_delete (client_query);
        }
    }

    return client;

}

// broadcast a packet to all clients inside an avl structure
void client_broadcast_to_all_avl (AVLNode *node, Cerver *cerver, Packet *packet) {

    if (node && cerver && packet) {
        client_broadcast_to_all_avl (node->right, cerver, packet);

        // send the packet to current client
        if (node->id) {
            Client *client = (Client *) node->id;

            // send the packet to all of its active connections
            Connection *connection = NULL;
            for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
                connection = (Connection *) le->data;
                packet_set_network_values (packet, cerver, client, connection, NULL);
                packet_send (packet, 0, NULL, false);
            }
        }

        client_broadcast_to_all_avl (node->left, cerver, packet);
    }

}

#pragma endregion

#pragma region aux

static ClientConnection *client_connection_aux_new (Client *client, Connection *connection) {

    ClientConnection *cc = (ClientConnection *) malloc (sizeof (ClientConnection));
    if (cc) {
        cc->connection_thread_id = 0;   
        cc->client = client;
        cc->connection = connection;
    }

    return cc;

}

void client_connection_aux_delete (ClientConnection *cc) { if (cc) free (cc); }

#pragma endregion

#pragma region client

static u8 client_start (Client *client) {

    u8 retval = 1;

    if (client) {
        // check if we walready have the client poll running
        if (!client->running) {
            time (&client->time_started);
            client->running = true;
            retval = 0;
        }
    }

    return retval;

}

// creates a new connection that is ready to connect and registers it to the client
Connection *client_connection_create (Client *client,
    const char *ip_address, u16 port, Protocol protocol, bool use_ipv6) {

    Connection *connection = NULL;

    if (client) {
        connection = connection_create_empty ();
        if (connection) {
            connection_set_values (connection, ip_address, port, protocol, use_ipv6);
            connection_init (connection);
            connection_register_to_client (client, connection);
        }
    }

    return connection;

}

#pragma region connect

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
unsigned int client_connect (Client *client, Connection *connection) {

    unsigned int retval = 1;

    if (client && connection) {
        if (!connection_connect (connection)) {
            // client_event_trigger (client, EVENT_CONNECTED);
            // connection->connected = true;
            connection->active = true;
            time (&connection->connected_timestamp);
            
            client_start (client);

            retval = 0;     // success - connected to cerver
        }
    }

    return retval;

}

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet 
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
unsigned int client_connect_to_cerver (Client *client, Connection *connection) {

    unsigned int retval = 1;

    if (!client_connect (client, connection)) {
        client_receive (client, connection);

        retval = 0;
    }

    return retval;

}

static void *client_connect_thread (void *client_connection_ptr) {

    if (client_connection_ptr) {
        ClientConnection *cc = (ClientConnection *) client_connection_ptr;

        if (!connection_connect (cc->connection)) {
            // client_event_trigger (cc->client, EVENT_CONNECTED);
            // cc->connection->connected = true;
            cc->connection->active = true;
            time (&cc->connection->connected_timestamp);
            
            client_start (cc->client);
        }

        client_connection_aux_delete (cc);
    }

    return NULL;

}

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// open a success connection, EVENT_CONNECTED will be triggered, otherwise, EVENT_CONNECTION_FAILED will be triggered
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
unsigned int client_connect_async (Client *client, Connection *connection) {

    unsigned int retval = 1;

    if (client && connection) {
        ClientConnection *cc = client_connection_aux_new (client, connection);
        if (cc) {
            if (!thread_create_detachable (&cc->connection_thread_id, client_connect_thread, cc)) {
                retval = 0;         // success
            }

            else {
                #ifdef CLIENT_DEBUG
                client_log_error ("Failed to create client_connect_thread () detachable thread!");
                #endif
            }
        }
    }

    return retval;

}

#pragma endregion

#pragma region requests

// when a client is already connected to the cerver, a request can be made to the cerver
// and the result will be returned
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
unsigned int client_request_to_cerver (Client *client, Connection *connection, Packet *request) {

    unsigned int retval = 1;

    if (client && connection && request) {
        // send the request to the cerver
        packet_set_network_values (request, NULL, client, connection, NULL);

        size_t sent = 0;
        if (!packet_send (request, 0, &sent, false)) {
            printf ("Request to cerver: %ld\n", sent);

            // receive the data directly
            connection->full_packet = false;
            while (!connection->full_packet) {
                client_receive (client, connection);
            }

            retval = 0;
        }

        else {
            #ifdef CLIENT_DEBUG
            client_log_error ("client_request_to_cerver () - failed to send request packet!");
            #endif
        }
    }

    return retval;

}

static void *client_request_to_cerver_thread (void *cc_ptr) {

    if (cc_ptr) {
        ClientConnection *cc = (ClientConnection *) cc_ptr;

        cc->connection->full_packet = false;
        while (!cc->connection->full_packet) {
            client_receive (cc->client, cc->connection);
        }

        client_connection_aux_delete (cc);
    }

    return NULL;

}

// when a client is already connected to the cerver, a request can be made to the cerver
// the result will be placed inside the connection
// this method will NOT block and the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// returns 0 on success request, 1 on error
unsigned int client_request_to_cerver_async (Client *client, Connection *connection, Packet *request) {

    unsigned int retval = 1;

    if (client && connection && request) {
        // send the request to the cerver
        packet_set_network_values (request, NULL, client, connection, NULL);
        if (!packet_send (request, 0, NULL, false)) {
            ClientConnection *cc = client_connection_aux_new (client, connection);
            if (cc) {
                // create a new thread to receive & handle the response
                if (!thread_create_detachable (&cc->connection_thread_id, client_request_to_cerver_thread, cc)) {
                    retval = 0;         // success
                }

                else {
                    #ifdef CLIENT_DEBUG
                    client_log_error ("Failed to create client_request_to_cerver_thread () detachable thread!");
                    #endif
                }
            }
        }

        else {
            #ifdef CLIENT_DEBUG
            client_log_error ("client_request_to_cerver_async () - failed to send request packet!");
            #endif
        }
    }

    return retval;

}

#pragma endregion

// this is a blocking method and ONLY works for cerver packets
// connects the client connection and makes a first request to the cerver
// then listen for packets until the target one is received, 
// then it returns the packet data as it is
// returns 0 on success, 1 on error
int client_connection_request_to_cerver (Client *client, Connection *connection, Packet *request_packet) {

    int retval = 1;

    if (client && connection) {
        // connection->sock_receive = sock_receive_new ();
        if (!connection_connect (connection)) {
            client_start (client);
            connection->active = true;

            // send the request to the cerver
            packet_set_network_values (request_packet, NULL, client, connection, NULL);
            packet_send (request_packet, 0, NULL, false);
            // packet_delete (request_packet);

            // read incoming buffer from cerver
            while (client->running && connection->active) 
                client_receive (client, connection);

            retval = 0;
        }
    }

    return retval;

}

// starts a client connection -- used to connect a client to another server
// returns only after a success or failed connection
// returns 0 on success, 1 on error
int client_connection_start (Client *client, Connection *connection) {

    int retval = 1;

    if (client && connection) {
        if (!connection_connect (connection)) {
            time (&connection->connected_timestamp);
            connection->active = true;

            if (!thread_create_detachable (
                &connection->update_thread_id,
                (void *(*)(void *)) connection_update,
                client_connection_aux_new (client, connection)
            )) {
                client_start (client);
                retval = 0;         // success
            }

            else {
                char *s = c_string_create ("client_connection_start () - Failed to create update thread for client %ld", 
                    client->id);
                if (s) {
                    cerver_log_error (s);
                    free (s);
                }
            }
        }
    }

    return retval;

}

static void client_connection_start_wrapper (void *data_ptr) {

    if (data_ptr) {
        ClientConnection *cc = (ClientConnection *) data_ptr;
        client_connection_start (cc->client, cc->connection);
        client_connection_aux_delete (cc);
    }

}

// starts the client connection async -- creates a new thread to handle how to connect with server
// returns 0 on success, 1 on error
int client_connection_start_async (Client *client, Connection *connection) {

    int retval = 1;

    if (client && connection) {
        pthread_t thread_id = 0;
        
        if (!thread_create_detachable (
            &thread_id,
            (void *(*) (void *)) client_connection_start_wrapper,
            client_connection_aux_new (client, connection)
        )) {
            retval = 0;         // success
        }

        else {
            // error
            cerver_log_error ("client_connection_start_async () - Failed to create and detatch thread!");
        }
    }

    return retval;

}

// ends a connection with a cerver by sending a disconnect packet and the closing the connection
static void client_connection_terminate (Client *client, Connection *connection) {

    if (connection) {
        if (connection->active) {
            if (connection->connected_to_cerver) {
                // send a close connection packet
                Packet *packet = packet_generate_request (REQUEST_PACKET, CLIENT_CLOSE_CONNECTION, NULL, 0);
                if (packet) {
                    packet_set_network_values (packet, NULL, client, connection, NULL);
                    packet_send (packet, 0, NULL, false);
                    packet_delete (packet);
                }
            }

            connection_end (connection);
        } 
    }

}

// terminates and destroy a connection registered to a client
// that is connected to a cerver
// returns 0 on success, 1 on error
int client_connection_end (Client *client, Connection *connection) {

    int retval = 1;

    if (client && connection) {
        client_connection_terminate (client, connection);

        connection_delete (dlist_remove_element (client->connections, 
            dlist_get_element (client->connections, connection, NULL)));

        retval = 0;
    }

    return retval;

}

// stop any on going connection and process then, destroys the client
u8 client_teardown (Client *client) {

    u8 retval = 1;

    if (client) {
        client->running = false;

        // end any ongoing connection
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            client_connection_terminate (client, (Connection *) le->data);
        }

        // delete all connections
        dlist_delete (client->connections);
        client->connections = NULL;

        client_delete (client);

        retval = 0;
    }

    return retval;

}

#pragma region handler 

// the client handles a packet based on its type
static void client_packet_handler (void *data) {

    if (data) {
        Packet *packet = (Packet *) data;
        packet->client->stats->n_packets_received += 1;

        // if (!packet_check (packet)) {
            switch (packet->header->packet_type) {
                // handles cerver type packets
                case CERVER_PACKET:
                    // packet->client->stats->received_packets->n_cerver_packets += 1; 
                    client_cerver_packet_handler (packet); 
                    break;

                // handles an error from the server
                case ERROR_PACKET: 
                    packet->client->stats->received_packets->n_error_packets += 1;
                    // error_packet_handler (packet); 
                    break;

                // handles authentication packets
                case AUTH_PACKET: 
                    packet->client->stats->received_packets->n_auth_packets += 1;
                    // client_auth_packet_handler (packet); 
                    break;

                // handles a request made from the server
                case REQUEST_PACKET: 
                    // packet->client->stats->received_packets->n_request_packets += 1; 
                    // client_request_packet_handler (packet);
                    break;

                // handles a game packet sent from the server
                case GAME_PACKET: 
                    // packet->client->stats->received_packets->n_game_packets += 1;
                    // client_game_packet_handler (packet);
                    break;

                // user set handler to handle app specific errors
                case APP_ERROR_PACKET: 
                    packet->client->stats->received_packets->n_app_error_packets += 1;
                    if (packet->client->app_error_packet_handler)
                        packet->client->app_error_packet_handler (packet);
                    break;

                // user set handler to handler app specific packets
                case APP_PACKET:
                    packet->client->stats->received_packets->n_app_packets += 1;
                    if (packet->client->app_packet_handler)
                        packet->client->app_packet_handler (packet);
                    break;

                // custom packet hanlder
                case CUSTOM_PACKET: 
                    packet->client->stats->received_packets->n_custom_packets += 1;
                    if (packet->client->custom_packet_handler)
                        packet->client->custom_packet_handler (packet);
                    break;

                // handles a test packet form the cerver
                case TEST_PACKET: 
                    packet->client->stats->received_packets->n_test_packets += 1;
                    cerver_log_msg (stdout, LOG_TEST, LOG_NO_TYPE, "Got a test packet from cerver.");
                    break;

                default:
                    packet->client->stats->received_packets->n_bad_packets += 1;
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_WARNING, LOG_NO_TYPE, "Got a packet of unknown type.");
                    #endif
                    break;
            }
        // }

        packet_delete (packet);
    }

}

static SockReceive *client_receive_handle_spare_packet (Client *client, Connection *connection, 
    size_t buffer_size, char **end, size_t *buffer_pos) {

    if (connection->sock_receive) {
        if (connection->sock_receive->header) {
            // copy the remaining header size
            memcpy (connection->sock_receive->header_end, (void *) *end, connection->sock_receive->remaining_header);

            connection->sock_receive->complete_header = true;
        }

        if (connection->sock_receive->spare_packet) {
            size_t copy_to_spare = 0;
            if (connection->sock_receive->missing_packet < buffer_size) 
                copy_to_spare = connection->sock_receive->missing_packet;

            else copy_to_spare = buffer_size;

            // append new data from buffer to the spare packet
            if (copy_to_spare > 0) {
                packet_append_data (connection->sock_receive->spare_packet, *end, copy_to_spare);

                // check if we can handler the packet 
                size_t curr_packet_size = connection->sock_receive->spare_packet->data_size + sizeof (PacketHeader);
                if (connection->sock_receive->spare_packet->header->packet_size == curr_packet_size) {
                    connection->sock_receive->spare_packet->client = client;
                    connection->sock_receive->spare_packet->connection = connection;
                    client_packet_handler (connection->sock_receive->spare_packet);

                    connection->sock_receive->spare_packet = NULL;
                    connection->sock_receive->missing_packet = 0;
                }

                else connection->sock_receive->missing_packet -= copy_to_spare;

                // offset for the buffer
                if (copy_to_spare < buffer_size) *end += copy_to_spare;
                *buffer_pos += copy_to_spare;
            }
        }
    }

    return connection->sock_receive;

}

// splits the entry buffer in packets of the correct size
static void client_receive_handle_buffer (Client *client, Connection *connection, 
    char *buffer, size_t buffer_size) {

    if (buffer && (buffer_size > 0)) {
        char *end = buffer;
        size_t buffer_pos = 0;

        SockReceive *sock_receive = client_receive_handle_spare_packet (client, connection, 
            buffer_size, &end, &buffer_pos);

        PacketHeader *header = NULL;
        size_t packet_size = 0;
        // char *packet_data = NULL;

        size_t remaining_buffer_size = 0;
        size_t packet_real_size = 0;
        size_t to_copy_size = 0;

        bool spare_header = false;

        while (buffer_pos < buffer_size) {
            remaining_buffer_size = buffer_size - buffer_pos;

            if (sock_receive->complete_header) {
                packet_header_copy (&header, (PacketHeader *) sock_receive->header);
                // header = ((PacketHeader *) sock_receive->header);
                // packet_header_print (header);

                end += sock_receive->remaining_header;
                buffer_pos += sock_receive->remaining_header;
                // printf ("buffer pos after copy to header: %ld\n", buffer_pos);

                // reset sock header values
                free (sock_receive->header);
                sock_receive->header = NULL;
                sock_receive->header_end = NULL;
                // sock_receive->curr_header_pos = 0;
                // sock_receive->remaining_header = 0;
                sock_receive->complete_header = false;

                spare_header = true;
            }

            else if (remaining_buffer_size >= sizeof (PacketHeader)) {
                header = (PacketHeader *) end;
                end += sizeof (PacketHeader);
                buffer_pos += sizeof (PacketHeader);

                // packet_header_print (header);

                spare_header = false;
            }

            if (header) {
                // check the packet size
                packet_size = header->packet_size;
                if ((packet_size > 0) /* && (packet_size < 65536) */) {
                    // printf ("packet_size: %ld\n", packet_size);
                    // end += sizeof (PacketHeader);
                    // buffer_pos += sizeof (PacketHeader);
                    // printf ("first buffer pos: %ld\n", buffer_pos);

                    Packet *packet = packet_new ();
                    if (packet) {
                        packet_header_copy (&packet->header, header);
                        packet->packet_size = header->packet_size;
                        // packet->cerver = cerver;
                        // packet->lobby = lobby;
                        packet->client = client;
                        packet->connection = connection;

                        if (spare_header) {
                            free (header);
                            header = NULL;
                        }

                        // check for packet size and only copy what is in the current buffer
                        packet_real_size = packet->header->packet_size - sizeof (PacketHeader);
                        to_copy_size = 0;
                        if ((remaining_buffer_size - sizeof (PacketHeader)) < packet_real_size) {
                            sock_receive->spare_packet = packet;

                            if (spare_header) to_copy_size = buffer_size - sock_receive->remaining_header;
                            else to_copy_size = remaining_buffer_size - sizeof (PacketHeader);
                            
                            sock_receive->missing_packet = packet_real_size - to_copy_size;
                        }

                        else {
                            to_copy_size = packet_real_size;
                            packet_delete (sock_receive->spare_packet);
                            sock_receive->spare_packet = NULL;
                        } 

                        // printf ("to copy size: %ld\n", to_copy_size);
                        packet_set_data (packet, (void *) end, to_copy_size);

                        end += to_copy_size;
                        buffer_pos += to_copy_size;
                        // printf ("second buffer pos: %ld\n", buffer_pos);

                        if (!sock_receive->spare_packet) {
                            client_packet_handler (packet);
                        }
                            
                    }

                    else {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                            "Failed to create a new packet in cerver_handle_receive_buffer ()");
                    }
                }

                else {
                    char *status = c_string_create ("Got a packet of invalid size: %ld", packet_size);
                    if (status) {
                        cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, status); 
                        free (status);
                    }

                    // FIXME: what to do next?
                    
                    break;
                }
            }

            else {
                if (sock_receive->spare_packet) packet_append_data (sock_receive->spare_packet, (void *) end, remaining_buffer_size);

                else {
                    // handle part of a new header
                    // #ifdef CERVER_DEBUG
                    // cerver_log_debug ("Handle part of a new header...");
                    // #endif

                    // copy the piece of possible header that was cut of between recv ()
                    sock_receive->header = malloc (sizeof (PacketHeader));
                    memcpy (sock_receive->header, (void *) end, remaining_buffer_size);

                    sock_receive->header_end = (char *) sock_receive->header;
                    sock_receive->header_end += remaining_buffer_size;

                    // sock_receive->curr_header_pos = remaining_buffer_size;
                    sock_receive->remaining_header = sizeof (PacketHeader) - remaining_buffer_size;

                    // printf ("curr header pos: %d\n", sock_receive->curr_header_pos);
                    // printf ("remaining header: %d\n", sock_receive->remaining_header);

                    buffer_pos += remaining_buffer_size;
                }
            }
        }
    }

}

// handles a failed recive from a connection associatd with a client
// end sthe connection to prevent seg faults or signals for bad sock fd
static void client_receive_handle_failed (Client *client, Connection *connection) {

    if (client && connection) {
        if (!client_connection_end (client, connection)) {
            // check if the client has any other active connection
            if (client->connections->size <= 0) {
                client->running = false;
            }
        }
    }

}

// receives incoming data from the socket
void client_receive (Client *client, Connection *connection) {

    if (client && connection) {
        char *packet_buffer = (char *) calloc (connection->receive_packet_buffer_size, sizeof (char));
        if (packet_buffer) {
            ssize_t rc = recv (connection->socket->sock_fd, packet_buffer, connection->receive_packet_buffer_size, 0);

            if (rc < 0) {
                if (errno != EWOULDBLOCK) {     // no more data to read 
                    #ifdef CERVER_DEBUG 
                    char *s = c_string_create ("client_receive () - rc < 0 - sock fd: %d", 
                        connection->socket->sock_fd);
                    if (s) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                        free (s);
                    }
                    perror ("Error");
                    #endif

                    client_receive_handle_failed (client, connection);
                }
            }

            else if (rc == 0) {
                // man recv -> steam socket perfomed an orderly shutdown
                // but in dgram it might mean something?
                #ifdef CERVER_DEBUG
                char *s = c_string_create ("client_receive () - rc == 0 - sock fd: %d",
                    connection->socket->sock_fd);
                if (s) {
                    cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, s);
                    free (s);
                }
                // perror ("Error");
                #endif

                switch (errno) {
                    case EAGAIN: 
                        printf ("Is the connection still opened?\n"); 
                        client_receive_handle_failed (client, connection);
                        break;
                    case EBADF:
                    case ENOTSOCK: {
                        #ifdef CERVER_DEBUG
                        perror ("Error ");
                        #endif
                        client_receive_handle_failed (client, connection);
                    }
                }
            }

            else {
                // cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, 
                //     c_string_create ("Connection %s rc: %ld",
                //     connection->name->str, rc));
                // client->stats->n_receives_done += 1;
                client->stats->total_bytes_received += rc;

                // handle the recived packet buffer -> split them in packets of the correct size
                client_receive_handle_buffer (client, connection, 
                    packet_buffer, rc);
            }

            free (packet_buffer);
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                "Failed to allocate a new packet buffer!");
            #endif
        }
    }

}

#pragma endregion