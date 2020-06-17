#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/socket.h"
#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/handler.h"
#include "cerver/client.h"
#include "cerver/packets.h"

#include "cerver/collections/htab.h"
#include "cerver/collections/dllist.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region stats

ConnectionStats *connection_stats_new (void) {

    ConnectionStats *stats = (ConnectionStats *) malloc (sizeof (ConnectionStats));
    if (stats) {
        memset (stats, 0, sizeof (ConnectionStats));
        stats->received_packets = packets_per_type_new ();
        stats->sent_packets = packets_per_type_new ();
    } 

    return stats;

}

static inline void connection_stats_delete (ConnectionStats *stats) { 
    
    if (stats) {
        packets_per_type_delete (stats->received_packets);
        packets_per_type_delete (stats->sent_packets);

        free (stats); 
    } 
    
}

void connection_stats_print (Connection *connection) {

    if (connection) {
        if (connection->stats) {
            printf ("\nConnection's stats:\n");
            printf ("Threshold time:            %ld\n", connection->stats->threshold_time);

            printf ("N receives done:           %ld\n", connection->stats->n_receives_done);

            printf ("Total bytes received:      %ld\n", connection->stats->total_bytes_received);
            printf ("Total bytes sent:          %ld\n", connection->stats->total_bytes_sent);

            printf ("N packets received:        %ld\n", connection->stats->n_packets_received);
            printf ("N packets sent:            %ld\n", connection->stats->n_packets_sent);

            printf ("\nReceived packets:\n");
            packets_per_type_print (connection->stats->received_packets);

            printf ("\nSent packets:\n");
            packets_per_type_print (connection->stats->sent_packets);
        }

        else {
            cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                "Connection does not have a reference to a connection stats!");
        }
    }

    else {
        cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
            "Can't get stats of a NULL connection!");
    }

}

#pragma endregion

#pragma region main

Connection *connection_new (void) {

    Connection *connection = (Connection *) malloc (sizeof (Connection));
    if (connection) {
        memset (connection, 0, sizeof (Connection));

        // connection->sock_fd = -1;
        connection->socket = NULL;
        connection->use_ipv6 = false;
        connection->protocol = DEFAULT_CONNECTION_PROTOCOL;

        connection->ip = NULL;

        connection->max_sleep = DEFAULT_CONNECTION_MAX_SLEEP;
        connection->connected_to_cerver = false;

        connection->active = false;

        connection->auth_tries = DEFAULT_AUTH_TRIES;

        connection->receive_packet_buffer_size = RECEIVE_PACKET_BUFFER_SIZE;
        connection->cerver_report = NULL;
        connection->sock_receive = NULL;

        connection->full_packet = false;

        connection->received_data = NULL;
        connection->received_data_delete = NULL;

        connection->update_sleep = DEFAULT_CONNECTION_UPDATE_SLEEP;

        connection->receive_packets = true;
        connection->custom_receive = NULL;
        connection->custom_receive_args = NULL;
        
        connection->stats = NULL;
    }

    return connection;

}

void connection_delete (void *ptr) {

    if (ptr) {
        Connection *connection = (Connection *) ptr;

        socket_delete (connection->socket);

        if (connection->active) connection_end (connection);

        estring_delete (connection->ip);

        cerver_report_delete (connection->cerver_report);
        sock_receive_delete (connection->sock_receive);

        if (connection->received_data && connection->received_data_delete)
            connection->received_data_delete (connection->received_data);

        connection_stats_delete (connection->stats);

        free (connection);
    }

}

Connection *connection_create_empty (void) {

    Connection *connection = connection_new ();
    if (connection) {
        connection->socket = (Socket *) socket_create_empty ();
        connection->sock_receive = sock_receive_new ();
        connection->stats = connection_stats_new ();
    }

    return connection;

}

Connection *connection_create (const i32 sock_fd, const struct sockaddr_storage address,
    Protocol protocol) {

    Connection *connection = connection_create_empty ();
    if (connection) {
        connection->socket->sock_fd = sock_fd;
        memcpy (&connection->address, &address, sizeof (struct sockaddr_storage));
        connection->protocol = protocol;

        connection_get_values (connection);
    }

    return connection;

}

