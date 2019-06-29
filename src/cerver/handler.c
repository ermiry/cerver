#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/packets.h"
#include "cerver/handler.h"
#include "cerver/auth.h"
#include "cerver/game/game.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

CerverReceive *cerver_receive_new (Cerver *cerver, i32 sock_fd, bool on_hold) {

    CerverReceive *cr = (CerverReceive *) malloc (sizeof (CerverReceive));
    if (cr) {
        cr->cerver = cerver;
        cr->sock_fd = sock_fd;
        cr->on_hold = on_hold;
    }

    return cr;

}

void cerver_receive_delete (void *ptr) { if (ptr) free (ptr); }

// sends back a test packet to the client!
void cerver_test_packet_handler (Packet *packet) {

    if (packet) {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_PACKET, 
            c_string_create ("Got a test packet in cerver %s.", packet->cerver->name->str));
        #endif

        Packet *test_packet = packet_new ();
        if (test_packet) {
            packet_set_network_values (test_packet, packet->sock_fd, packet->cerver->protocol);
            test_packet->packet_type = TEST_PACKET;
            if (packet_send (test_packet, 0)) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                    c_string_create ("Failed to send error packet from cerver %s.", 
                    packet->cerver->name->str));
            }

            packet_delete (test_packet);
        }
    }

}

// handle packet based on type
static void cerver_packet_handler (void *ptr) {

    if (ptr) {
        Packet *packet = (Packet *) ptr;
        if (!packet_check (packet)) {
            switch (packet->header->packet_type) {
                // handles an error from the client
                case ERROR_PACKET: /* TODO: */ break;

                // handles authentication packets
                case AUTH_PACKET: /* TODO: */ break;

                // handles a request made from the server
                case REQUEST_PACKET: break;

                // handle a game packet sent from the server
                case GAME_PACKET: game_packet_handler (packet); break;

                // user set handler to handle app specific packets
                case APP_PACKET:
                    if (packet->cerver->app_packet_handler)
                        packet->cerver->app_packet_handler (packet);
                    break;

                // user set handler to handle app specific errors
                case APP_ERROR_PACKET: 
                    if (packet->cerver->app_error_packet_handler)
                        packet->cerver->app_error_packet_handler (packet);
                    break;

                // custom packet hanlder
                case CUSTOM_PACKET: 
                    if (packet->cerver->custom_packet_handler)
                        packet->cerver->custom_packet_handler (packet);
                    break;

                // acknowledge the client we have received his test packet
                case TEST_PACKET: cerver_test_packet_handler (packet); break;

                default:
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, 
                        c_string_create ("Got a packet of unknown type in cerver %s.", 
                        packet->cerver->name->str));
                    #endif
                    break;
            }
        }

        packet_delete (packet);
    }

}

static void cerver_handle_receive_buffer (Cerver *cerver, i32 sock_fd, 
    char *buffer, size_t buffer_size, bool on_hold) {

    if (buffer && (buffer_size > 0)) {
        u32 buffer_idx = 0;
        char *end = buffer;

        PacketHeader *header = NULL;
        u32 packet_size;
        char *packet_data = NULL;

        while (buffer_idx < buffer_size) {
            header = (PacketHeader *) end;

            // check the packet size
            packet_size = header->packet_size;
            if (packet_size > 0) {
                // copy the content of the packet from the buffer
                packet_data = (char *) calloc (packet_size, sizeof (char));
                for (u32 i = 0; i < packet_size; i++, buffer_idx++) 
                    packet_data[i] = buffer[buffer_idx];

                Packet *packet = packet_new ();
                if (packet) {
                    packet->cerver = cerver;
                    packet->sock_fd = sock_fd;
                    packet->header = header;
                    packet->packet_size = packet_size;
                    packet->packet = packet_data;

                    if (on_hold) on_hold_packet_handler (packet);
                    else {
                        Client *client = client_get_by_sock_fd (cerver, sock_fd);
                         if (client) cerver_packet_handler (packet);

                        else {
                            #ifdef CERVER_DEBUG
                            cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                                c_string_create ("Failed to get client associated with sock: %i.", sock_fd));
                            #endif
                            // no client - discard the data!
                            packet_delete (packet);
                        }
                    }
                }

                else {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                        c_string_create ("Failed to create a new packet in cerver_receive () in cerver %s.", 
                        cerver->name->str));
                    #endif
                    // failed to create a new packet -- discard the data
                    if (packet_data) free (packet_data);
                }

                end += packet_size;
            }

            else break;
        }
    }

}

