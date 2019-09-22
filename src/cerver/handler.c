#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/packets.h"
#include "cerver/handler.h"
#include "cerver/auth.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "cerver/collections/htab.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

#pragma region auxiliary structures

SockReceive *sock_receive_new (void) {

    SockReceive *sr = (SockReceive *) malloc (sizeof (SockReceive));
    if (sr) {
        sr->spare_packet = NULL;
        sr->missing_packet = 0;
    } 

    return sr;

}

void sock_receive_delete (void *sock_receive_ptr) {

    if (sock_receive_ptr) {
        packet_delete (((SockReceive *) sock_receive_ptr)->spare_packet);
        free (sock_receive_ptr);
    }

}

static SockReceive *sock_receive_get (Cerver *cerver, i32 sock_fd) {

    SockReceive *sock_receive = NULL;
    const i32 *key = &sock_fd;
    void *sock_buffer_data = htab_get_data (cerver->sock_buffer_map,
        key, sizeof (i32));

    if (sock_buffer_data) sock_receive = (SockReceive *) sock_buffer_data;

    return sock_receive;    

}

CerverReceive *cerver_receive_new (Cerver *cerver, i32 sock_fd, bool on_hold, Lobby *lobby) {

    CerverReceive *cr = (CerverReceive *) malloc (sizeof (CerverReceive));
    if (cr) {
        cr->cerver = cerver;
        cr->sock_fd = sock_fd;
        cr->on_hold = on_hold;
        cr->lobby = lobby;
    }

    return cr;

}

void cerver_receive_delete (void *ptr) { if (ptr) free (ptr); }

#pragma endregion

#pragma region handlers

// handles a request made from the client
static void cerver_request_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->data && (packet->data_size >= (sizeof (RequestData)))) {
            char *end = (char *) packet->data;
            RequestData *req_data = (RequestData *) end;

            switch (req_data->type) {
                // the client is going to close its current connection
                // but will remain in the cerver if it has another connection active
                // if not, it will be dropped
                case CLIENT_CLOSE_CONNECTION:
                    // check if the client is inside a lobby
                    if (packet->lobby) {
                        #ifdef CERVER_DEBUG
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_GAME, 
                            c_string_create ("Client %ld inside lobby %s wants to close the connection...",
                            packet->client->id, packet->lobby->id->str));
                        #endif

                        // remove the player from the lobby
                        Player *player = player_get_by_sock_fd_list (packet->lobby, packet->connection->sock_fd);
                        player_unregister_from_lobby (packet->lobby, player);
                    }

                    client_remove_connection_by_sock_fd (packet->cerver, 
                        packet->client, packet->connection->sock_fd); 
                    break;

                // the client is going to disconnect and will close all of its active connections
                // so drop it from the server
                case CLIENT_DISCONNET: {
                    // check if the client is inside a lobby
                    if (packet->lobby) {
                        #ifdef CERVER_DEBUG
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_GAME, 
                            c_string_create ("Client %ld inside lobby %s wants to close the connection...",
                            packet->client->id, packet->lobby->id->str));
                        #endif

                        // remove the player from the lobby
                        Player *player = player_get_by_sock_fd_list (packet->lobby, packet->connection->sock_fd);
                        player_unregister_from_lobby (packet->lobby, player);
                    }

                    client_drop (packet->cerver, packet->client);
                } break;

                default: 
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                        c_string_create ("Got an unknown request in cerver %s",
                        packet->cerver->info->name->str));
                    #endif
                    break;
            }
        }
    }

}

// sends back a test packet to the client!
void cerver_test_packet_handler (Packet *packet) {

    if (packet) {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_PACKET, 
            c_string_create ("Got a test packet in cerver %s.", packet->cerver->info->name->str));
        #endif

        Packet *test_packet = packet_new ();
        if (test_packet) {
            packet_set_network_values (test_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
            test_packet->packet_type = TEST_PACKET;
            if (packet_send (test_packet, 0, NULL, false)) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                    c_string_create ("Failed to send error packet from cerver %s.", 
                    packet->cerver->info->name->str));
            }

            packet_delete (test_packet);
        }
    }

}