// compare two connections by their socket fds
int connection_comparator (const void *a, const void *b) {

    if (a && b) {
        Connection *con_a = (Connection *) a;
        Connection *con_b = (Connection *) b;

        if (con_a->socket->sock_fd < con_b->socket->sock_fd) return -1;
        else if (con_a->socket->sock_fd == con_b->socket->sock_fd) return 0;
        else return 1; 
    }

    return 0;

}

// get from where the client is connecting
void connection_get_values (Connection *connection) {

    if (connection) {
        connection->ip = estring_new (sock_ip_to_string ((const struct sockaddr *) &connection->address));
        connection->port = sock_ip_port ((const struct sockaddr *) &connection->address);
    }

}

// sets the connection's newtwork values
void connection_set_values (Connection *connection,
    const char *ip_address, u16 port, Protocol protocol, bool use_ipv6) {

    if (connection) {
        if (connection->ip) estring_delete (connection->ip);
        connection->ip = ip_address ? estring_new (ip_address) : NULL;
        connection->port = port;
        connection->protocol = protocol;
        connection->use_ipv6 = use_ipv6;

        connection->active = false;
    }

}

// sets the connection max sleep (wait time) to try to connect to the cerver
void connection_set_max_sleep (Connection *connection, u32 max_sleep) {

    if (connection) connection->max_sleep = max_sleep;

}

// sets if the connection will receive packets or not (default true)
// if true, a new thread is created that handled incoming packets
void connection_set_receive (Connection *connection, bool receive) {

    if (connection) connection->receive_packets = receive;

}

// read packets into a buffer of this size in client_receive ()
// by default the value RECEIVE_PACKET_BUFFER_SIZE is used
void connection_set_receive_buffer_size (Connection *connection, u32 size) {

    if (connection) connection->receive_packet_buffer_size = size;

}

// sets the connection received data
// 01/01/2020 - a place to safely store the request response, like when using client_connection_request_to_cerver ()
void connection_set_received_data (Connection *connection, void *data, size_t data_size, Action data_delete) {

    if (connection) {
        connection->received_data = data;
        connection->received_data_size = data_size;
        connection->received_data_delete = data_delete;
    }

}

// 17/06/2020
// sets the waiting time (sleep) between each call to recv () in connection_update () thread
void connection_set_update_sleep (Connection *connection, u32 sleep) {

    if (connection) connection->update_sleep = sleep;

}

// sets a custom receive method to handle incomming packets in the connection
// a reference to the client and connection will be passed to the action as ClientConnection structure
// the method must return 0 on success & 1 on error
void connection_set_custom_receive (Connection *connection, delegate custom_receive, void *args) {

    if (connection) {
        connection->custom_receive = custom_receive;
        connection->custom_receive_args = args;
        if (connection->custom_receive) connection->receive_packets = true;
    }

}

// sets up the new connection values
u8 connection_init (Connection *connection) {

    u8 retval = 1;

    if (connection) {
        if (!connection->active) {
            // init the new connection socket
            switch (connection->protocol) {
                case IPPROTO_TCP: 
                    connection->socket->sock_fd = socket ((connection->use_ipv6 == 1 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
                    break;
                case IPPROTO_UDP:
                    connection->socket->sock_fd = socket ((connection->use_ipv6 == 1 ? AF_INET6 : AF_INET), SOCK_DGRAM, 0);
                    break;

                default: cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, "Unkonw protocol type!"); return 1;
            }

            if (connection->socket->sock_fd > 0) {
                // if (connection->async) {
                //     if (sock_set_blocking (connection->sock_fd, connection->blocking)) {
                //         connection->blocking = false;

                        // get the address ready
                        if (connection->use_ipv6) {
                            struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &connection->address;
                            addr->sin6_family = AF_INET6;
                            addr->sin6_addr = in6addr_any;
                            addr->sin6_port = htons (connection->port);
                        } 

                        else {
                            struct sockaddr_in *addr = (struct sockaddr_in *) &connection->address;
                            addr->sin_family = AF_INET;
                            addr->sin_addr.s_addr = inet_addr (connection->ip->str);
                            addr->sin_port = htons (connection->port);
                        }

                        retval = 0;     // connection setup was successfull
                    // }

                    // else {
                    //     #ifdef CLIENT_DEBUG
                    //     cengine_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                    //         "Failed to set the socket to non blocking mode!");
                    //     #endif
                    //     close (connection->sock_fd);
                //     }
                // }
            }

            else {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                    "Failed to create new socket!");
            }
        }

        else {
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE,
                "Failed to init connection -- it is already active!");
        }
        
    }

    return retval;

}

