#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/client.h"

#include "cerver/collections/avl.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// get from where the client is connecting
char *client_getConnectionValues (i32 fd, const struct sockaddr_storage address) {

    char *connectionValues = NULL;

    char *ipstr = sock_ip_to_string ((const struct sockaddr *) &address);
    u16 port = sock_ip_port ((const struct sockaddr *) &address);

    if (ipstr && (port > 0)) 
        connectionValues = string_create ("%s-%i", ipstr, port);

    return connectionValues;

}

void client_set_sessionID (Client *client, const char *sessionID) {

    if (client && sessionID) {
        if (client->sessionID) free (client->sessionID);
        client->sessionID = string_create ("%s", sessionID);
    }

}

Client *newClient (Server *server, i32 clientSock, struct sockaddr_storage address,
    char *connection_values) {

    Client *client = NULL;

    client = (Client *) malloc (sizeof (Client));
    client->clientID = NULL;
    client->sessionID = NULL;
    // client->active_connections = NULL;

    // if (server->clientsPool) client = pool_pop (server->clientsPool);

    // if (!client) {
    //     client = (Client *) malloc (sizeof (Client));
    //     client->sessionID = NULL;
    //     client->active_connections = NULL;
    // } 

    memcpy (&client->address, &address, sizeof (struct sockaddr_storage));

    // printf ("address ip: %s\n", sock_ip_to_string ((const struct sockaddr *) &address));
    // printf ("client address ip: %s\n", sock_ip_to_string ((const struct sockaddr *) & client->address));

    if (connection_values) {
        if (client->clientID) free (client->clientID);

        client->clientID = string_create ("%s", connection_values);
        free (connection_values);
    }

    client->sessionID = NULL;

    if (server->authRequired) {
        client->authTries = server->auth.maxAuthTries;
        client->dropClient = false;
    } 

    client->n_active_cons = 0;
    if (client->active_connections) 
        for (u8 i = 0; i < DEFAULT_CLIENT_MAX_CONNECTS; i++)
            client->active_connections[i] = -1;

    else {
        client->active_connections = (i32 *) calloc (DEFAULT_CLIENT_MAX_CONNECTS, sizeof (i32));
        for (u8 i = 0; i < DEFAULT_CLIENT_MAX_CONNECTS; i++)
            client->active_connections[i] = -1;
    }

    client->curr_max_cons = DEFAULT_CLIENT_MAX_CONNECTS;
    
    // add the fd to the active connections
    if (client->active_connections) {
        client->active_connections[client->n_active_cons] = clientSock;
        printf ("client->active_connections[%i] = %i\n", client->n_active_cons, clientSock);
        client->n_active_cons++;
    } 

    return client;

}

// FIXME: what happens with the idx in the poll structure?
// deletes a client forever
void destroyClient (void *data) {

    if (data) {
        Client *client = (Client *) data;

        if (client->active_connections) {
            // close all the client's active connections
            // for (u8 i = 0; i < client->n_active_cons; i++)
            //     close (client->active_connections[i]);

            free (client->active_connections);
        }

        if (client->clientID) free (client->clientID);
        if (client->sessionID) free (client->sessionID);

        free (client);
    }

}

// destroy client without closing his connections
void client_delete_data (Client *client) {

    if (client) {
        if (client->clientID) free (client->clientID);
        if (client->sessionID) free (client->sessionID);
        if (client->active_connections) free (client->active_connections);
    }

}

// compare clients based on their client ids
int client_comparator_clientID (const void *a, const void *b) {

    if (a && b) {
        Client *client_a = (Client *) a;
        Client *client_b = (Client *) b;

        return strcmp (client_a->clientID, client_b->clientID);
    }

}

// compare clients based on their session ids
int client_comparator_sessionID (const void *a, const void *b) {

    if (a && b) {
        Client *client_a = (Client *) a;
        Client *client_b = (Client *) b;

        return strcmp (client_a->sessionID, client_b->sessionID);
    }

}

// recursively get the client associated with the socket
Client *getClientBySocket (AVLNode *node, i32 socket_fd) {

    if (node) {
        Client *client = NULL;

        client = getClientBySocket (node->right, socket_fd);

        if (!client) {
            if (node->id) {
                client = (Client *) node->id;
                
                // search the socket fd in the clients active connections
                for (int i = 0; i < client->n_active_cons; i++)
                    if (socket_fd == client->active_connections[i])
                        return client;

            }
        }

        if (!client) client = getClientBySocket (node->left, socket_fd);

        return client;
    }

    return NULL;

}

Client *getClientBySession (AVLTree *clients, char *sessionID) {

    if (clients && sessionID) {
        Client temp;
        temp.sessionID = string_create ("%s", sessionID);
        
        void *data = avl_get_node_data (clients, &temp);
        if (data) return (Client *) data;
        else 
            log_msg (stderr, WARNING, SERVER, 
                string_create ("Couldn't find a client associated with the session ID: %s.", 
                sessionID));
    }

    return NULL;

}