// FIXME: correctly end client connection on error
// TODO: add support for handling large files transmissions
// receive all incoming data from the socket
void cerver_receive (void *ptr) {

    if (ptr) {
        CerverReceive *cr = (CerverReceive *) ptr;

        ssize_t rc;
        char packet_buffer[MAX_UDP_PACKET_SIZE];
        memset (packet_buffer, 0, MAX_UDP_PACKET_SIZE);

        // do {
            rc = recv (cr->sock_fd, packet_buffer, sizeof (packet_buffer), 0);
            
            if (rc < 0) {
                if (errno != EWOULDBLOCK) {     // no more data to read 
                    #ifdef CERVER_DEBUG 
                    cerver_log_msg (stderr, LOG_CERVER, LOG_NO_TYPE, "cerver_recieve () - rc < 0");
                    perror ("Error ");
                    #endif
                }

                // /break;
            }

            else if (rc == 0) {
                // man recv -> steam socket perfomed an orderly shutdown
                // but in dgram it might mean something?
                #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, "cerver_recieve () - rc == 0");
                #endif
                // break;
            }
            
            else {
                char *buffer_data = (char *) calloc (rc, sizeof (char));
                if (buffer_data) {
                    memcpy (buffer_data, packet_buffer, rc);
                    cerver_handle_receive_buffer (cr->cerver, cr->sock_fd, buffer_data, rc, cr->on_hold);
                }
            }
            
        // } while (true);

        // when we are done...
        cerver_receive_delete (cr);
    }

}

static void cerver_register_new_connection (Cerver *cerver, 
    const i32 new_fd, const struct sockaddr_storage client_address) {

    Connection *connection = client_connection_create (new_fd, client_address);
    if (connection) {
        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT,
                c_string_create ("New connection from IP address: %s -- Port: %d", 
                connection->ip->str, connection->port));
        #endif

        bool failed = false;

        // if the cerver requires auth, we put the connection on hold
        if (cerver->auth_required) 
            on_hold_connection (cerver, connection);

        // if not, we create a new client and we register the connection
        else {
            Client *client = client_new ();
            if (client) {
                client_connection_register (cerver, client, connection);

                // if we need to generate session ids...
                if (cerver->use_sessions) {
                    char *session_id = (char *) cerver->session_id_generator (client);
                    if (session_id) {
                        #ifdef CERVER_DEBUG
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT,
                            c_string_create ("Generated client session id: %s", session_id));
                        #endif
                        client_set_session_id (client, session_id);

                        client_register_to_cerver ((Cerver *) cerver, client);

                        // trigger cerver on client connected action
                        if (cerver->on_client_connected) 
                            cerver->on_client_connected (cerver->on_client_connected_data);
                    }

                    else {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                            "Failed to generate client session id!");
                        // TODO: do we want to drop the client connection?
                        failed = true;
                    }
                }

                // just register the client to the cerver
                else client_register_to_cerver ((Cerver *) cerver, client);
            }
        }

        if (!failed) {
            // send cerver info packet
            if (cerver->type != WEB_CERVER) {
                packet_set_network_values (cerver->cerver_info_packet, new_fd, cerver->protocol);
                if (packet_send (cerver->cerver_info_packet, 0)) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                        c_string_create ("Failed to send cerver %s info packet!", cerver->name->str));
                }   
            }
        }

        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, 
                c_string_create ("New connection to cerver %s!", cerver->name->str));
        #endif
    }

    else {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_ERROR, LOG_CLIENT, "Failed to create a new connection!");
        #endif
    }

}

// accepst a new connection to the cerver
static void cerver_accept (void *ptr) {

    if (ptr) {
        Cerver *cerver = (Cerver *) ptr;

        // accept the new connection
        struct sockaddr_storage client_address;
        memset (&client_address, 0, sizeof (struct sockaddr_storage));
        socklen_t socklen = sizeof (struct sockaddr_storage);

        i32 new_fd = accept (cerver->sock, (struct sockaddr *) &client_address, &socklen);
        if (new_fd > 0) cerver_register_new_connection (cerver, new_fd, client_address);
        else {
            // if we get EWOULDBLOCK, we have accepted all connections
            if (errno != EWOULDBLOCK) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Accept failed!");
                perror ("Error");
            } 
        }
    }

}

// reallocs main cerver poll fds
// returns 0 on success, 1 on error
u8 cerver_realloc_main_poll_fds (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver->max_n_fds = cerver->max_n_fds * 2;
        cerver->fds = realloc (cerver->fds, cerver->max_n_fds * sizeof (struct pollfd));
        if (cerver->fds) retval = 0;
    }

    return retval;

}

// get a free index in the main cerver poll strcuture
i32 cerver_poll_get_free_idx (Cerver *cerver) {

    if (cerver) {
        for (u32 i = 0; i < cerver->max_n_fds; i++)
            if (cerver->fds[i].fd == -1) return i;

        return 0;
    }

    return -1;

}