// try to connect a client to an address (server) with exponential backoff
static u8 connection_try (Connection *connection, const struct sockaddr_storage address) {

    u32 numsec;
    for (numsec = 2; numsec <= connection->max_sleep; numsec <<= 1) {
        if (!connect (connection->socket->sock_fd, 
            (const struct sockaddr *) &address, 
            sizeof (struct sockaddr))) 
            return 0;

        if (numsec <= connection->max_sleep / 2) sleep (numsec);
    } 

    return 1;

}

// starts a connection -> connects to the specified ip and port
// returns 0 on success, 1 on error
int connection_connect (Connection *connection) {

    return (connection ? connection_try (connection, connection->address) : 1);

}

// ends a connection
void connection_end (Connection *connection) {

    if (connection) {
        if (connection->active) {
            close (connection->socket->sock_fd);
            connection->socket->sock_fd = -1;
            connection->active = false;
        }
    }

}

// gets the connection from the on hold connections map in cerver
Connection *connection_get_by_sock_fd_from_on_hold (Cerver *cerver, i32 sock_fd) {

    Connection *connection = NULL;

    if (cerver) {
        const i32 *key = &sock_fd;
        void *connection_data = htab_get (
            cerver->on_hold_connection_sock_fd_map,
            key, sizeof (i32)
        );
        
        if (connection_data) connection = (Connection *) connection_data;
    }

    return connection;

}

// gets the connection from the client by its sock fd
Connection *connection_get_by_sock_fd_from_client (Client *client, i32 sock_fd) {

    if (client) {
        Connection *query = connection_new ();
        if (query) {
            // query->sock_fd = sock_fd;
            query->socket = socket_create (sock_fd);
            void *connection_data = dlist_search (client->connections, query, NULL);
            connection_delete (query);
            return (connection_data ? (Connection *) connection_data : NULL);
        }
    }

    return NULL;

}

// checks if the connection belongs to the client
bool connection_check_owner (Client *client, Connection *connection) {

    if (client && connection) {
        for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
            if (connection->socket->sock_fd == ((Connection *) le->data)->socket->sock_fd) return true;
        }
    }

    return false;

}

// necessary steps to completely remove a connection from the cerver and from the client
// returns 0 on success, 1 on error
u8 connection_remove (Cerver *cerver, Client *client, Connection *connection, ListElement *le) {

    u8 retval = 1;

    if (cerver && client && connection) {
        // pthread_mutex_lock (connection->socket->mutex);

        connection_unregister_from_cerver (cerver, client, connection);

        // unmap the client connection from the cerver poll
        cerver_poll_unregister_connection (cerver, client, connection);

        // close the socket
        connection_end (connection);

        // remove the connection from the client
        dlist_remove_element (client->connections, le);

        // 29/05/2020
        // move the socket to the cerver's socket pool to avoid destroying it
        // to handle if any other thread is waiting to access the socket's mutex
        cerver_sockets_pool_push (cerver, connection->socket);
        connection->socket = NULL;

        connection_delete (connection);

        retval = 0;

        // pthread_mutex_unlock (connection->socket->mutex);
    }

    return retval;

}

// registers a new connection to a client without adding it to the cerver poll
// returns 0 on success, 1 on error
u8 connection_register_to_client (Client *client, Connection *connection) {

    u8 retval = 1;

    if (client && connection) {
        if (!dlist_insert_after (client->connections, dlist_end (client->connections), connection)) {
            #ifdef CERVER_DEBUG
            if (client->session_id) {
                char *s = c_string_create ("Registered a new connection to client with session id: %s",
                    client->session_id->str);
                if (s) {
                    cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, s);
                    free (s);
                }
            }

            else {
                char *s = c_string_create ("Registered a new connection to client (id): %ld",
                    client->id);
                if (s) {
                    cerver_log_msg (stdout, LOG_SUCCESS, LOG_CLIENT, s);
                    free (s);
                }
            }
            #endif

            retval = 0;
        }

        // for wahtever reason failed to register the connection to the client
        // we close the connection, as it can not be a free connection
        else {
            connection_end (connection);
            connection_delete (connection);
        }
    }

    return retval;

}

