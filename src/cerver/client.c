#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/auth.h"
#include "cerver/handler.h"
#include "cerver/client.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dllist.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

void client_connection_set_time (Connection *connection);
void client_connection_get_values (Connection *connection);

Connection *client_connection_new (void) {

    Connection *connection = (Connection *) malloc (sizeof (Connection));
    if (connection) {
        memset (connection, 0, sizeof (Connection));
        connection->ip = NULL;
        connection->active = false;
        connection->auth_tries = DEFAULT_AUTH_TRIES;
        connection->time_info = NULL;
    }

    return connection;

}

Connection *client_connection_create (const i32 sock_fd, const struct sockaddr_storage address) {

    Connection *connection = client_connection_new ();
    if (connection) {
        client_connection_set_time (connection);
        memcpy (&connection->address, &address, sizeof (struct sockaddr_storage));
        client_connection_get_values (connection);
    }

}

void client_connection_delete (void *ptr) {

    if (ptr) {
        Connection *connection = (Connection *) ptr;
        str_delete (connection->ip);
        if (connection->time_info) free (connection->time_info);
        free (connection);
    }

}

// compare two connections by their socket fds
int client_connection_comparator (const void *a, const void *b) {

    if (a && b) {
        Connection *con_a = (Connection *) a;
        Connection *con_b = (Connection *) b;

        if (con_a->sock_fd < con_b->sock_fd) return -1;
        else if (con_a->sock_fd == con_b->sock_fd) return 0;
        else return 1; 
    }

}

// sets the connection time stamp
void client_connection_set_time (Connection *connection) {

    if (connection) {
        time (&connection->raw_time);
        connection->time_info = localtime (&connection->raw_time);
    }

}

// returns connection time stamp in a string that should be deleted
const String *client_connection_get_time (const Connection *connection) {

    if (connection) return str_new (asctime (connection->time_info));
    else return NULL;

}

// get from where the client is connecting
void client_connection_get_values (Connection *connection) {

    if (connection) {
        connection->ip = str_new (sock_ip_to_string ((const struct sockaddr *) &connection->address));
        connection->port = sock_ip_port ((const struct sockaddr *) &connection->address);
    }

}

// ends a client connection
void client_connection_end (Connection *connection) {

    if (connection) {
        close (connection->sock_fd);
        connection->sock_fd = -1;
        connection->active = false;
    }

}

// registers a new connection to a client
// returns 0 on success, 1 on error
u8 client_connection_register (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (client && connection) {
        if (dlist_insert_after (client->connections, dlist_end (client->connections), connection)) {
            // regsiter the connection to the cerver poll
            if (!cerver_poll_register_connection (cerver, client, connection)) {
                #ifdef CERVER_DEBUG
                if (client->session_id) {
                    cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, 
                        c_string_create ("Registered a new connection to client with session id: %s",
                        client->session_id->str));
                }
                #endif

                connection->active = true;
                retval = 0;
            }
        }

        // failed to register connection
        else {
            client_connection_end (connection);
            client_connection_delete (connection);
        }
    }

    return retval;

}

// unregisters a connection from a client and stops and deletes it
// returns 0 on success, 1 on error
u8 client_connection_unregister (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (client && connection) {
        Connection *con = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            con = (Connection *) le->data;
            if (connection->sock_fd == con->sock_fd) {
                // unmap the client connection from the cerver poll
                cerver_poll_register_connection (cerver, client, connection);

                client_connection_end (connection);
                client_connection_delete (dlist_remove_element (client->connections, le));

                retval = 0;
                break;
            }
        }
    }

    return retval;

}

Client *client_new (void) {

    Client *client = (Client *) malloc (sizeof (Client));
    if (client) {
        memset (client, 0, sizeof (Client));

        client->client_id = NULL;
        client->session_id = NULL;

        client->connections = dlist_init (client_connection_delete, client_connection_comparator);

        client->drop_client = false;

        client->data = NULL;
        client->delete_data = NULL;
    }

    return client;

}

void client_delete (void *ptr) {

    if (ptr) {
        Client *client = (Client *) ptr;

        str_delete (client->client_id);
        str_delete (client->session_id);

        dlist_destroy (client->connections);

        if (client->data) {
            if (client->delete_data) client->delete_data (client->data);
            else free (client->data);
        }

        free (client);
    }

}

// TODO: create connected time stamp
// creates a new client and registers a new connection
Client *client_create (Cerver *cerver, const i32 sock_fd, const struct sockaddr_storage address) {

    Client *client = client_new ();
    if (client) {
        Connection *connection = client_connection_create (sock_fd, address);
        if (connection) client_connection_register (cerver, client, connection);
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
        if (client->session_id) str_delete (client->session_id);
        client->session_id = str_new (session_id);
    }

}

// sets client's data and a way to destroy it
void client_set_data (Client *client, void *data, Action delete_data) {

    if (client) {
        client->data = data;
        client->delete_data = delete_data;
    }

}

// compare clients based on their client ids
int client_comparator_client_id (const void *a, const void *b) {

    if (a && b) 
        return str_compare (((Client *) a)->client_id, ((Client *) b)->client_id);

}

// compare clients based on their session ids
int client_comparator_session_id (const void *a, const void *b) {

    if (a && b) 
        return str_compare (((Client *) a)->session_id, ((Client *) b)->session_id);

}

// closes all client connections
u8 client_disconnect (Client *client) {

    if (client) {
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = le->data;
            client_connection_end (connection);
        }

        return 0;
    }

    return 1;

}

// TODO: maybe we dont want to fully remove the client from the avl,
// maybe create another function???
// registers a client to the cerver --> add it to cerver's structures
u8 client_register_to_cerver (Cerver *cerver, Client *client) {

    u8 retval = 1;

    if (cerver && client) {
        bool failed = false;

        // register all the client connections to the cerver poll
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            failed = cerver_poll_register_connection (cerver, client, connection);
        }

        if (!failed) {
            // register the client to the cerver client's
            avl_insert_node (cerver->clients, client);

            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, 
                c_string_create ("Registered a new client to cerver %s.", cerver->name->str));
            #endif
            
            cerver->n_connected_clients++;
            #ifdef CERVER_STATS
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Connected clients to cerver %s: %i.", 
                cerver->name->str, cerver->n_connected_clients));
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
        // unregister all the client connections from the cerver
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            cerver_poll_unregister_connection (cerver, client, connection);
        }

        // remove the client from the cerver's clients
        void *client_data = avl_remove_node (cerver->clients, client);
        if (client_data) {
            retval = (Client *) client_data;

            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, 
                c_string_create ("Unregistered a new client from cerver %s.", cerver->name->str));
            #endif

            cerver->n_connected_clients--;
            #ifdef CERVER_STATS
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                c_string_create ("Connected clients to cerver %s: %i.", 
                cerver->name->str, cerver->n_connected_clients));
            #endif
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER,
                c_string_create ("Received NULL ptr when attempting to remove a client from cerver's %s client tree.", 
                cerver->name->str));
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
        size_t client_size = sizeof (Client);
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