// handle packet based on type
static void cerver_packet_handler (void *ptr) {

    if (ptr) {
        Packet *packet = (Packet *) ptr;
        packet->cerver->stats->client_n_packets_received += 1;
        packet->cerver->stats->total_n_packets_received += 1;
        if (packet->lobby) packet->lobby->stats->n_packets_received += 1;

        // if (!packet_check (packet)) {
            switch (packet->header->packet_type) {
                // handles an error from the client
                case ERROR_PACKET: 
                    packet->cerver->stats->received_packets->n_error_packets += 1;
                    packet->client->stats->received_packets->n_error_packets += 1;
                    packet->connection->stats->received_packets->n_error_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_error_packets += 1;
                    /* TODO: */ 
                    break;

                // handles authentication packets
                case AUTH_PACKET: 
                    packet->cerver->stats->received_packets->n_auth_packets += 1;
                    packet->client->stats->received_packets->n_auth_packets += 1;
                    packet->connection->stats->received_packets->n_auth_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_auth_packets += 1;
                    /* TODO: */ 
                    break;

                // handles a request made from the client
                case REQUEST_PACKET: 
                    packet->cerver->stats->received_packets->n_request_packets += 1;
                    packet->client->stats->received_packets->n_request_packets += 1;
                    packet->connection->stats->received_packets->n_request_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_request_packets += 1;
                    cerver_request_packet_handler (packet); 
                    break;

                // handles a game packet sent from the client
                case GAME_PACKET: 
                    packet->cerver->stats->received_packets->n_game_packets += 1;
                    packet->client->stats->received_packets->n_game_packets += 1;
                    packet->connection->stats->received_packets->n_game_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_game_packets += 1;
                    game_packet_handler (packet); 
                    break;

                // user set handler to handle app specific packets
                case APP_PACKET:
                    packet->cerver->stats->received_packets->n_app_packets += 1;
                    packet->client->stats->received_packets->n_app_packets += 1;
                    packet->connection->stats->received_packets->n_app_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_app_packets += 1;
                    if (packet->cerver->app_packet_handler)
                        packet->cerver->app_packet_handler (packet);
                    break;

                // user set handler to handle app specific errors
                case APP_ERROR_PACKET: 
                    packet->cerver->stats->received_packets->n_app_error_packets += 1;
                    packet->client->stats->received_packets->n_app_error_packets += 1;
                    packet->connection->stats->received_packets->n_app_error_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_app_error_packets += 1;
                    if (packet->cerver->app_error_packet_handler)
                        packet->cerver->app_error_packet_handler (packet);
                    break;

                // custom packet hanlder
                case CUSTOM_PACKET: 
                    packet->cerver->stats->received_packets->n_custom_packets += 1;
                    packet->client->stats->received_packets->n_custom_packets += 1;
                    packet->connection->stats->received_packets->n_custom_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_custom_packets += 1;
                    if (packet->cerver->custom_packet_handler)
                        packet->cerver->custom_packet_handler (packet);
                    break;

                // acknowledge the client we have received his test packet
                case TEST_PACKET: 
                    packet->cerver->stats->received_packets->n_test_packets += 1;
                    packet->client->stats->received_packets->n_test_packets += 1;
                    packet->connection->stats->received_packets->n_test_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_test_packets += 1;
                    cerver_test_packet_handler (packet); 
                    break;

                default:
                    packet->cerver->stats->received_packets->n_bad_packets += 1;
                    packet->client->stats->received_packets->n_bad_packets += 1;
                    packet->connection->stats->received_packets->n_bad_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_bad_packets += 1;
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, 
                        c_string_create ("Got a packet of unknown type in cerver %s.", 
                        packet->cerver->info->name->str));
                    #endif
                    break;
            }
        // }

        packet_delete (packet);
    }

}