// unregisters a connection from a client, if the connection is active, it is stopped and removed 
// from the cerver poll as there can't be a free connection withput client
// returns 0 on success, 1 on error
u8 connection_unregister_from_client (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (client && connection) {
        if (client->connections->size > 1) {
            Connection *con = NULL;
            for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
                con = (Connection *) le->data;
                if (connection->socket->sock_fd == con->socket->sock_fd) {
                    connection_remove (
                        cerver,
                        client,
                        connection,
                        le
                    );

                    retval = 0;
                    
                    break;
                }
            }
        }

        else {
            connection_remove (
                cerver,
                client,
                connection,
                NULL
            );

            retval = 0;
        }
    }

    return retval;

}

// registers the client connection to the cerver's strcutures (like maps)
// returns 0 on success, 1 on error
u8 connection_register_to_cerver (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        const void *key = &connection->socket->sock_fd;

        // map the socket fd with the client
        htab_insert (cerver->client_sock_fd_map, key, sizeof (i32), 
            client, sizeof (Client));

        retval = 0;
    }

    return retval;

}

// unregister the client connection from the cerver's structures (like maps)
// returns 0 on success, 1 on error
u8 connection_unregister_from_cerver (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        // remove the sock fd from each map
        const void *key = &connection->socket->sock_fd;
        if (htab_remove (cerver->client_sock_fd_map, key, sizeof (i32))) {
            // char *s = c_string_create ("Removed sock fd %d from cerver's %s client sock map.", 
            //     connection->socket->sock_fd, cerver->info->name->str);
            // if (s) {
            //     cerver_log_success (s);
            //     free (s);
            // }

            retval = 0;
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Failed to remove sock fd %d from cerver's %s client sock map.", 
                connection->socket->sock_fd, cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                free (s);
            }
            #endif
        }
    }

    return retval;

}

// wrapper function for easy access
// registers a client connection to the cerver poll structures
u8 connection_register_to_cerver_poll (Cerver *cerver, Client *client, Connection *connection) {

    if (!cerver_poll_register_connection (cerver, client, connection)) {
        connection->active = true;
        return 0;
    }

    return 1;

}

// wrapper function for easy access
// unregisters a client connection from the cerver poll structures
u8 connection_unregister_from_cerver_poll (Cerver *cerver, Client *client, Connection *connection) {

    if (!cerver_poll_unregister_connection (cerver, client, connection)) {
        connection->active = false;
        return 0;
    }

    return 1;

}

static ConnectionCustomReceiveData *connection_custom_receive_data_new (Client *client, Connection *connection, void *args) {

    ConnectionCustomReceiveData *custom_data = (ConnectionCustomReceiveData *) malloc (sizeof (ConnectionCustomReceiveData));
    if (custom_data) {
        custom_data->client = client;
        custom_data->connection = connection;
        custom_data->args = args;
    }

    return custom_data;

}

static inline void connection_custom_receive_data_delete (void *custom_data_ptr) {

    if (custom_data_ptr) free (custom_data_ptr);

}

// starts listening and receiving data in the connection sock
void connection_update (void *ptr) {

    if (ptr) {
        ClientConnection *cc = (ClientConnection *) ptr;
        // thread_set_name (c_string_create ("connection-%s", cc->connection->name->str));

        ConnectionCustomReceiveData *custom_data = connection_custom_receive_data_new (cc->client, cc->connection, 
            cc->connection->custom_receive_args);

        if (!cc->connection->sock_receive) cc->connection->sock_receive = sock_receive_new ();

        // 17/06/2020 - non blocking recv () to correctly use client mutex
        // when destroying client
        if (sock_set_blocking (cc->connection->socket->sock_fd, true)) {
            // if (cc->connection->receive_packets) {
                while (cc->client->running && cc->connection->active) {
                    pthread_mutex_lock (cc->client->lock);

                    if (cc->connection->custom_receive) {
                        // if a custom receive method is set, use that one directly
                        if (cc->connection->custom_receive (custom_data)) {
                            // break;      // an error has ocurred
                        }
                    } 

                    else {
                        // use the default receive method that expects cerver type packages
                        if (client_receive (cc->client, cc->connection)) {
                            // break;      // an error has ocurred
                        }
                    }

                    pthread_mutex_unlock (cc->client->lock);

                    usleep (cc->connection->update_sleep);
                }
            // }    
        }

        connection_custom_receive_data_delete (custom_data);
        client_connection_aux_delete (cc);
    }

}

#pragma endregion