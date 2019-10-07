#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

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

static u64 next_client_id = 0;

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
    return 0;

}

// closes all client connections
u8 client_disconnect (Client *client) {

    if (client) {
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            connection_end (connection);
        }

        return 0;
    }

    return 1;

}

// drops a client form the cerver
// unregisters the client from the cerver and the deletes him
void client_drop (Cerver *cerver, Client *client) {

    if (cerver && client) {
        client_unregister_from_cerver (cerver, client);
        client_delete (client);
    }

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
            cerver_log_msg (stderr, LOG_WARNING, LOG_CLIENT, 
                c_string_create ("client_remove_connection () - Client with id" 
                "%ld does not have a connection related to sock fd %d",
                client->id, connection->sock_fd));
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
        // remove the connection from the cerver and from the client
        Connection *connection = connection_get_by_sock_fd_from_client (client, sock_fd);
        if (connection) {
            connection_unregister_from_client (cerver, client, connection);

            // if the client does not have any active connection, drop it
            if (client->connections->size <= 0) client_drop (cerver, client);

            retval = 0;
        }

        else {
            // the connection may not belong to this client
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_CLIENT, 
                c_string_create ("client_remove_connection_by_sock_fd () - Client with id" 
                "%ld does not have a connection related to sock fd %d",
                client->id, sock_fd));
            #endif
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
            cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                c_string_create ("Failed to register all the connections for client %ld (id) to cerver %s",
                client->id, cerver->info->name->str));
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
            cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                c_string_create ("Failed to unregister all the connections for client %ld (id) from cerver %s",
                client->id, cerver->info->name->str));
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
            cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                c_string_create ("Failed to register all the connections for client %ld (id) to cerver %s poll",
                client->id, cerver->info->name->str));
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
            cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                c_string_create ("Failed to unregister all the connections for client %ld (id) from cerver %s poll",
                client->id, cerver->info->name->str));
            #endif

            client_drop (cerver, client);       // drop the client ---> no active connections
        }

        else retval = 0;        // at least one connection is active
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

            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, 
                c_string_create ("Registered a new client to cerver %s.", cerver->info->name->str));
            #endif
            
            cerver->stats->total_n_clients++;
            cerver->stats->current_n_connected_clients++;
            #ifdef CERVER_STATS
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Connected clients to cerver %s: %i.", 
                cerver->info->name->str, cerver->stats->current_n_connected_clients));
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
        // unregister the connections from the cerver
        client_unregister_connections_from_cerver (cerver, client);

        // unregister all the client connections from the cerver
        // client_unregister_connections_from_cerver (cerver, client);
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            connection_unregister_from_cerver_poll (cerver, client, connection);
        }

        // remove the client from the cerver's clients
        void *client_data = avl_remove_node (cerver->clients, client);
        if (client_data) {
            retval = (Client *) client_data;

            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, 
                c_string_create ("Unregistered a client from cerver %s.", cerver->info->name->str));
            #endif

            cerver->stats->current_n_connected_clients--;
            #ifdef CERVER_STATS
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Connected clients to cerver %s: %i.", 
                cerver->info->name->str, cerver->stats->current_n_connected_clients));
            #endif
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER,
                c_string_create ("Received NULL ptr when attempting to remove a client from cerver's %s client tree.", 
                cerver->info->name->str));
            #endif
        }
    }

    return retval;

}

// gets the client associated with a sock fd using the client-sock fd map
Client *client_get_by_sock_fd (Cerver *cerver, i32 sock_fd) {

    Client *client = NULL;

    if (cerver) {
        const i32 *key = &sock_fd;
        void *client_data = htab_get_data (cerver->client_sock_fd_map, 
            key, sizeof (i32));
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

            void *data = avl_get_node_data (cerver->clients, client_query);
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

static ClientConnection *client_connection_aux_new (Client *client, Connection *connection) {

    ClientConnection *cc = (ClientConnection *) malloc (sizeof (ClientConnection));
    if (cc) {
        cc->client = client;
        cc->connection = connection;
    }

    return cc;

}

void client_connection_aux_delete (ClientConnection *cc) { if (cc) free (cc); }

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
        connection = connection_new ();
        if (connection) {
            connection_set_values (connection, ip_address, port, protocol, use_ipv6);
            connection_init (connection);
            connection_register_to_client (client, connection);
        }
    }

    return connection;

}

// starts a client connection -- used to connect a client to another server
// returns only after a success or failed connection
// returns 0 on success, 1 on error
int client_connection_start (Client *client, Connection *connection) {

    int retval = 1;

    if (client && connection) {
        if (!connection_connect (connection)) {
            connection->active = true;
            thread_create_detachable ((void *(*)(void *)) connection_update, 
                client_connection_aux_new (client, connection), NULL);
            client_start (client);
            retval = 0;
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
void client_connection_start_async (Client *client, Connection *connection) {

    if (client && connection) {
        thread_create_detachable ((void *(*) (void *)) client_connection_start_wrapper, 
            client_connection_aux_new (client, connection), NULL);
    }

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
            dlist_get_element (client->connections, connection)));

        retval = 0;
    }

    return retval;

}

// stop any on going connection and process then, destroys the client
u8 client_teardown (Client *client) {

    u8 retval = 1;

    if (client) {
        // end any ongoing connection
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            client_connection_terminate (client, (Connection *) le->data);
            connection_delete (dlist_remove_element (client->connections, le));
        }

        client->running = false;

        client_delete (client);

        retval = 0;
    }

    return retval;

}