// the client made a new connection to the server
u8 client_registerNewConnection (Client *client, i32 socket_fd) {

    if (client) {
        if (client->active_connections) {
            u8 new_active_cons = client->n_active_cons + 1;

            if (new_active_cons <= client->curr_max_cons) {
                // search for a free spot
                for (int i = 0; i < client->curr_max_cons; i++) {
                    if (client->active_connections[i] == -1) {
                        client->active_connections[i] = socket_fd;
                        printf ("client->active_connections[%i] = %i\n", client->n_active_cons, socket_fd);
                        client->n_active_cons++;
                        printf ("client->n_active_cons: %i\n", client->n_active_cons);
                        return 0;
                    }
                }
            }

            // we need to add more space
            else {
                client->active_connections = (i32 *) realloc (client->active_connections, 
                    client->n_active_cons * sizeof (i32));

                // add the connection at the end
                client->active_connections[new_active_cons] = socket_fd;
                client->n_active_cons++;

                client->curr_max_cons++;
                
                return 0;
            }
        }
    }

    return 1;

}

// removes an active connection from a client
u8 client_unregisterConnection (Client *client, i32 socket_fd) {

    if (client) {
        if (client->active_connections) {
            if (client->active_connections > 0) {
                // search the socket fd
                for (u8 i = 0; i < client->n_active_cons; i++) {
                    if (client->active_connections[i] == socket_fd) {
                        client->active_connections[i] = -1;
                        client->n_active_cons--;
                        return 0;
                    }
                }
            }
        }
    }

    return 1;

}

// FIXME: what is the max number of clients that a server can handle?
// registers a NEW client to the server
void client_registerToServer (Server *server, Client *client, i32 newfd) {

    if (server && client) {
        // add the new sock fd to the server main poll
        i32 idx = getFreePollSpot (server);
        if (idx > 0) {
            server->fds[idx].fd = newfd;
            server->fds[idx].events = POLLIN;
            server->nfds++;

            printf ("client_registerToServer () - idx: %i\n", idx);

            // insert the new client into the server's clients
            avl_insert_node (server->clients, client);

            server->connectedClients++;

            #ifdef CERVER_STATS
                log_msg (stdout, SERVER, NO_TYPE, 
                string_create ("New client registered to server. Connected clients: %i.", 
                server->connectedClients));
            #endif
        }

        // TODO: how to better handle this error?
        else {
            log_msg (stderr, ERROR, SERVER, 
                "Failed to get a free main poll idx. Is the server full?");
            // just drop the client connection
            close (newfd);

            // if it was the only client connection, drop it
            if (client->n_active_cons <= 1) client_closeConnection (server, client);
        } 
    }

}

// removes a client form a server's main poll structures
Client *client_unregisterFromServer (Server *server, Client *client) {

    if (server && client) {
        Client *c = avl_remove_node (server->clients, client);
        if (c) {
            if (client->active_connections) {
                for (u8 i = 0; i < client->n_active_cons; i++) {
                    for (u8 j = 0; j < poll_n_fds; j++) {
                        if (server->fds[j].fd == client->active_connections[i]) {
                            server->fds[j].fd = -1;
                            server->fds[j].events = -1;
                        }
                    }
                }
            }

            #ifdef CERVER_DEBUG
                log_msg (stdout, DEBUG_MSG, SERVER, "Unregistered a client from the sever");
            #endif

            server->connectedClients--;

            return c;
        }

        else log_msg (stdout, WARNING, CLIENT, "The client wasn't registered in the server.");
    }

    return NULL;

}

// disconnect a client from the server and take it out from the server's clients and 
void client_closeConnection (Server *server, Client *client) {

    if (server && client) {
        if (client->active_connections) {
            // close all the client's active connections
            for (u8 i = 0; i < client->n_active_cons; i++)
                close (client->active_connections[i]);

            free (client->active_connections);
            client->active_connections = NULL;
        }

        // remove it from the server structures
        client_unregisterFromServer (server, client);

        #ifdef CERVER_DEBUG
            log_msg (stdout, DEBUG_MSG, CLIENT, 
                string_create ("Disconnected a client from the server.\
                \nConnected clients remainning: %i.", server->connectedClients));
        #endif
    }

}

// disconnect the client from the server by a socket -- usefull for http servers
int client_disconnect_by_socket (Server *server, const int sock_fd) {
    
    int retval = 1;

    if (server) {
        Client *c = getClientBySocket (server->clients->root, sock_fd);
        if (c) {
            if (c->n_active_cons <= 1) 
                client_closeConnection (server, c);

            // FIXME: check for client_CloseConnection retval to be sure!!
            retval  = 0;
        }
            
        else {
            #ifdef CERVER_DEBUG
            log_msg (stderr, ERROR, CLIENT, 
                "Couldn't find an active client with the requested socket!");
            #endif
        }
    }

    return retval;

}

// TODO: used to check for client timeouts in any type of server
void client_checkTimeouts (Server *server) {}