static void cerver_packet_select_handler (Cerver *cerver, i32 sock_fd,
    Packet *packet, bool on_hold) {

    if (on_hold) {
        Connection *connection = connection_get_by_sock_fd_from_on_hold (cerver, sock_fd);
        if (connection) {
            packet->connection = connection;

            packet->connection->stats->n_packets_received += 1;
            packet->connection->stats->total_bytes_received += packet->packet_size;

            on_hold_packet_handler (packet);
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
                c_string_create ("Failed to get on hold connection associated with sock: %i",
                sock_fd));
            #endif
            // no connection - discard the data
            packet_delete (packet);
        }
    } 

    else {
        Client *client = client_get_by_sock_fd (cerver, sock_fd);
        if (client) {
            Connection *connection = connection_get_by_sock_fd_from_client (client, sock_fd);
            packet->client = client;
            packet->connection = connection;

            packet->client->stats->n_packets_received += 1;
            packet->client->stats->total_bytes_received += packet->packet_size;
            packet->connection->stats->n_packets_received += 1;
            packet->connection->stats->total_bytes_received += packet->packet_size;

            cerver_packet_handler (packet);
        } 

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

#pragma endregion

#pragma region receive

static SockReceive *cerver_receive_handle_spare_packet (Cerver *cerver, i32 sock_fd, bool on_hold,
    size_t buffer_size, char **end, size_t *buffer_pos) {

    // check if there is already a buffer asscoaited with the sock fd
    SockReceive *sock_receive = sock_receive_get (cerver, sock_fd);
    if (sock_receive) {
        if (sock_receive->spare_packet) {
            size_t copy_to_spare = 0;
            if (sock_receive->missing_packet < buffer_size) 
                copy_to_spare = sock_receive->missing_packet;

            else copy_to_spare = buffer_size;

            // append new data from buffer to the spare packet
            if (copy_to_spare > 0) {
                packet_append_data (sock_receive->spare_packet, *end, copy_to_spare);

                // check if we can handler the packet 
                size_t curr_packet_size = sock_receive->spare_packet->data_size + sizeof (PacketHeader);
                if (sock_receive->spare_packet->header->packet_size == curr_packet_size) {
                    cerver_packet_select_handler (cerver, sock_fd, sock_receive->spare_packet, on_hold);
                    sock_receive->spare_packet = NULL;
                    sock_receive->missing_packet = 0;
                }

                else sock_receive->missing_packet -= copy_to_spare;

                // offset for the buffer
                if (copy_to_spare < buffer_size) *end += copy_to_spare;
                *buffer_pos += copy_to_spare;
            }
        }
    }

    else {
        #ifdef CERVER_DEBUG
        cerver_log_msg (stderr, LOG_WARNING, LOG_CERVER,
            c_string_create ("Sock fd: %d does not have an associated receive buffer in cerver %s.",
            sock_fd, cerver->info->name->str));
        #endif
    }

    return sock_receive;

}

// TODO: test whit large files
static void cerver_receive_handle_buffer (Cerver *cerver, i32 sock_fd, 
    char *buffer, size_t buffer_size, bool on_hold, Lobby *lobby) {

    if (buffer && (buffer_size > 0)) {
        char *end = buffer;
        size_t buffer_pos = 0;

        SockReceive *sock_receive = cerver_receive_handle_spare_packet (cerver, sock_fd, on_hold,
            buffer_size, &end, &buffer_pos);

        if (buffer_pos >= buffer_size) return;

        PacketHeader *header = NULL;
        size_t packet_size = 0;
        char *packet_data = NULL;

        size_t remaining_buffer_size = 0;
        size_t packet_real_size = 0;
        size_t to_copy_size = 0;

        while (buffer_pos < buffer_size) {
            remaining_buffer_size = buffer_size - buffer_pos;
            if (remaining_buffer_size >= sizeof (PacketHeader)) {
                header = (PacketHeader *) end;

                // check the packet size
                packet_size = header->packet_size;
                if ((packet_size > 0) && (packet_size < 65536)) {
                    end += sizeof (PacketHeader);
                    buffer_pos += sizeof (PacketHeader);

                    Packet *packet = packet_new ();
                    if (packet) {
                        packet->cerver = cerver;
                        packet->lobby = lobby;
                        packet_header_copy (&packet->header, header);
                        packet->packet_size = packet->header->packet_size;

                        // check for packet size and only copy what is in the current buffer
                        packet_real_size = header->packet_size - sizeof (PacketHeader);
                        to_copy_size = 0;
                        if ((remaining_buffer_size - sizeof (PacketHeader)) < packet_real_size) {
                            sock_receive->spare_packet = packet;
                            to_copy_size = remaining_buffer_size - sizeof (PacketHeader);
                            sock_receive->missing_packet = packet_real_size - to_copy_size;
                        }

                        else {
                            to_copy_size = packet_real_size;
                            packet_delete (sock_receive->spare_packet);
                            sock_receive->spare_packet = NULL;
                        } 

                        packet_set_data (packet, (void *) end, to_copy_size);

                        end += to_copy_size;
                        buffer_pos += to_copy_size;

                        if (!sock_receive->spare_packet) 
                            cerver_packet_select_handler (cerver, sock_fd, packet, on_hold);
                    }

                    else {
                        #ifdef CERVER_DEBUG
                        cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                            c_string_create ("Failed to create a new packet in cerver_handle_receive_buffer () in cerver %s.", 
                            cerver->info->name->str));
                        #endif
                    }
                }

                else {
                    #ifdef CERVER_DEBUG 
                    cerver_log_msg (stderr, LOG_WARNING, LOG_PACKET, 
                        c_string_create ("Got a packet of invalid size: %ld in cerver %s.",
                        packet_size, cerver->info->name->str));
                    #endif
                    break;
                }
            }

            else break;
        }
    }

}