// get the idx of the connection sock fd in the cerver poll fds
i32 cerver_poll_get_idx_by_sock_fd (Cerver *cerver, i32 sock_fd) {

    if (cerver) {
        for (u32 i = 0; i < cerver->max_n_fds; i++)
            if (cerver->fds[i].fd == sock_fd) return i;
    }

    return -1;

}

// we remove any fd that was set to -1 for what ever reason
static void cerver_poll_compress_clients (Cerver *cerver) {

    if (cerver) {
        cerver->compress_clients = false;

        for (u32 i = 0; i < cerver->max_n_fds; i++) {
            if (cerver->fds[i].fd == -1) {
                for (u32 j = i; j < cerver->max_n_fds - 1; j++) 
                    cerver->fds[j].fd = cerver->fds[j + 1].fd;
                    
            }
        }
    }  

}

// regsiters a client connection to the cerver's mains poll structure
// and maps the sock fd to the client
u8 cerver_poll_register_connection (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        i32 idx = cerver_poll_get_free_idx (cerver);
        if (idx > 0) {
            cerver->fds[idx].fd = connection->sock_fd;
            cerver->fds[idx].events = POLLIN;
            cerver->current_n_fds++;

            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, 
                c_string_create ("Added new sock fd to cerver %s main poll, idx: %i", 
                cerver->name->str, idx));
            #endif

            // map the socket fd with the client
            const void *key = &connection->sock_fd;
            htab_insert (cerver->client_sock_fd_map, key, sizeof (i32), client, sizeof (Client));

            retval = 0;
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Cerver %s main poll is full -- we need to realloc...", 
                cerver->name->str));
            #endif
            if (cerver_realloc_main_poll_fds (cerver)) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                    c_string_create ("Failed to realloc cerver %s main poll fds!", 
                    cerver->name->str));
            }
        }
    }

    return retval;

}

// unregsiters a client connection from the cerver's main poll structure
// and removes the map from the socket to the client
u8 cerver_poll_unregister_connection (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        // get the idx of the connection sock fd in the cerver poll fds
        i32 idx = cerver_poll_get_idx_by_sock_fd (cerver, connection->sock_fd);
        if (idx > 0) {
            cerver->fds[idx].fd = -1;
            cerver->fds[idx].events = -1;
            cerver->current_n_fds--;

            const void *key = &connection->sock_fd;
            retval = htab_remove (cerver->client_sock_fd_map, key, sizeof (i32));
            #ifdef CERVER_DEBUG
            if (retval) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Failed to remove from cerver's %s client sock map.", 
                    cerver->name->str));
            }
            #endif
        }
    }

    return retval;

}

// cerver poll loop to handle events in the registered socket's fds
u8 cerver_poll (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, 
            c_string_create ("Cerver %s main handler has started!", cerver->name->str));
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Waiting for connections...");
        #endif

        int poll_retval = 0;

        while (cerver->isRunning) {
            poll_retval = poll (cerver->fds, cerver->current_n_fds, cerver->poll_timeout);

            // poll failed
            if (poll_retval < 0) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Cerver %s main poll has failed!", cerver->name->str));
                perror ("Error");
                cerver->isRunning = false;
                break;
            }

            // if poll has timed out, just continue to the next loop... 
            if (poll_retval == 0) {
                // #ifdef CERVER_DEBUG
                // cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                //    c_string_create ("Cerver %s poll timeout", cerver->name->str));
                // #endif
                continue;
            }

            // one or more fd(s) are readable, need to determine which ones they are
            for (u32 i = 0; i < poll_n_fds; i++) {
                if (cerver->fds[i].revents == 0) continue;
                if (cerver->fds[i].revents != POLLIN) continue;

                // accept incoming connections that are queued
                if (cerver->fds[i].fd == cerver->sock) {
                    if (thpool_add_work (cerver->thpool, cerver_accept, cerver))  {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                            c_string_create ("Failed to add cerver_accept () to cerver's %s thpool!", 
                            cerver->name->str));
                    }
                }

                // not the cerver socket, so a connection fd must be readable
                else {
                    if (thpool_add_work (cerver->thpool, cerver_receive, 
                        cerver_receive_new (cerver, cerver->fds[i].fd, false))) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                            c_string_create ("Failed to add cerver_receive () to cerver's %s thpool!", 
                            cerver->name->str));
                    }
                } 
            }

            // if (cerver->compress_clients) compressClients (cerver);
        }

        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, 
                c_string_create ("Cerver %s main poll has stopped!", cerver->name->str));
        #endif

        retval = 0;
    }

    else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Can't listen for connections on a NULL cerver!");

    return retval;

}