// handles a failed recive from a connection associatd with a client
// end sthe connection to prevent seg faults or signals for bad sock fd
static void cerver_receive_handle_failed (CerverReceive *cr) {

    if (cr->on_hold) {
        Connection *connection = connection_get_by_sock_fd_from_on_hold (cr->cerver, cr->sock_fd);
        if (connection) on_hold_connection_drop (cr->cerver, connection);

        // for what ever reason we have a rogue connection
        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Sock fd %d is not associated with an on hold connection in cerver %s",
                cr->sock_fd, cr->cerver->info->name->str));
            #endif
            close (cr->sock_fd);
        }
    }

    else {
        // check if the socket belongs to a player inside a lobby
        if (cr->lobby->players->size > 0) {
            Player *player = player_get_by_sock_fd_list (cr->lobby, cr->sock_fd);
            if (player) player_unregister_from_lobby (cr->lobby, player);
        }

        // get to which client the connection is registered to
        Client *client = client_get_by_sock_fd (cr->cerver, cr->sock_fd);
        if (client) client_remove_connection_by_sock_fd (cr->cerver, client, cr->sock_fd);

        // for what ever reason we have a rogue connection
        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Sock fd: %d is not registered to a client in cerver %s",
                cr->sock_fd, cr->cerver->info->name->str));
            #endif
            close (cr->sock_fd);        // just close the socket
        }
    }

}

// receive all incoming data from the socket
void cerver_receive (void *ptr) {

    if (ptr) {
        CerverReceive *cr = (CerverReceive *) ptr;

        if (cr->cerver && (cr->sock_fd != -1)) {
            // do {
                char *packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
                if (packet_buffer) {
                    ssize_t rc = recv (cr->sock_fd, packet_buffer, cr->cerver->receive_buffer_size, 0);
                    // ssize_t rc = read (cr->sock_fd, packet_buffer, cr->cerver->receive_buffer_size);

                    if (rc < 0) {
                        if (errno != EWOULDBLOCK) {     // no more data to read 
                            #ifdef CERVER_DEBUG 
                            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                                c_string_create ("cerver_receive () - rc < 0 - sock fd: %d", cr->sock_fd));
                            perror ("Error ");
                            #endif

                            cerver_receive_handle_failed (cr);
                        }

                        // break;
                    }

                    else if (rc == 0) {
                        // man recv -> steam socket perfomed an orderly shutdown
                        // but in dgram it might mean something?
                        #ifdef CERVER_DEBUG
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                            c_string_create ("cerver_recieve () - rc == 0 - sock fd: %d",
                            cr->sock_fd));
                        // perror ("Error ");
                        #endif

                        int err = errno;
                        if (err) {
                            switch (err) {
                                case EAGAIN: 
                                    printf ("Is the connection still opened?\n"); 
                                    // cerver_receive_handle_failed (cr);
                                    break;
                                case EBADF:
                                case ENOTSOCK: {
                                    #ifdef CERVER_DEBUG
                                    perror ("Error ");
                                    #endif
                                    cerver_receive_handle_failed (cr);
                                }
                            }
                        }

                        else cerver_receive_handle_failed (cr);

                        // break;
                    }

                    else {
                        // cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                        //     c_string_create ("Cerver %s rc: %ld for sock fd: %d",
                        //     cr->cerver->info->name->str, rc, cr->sock_fd));

                        if (cr->lobby) {
                            cr->lobby->stats->n_receives_done += 1;
                            cr->lobby->stats->bytes_received += rc;
                        }

                        if (cr->on_hold) {
                            cr->cerver->stats->on_hold_receives_done += 1;
                            cr->cerver->stats->on_hold_bytes_received += rc;
                        }

                        else {
                            cr->cerver->stats->client_receives_done += 1;
                            cr->cerver->stats->client_bytes_received += rc;
                        }

                        cr->cerver->stats->total_n_receives_done += 1;
                        cr->cerver->stats->total_bytes_received += rc;

                        // handle the received packet buffer -> split them in packets of the correct size
                        cerver_receive_handle_buffer (cr->cerver, cr->sock_fd, 
                            packet_buffer, rc, cr->on_hold, cr->lobby);
                    }

                    free (packet_buffer);
                }

                else {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                        "Failed to allocate a new packet buffer!");
                    #endif
                    // break;
                }
            // } while (true);
        }

        cerver_receive_delete (cr);
    }

}

#pragma endregion

#pragma region accept

static void cerver_register_new_connection (Cerver *cerver, 
    const i32 new_fd, const struct sockaddr_storage client_address) {

    Connection *connection = connection_create (new_fd, client_address, cerver->protocol);
    if (connection) {
        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT,
                c_string_create ("New connection from IP address: %s -- Port: %d", 
                connection->ip->str, connection->port));
        #endif

        Client *client = NULL;
        bool failed = false;

        // if the cerver requires auth, we put the connection on hold
        if (cerver->auth_required) 
            on_hold_connection (cerver, connection);

        // if not, we create a new client and we register the connection
        else {
            client = client_create ();
            if (client) {
                connection_register_to_client (client, connection);

                // if we need to generate session ids...
                if (cerver->use_sessions) {
                    char *session_id = (char *) cerver->session_id_generator (client);
                    if (session_id) {
                        #ifdef CERVER_DEBUG
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT,
                            c_string_create ("Generated client session id: %s", session_id));
                        #endif
                        client_set_session_id (client, session_id);

                        if (!client_register_to_cerver ((Cerver *) cerver, client)) {
                            // trigger cerver on client connected action
                            if (cerver->on_client_connected) 
                                cerver->on_client_connected (client);
                        }
                    }

                    else {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, 
                            "Failed to generate client session id!");
                        // TODO: do we want to drop the client connection?
                        failed = true;
                    }
                }

                // just register the client to the cerver
                else {
                    if (!client_register_to_cerver ((Cerver *) cerver, client)) {
                        // trigger cerver on client connected action
                        if (cerver->on_client_connected) 
                            cerver->on_client_connected (client);
                    }
                } 
            }
        }

        if (!failed) {
            // send cerver info packet
            if (cerver->type != WEB_CERVER) {
                packet_set_network_values (cerver->info->cerver_info_packet, 
                    cerver, client, connection, NULL);
                if (packet_send (cerver->info->cerver_info_packet, 0, NULL, false)) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, 
                        c_string_create ("Failed to send cerver %s info packet!", cerver->info->name->str));
                }   
            }
        }

        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, 
                c_string_create ("New connection to cerver %s!", cerver->info->name->str));
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
        if (new_fd > 0) {
            printf ("Accepted fd: %d\n", new_fd);
            cerver_register_new_connection (cerver, new_fd, client_address);
        } 

        else {
            // if we get EWOULDBLOCK, we have accepted all connections
            if (errno != EWOULDBLOCK) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Accept failed!");
                perror ("Error");
            } 
        }
    }

}

#pragma endregion

#pragma region poll

// reallocs main cerver poll fds
// returns 0 on success, 1 on error
u8 cerver_realloc_main_poll_fds (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        u32 current_max = cerver->max_n_fds;
        cerver->max_n_fds = cerver->max_n_fds * 2;
        cerver->fds = (struct pollfd *) realloc (cerver->fds, cerver->max_n_fds * sizeof (struct pollfd));
        if (cerver->fds) {
            for (u32 i = current_max; i < cerver->max_n_fds; i++)
                cerver->fds[i].fd == -1;

            retval = 0;
        }
    }

    return retval;

}

// get a free index in the main cerver poll strcuture
i32 cerver_poll_get_free_idx (Cerver *cerver) {

    if (cerver) {
        for (u32 i = 0; i < cerver->max_n_fds; i++)
            if (cerver->fds[i].fd == -1) return i;

        return -1;
    }

    return 0;

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
                cerver->info->name->str, idx));
            #endif

            retval = 0;
        }

        else if (idx < 0) {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, 
                c_string_create ("Cerver %s main poll is full -- we need to realloc...", 
                cerver->info->name->str));
            #endif
            if (cerver_realloc_main_poll_fds (cerver)) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                    c_string_create ("Failed to realloc cerver %s main poll fds!", 
                    cerver->info->name->str));
            }

            // try again to register the connection
            else {
                retval = cerver_poll_register_connection (cerver, client, connection);
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
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER,
                c_string_create ("Removed sock fd from cerver %s main poll, idx: %d",
                cerver->info->name->str, idx));
            #endif

            cerver->fds[idx].fd = -1;
            cerver->fds[idx].events = -1;
            cerver->current_n_fds--;

            retval = 0;     // removed the sock fd form the cerver poll
        }
    }

    return retval;

}

// cerver poll loop to handle events in the registered socket's fds
u8 cerver_poll (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, 
            c_string_create ("Cerver %s main handler has started!", cerver->info->name->str));
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Waiting for connections...");
        #endif

        int poll_retval = 0;

        while (cerver->isRunning) {
            poll_retval = poll (cerver->fds, cerver->max_n_fds, cerver->poll_timeout);

            // poll failed
            if (poll_retval < 0) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    c_string_create ("Cerver %s main poll has failed!", cerver->info->name->str));
                perror ("Error");
                cerver->isRunning = false;
                break;
            }

            // if poll has timed out, just continue to the next loop... 
            if (poll_retval == 0) {
                // #ifdef CERVER_DEBUG
                // cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, 
                //    c_string_create ("Cerver %s poll timeout", cerver->info->name->str));
                // #endif
                continue;
            }

            // one or more fd(s) are readable, need to determine which ones they are
            for (u32 i = 0; i < cerver->max_n_fds; i++) {
                if (cerver->fds[i].fd != -1) {
                    CerverReceive *cr = cerver_receive_new (cerver, cerver->fds[i].fd, false, NULL);

                    switch (cerver->fds[i].revents) {
                        // A connection setup has been completed or new data arrived
                        case POLLIN: {
                            // accept incoming connections that are queued
                            if (i == 0) {
                                if (thpool_add_work (cerver->thpool, cerver_accept, cerver))  {
                                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                                        c_string_create ("Failed to add cerver_accept () to cerver's %s thpool!", 
                                        cerver->info->name->str));
                                }
                            }

                            // not the cerver socket, so a connection fd must be readable
                            else {
                                // printf ("Receive fd: %d\n", cerver->fds[i].fd);
                                cerver_receive (cr);
                                // if (thpool_add_work (cerver->thpool, cerver_receive, 
                                //     cerver_receive_new (cerver, cerver->fds[i].fd, false))) {
                                //     cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                                //         c_string_create ("Failed to add cerver_receive () to cerver's %s thpool!", 
                                //         cerver->info->name->str));
                                // }
                            } 
                        } break;

                        // A disconnection request has been initiated by the other end
                        // or a connection is broken (SIGPIPE is also set when we try to write to it)
                        // or The other end has shut down one direction
                        case POLLHUP: {
                            cerver_receive_handle_failed (cr);
                            cerver_receive_delete (cr);
                        } break;

                        // An asynchronous error occurred
                        case POLLERR: {
                            cerver_receive_handle_failed (cr);
                            cerver_receive_delete (cr);
                        } break;

                        // Urgent data arrived. SIGURG is sent then.
                        case POLLPRI: {
                            /*** TODO: ***/
                        } break;

                        default: break;
                    }
                }
            }

            // if (cerver->compress_clients) cerver_poll_compress_clients (cerver);
        }

        #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, 
                c_string_create ("Cerver %s main poll has stopped!", cerver->info->name->str));
        #endif

        retval = 0;
    }

    else cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, "Can't listen for connections on a NULL cerver!");

    return retval;

}

#pragma endregion