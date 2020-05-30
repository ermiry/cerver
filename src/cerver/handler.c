#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>

#include <sys/prctl.h>

#include "cerver/types/types.h"

#include "cerver/collections/htab.h"

#include "cerver/socket.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/packets.h"
#include "cerver/handler.h"
#include "cerver/auth.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/jobs.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

#pragma region handler

static HandlerData *handler_data_new (int handler_id, void *data, Packet *packet) {

    HandlerData *handler_data = (HandlerData *) malloc (sizeof (HandlerData));
    if (handler_data) {
        handler_data->handler_id = handler_id;

        handler_data->data = data;
        handler_data->packet = packet;
    }

    return handler_data;

}

static inline void handler_data_delete (HandlerData *handler_data) { 

    if (handler_data) free (handler_data);
    
}

static Handler *handler_new (void) {

    Handler *handler = (Handler *) malloc (sizeof (Handler));
    if (handler) {
        handler->id = -1;
        handler->thread_id = 0;

        handler->data = NULL;
        handler->data_create = NULL;
        handler->data_create_args = NULL;
        handler->data_delete = NULL;

        handler->handler = NULL;
        handler->direct_handle = false;

        handler->job_queue = NULL;

        handler->cerver = NULL;
    }

    return handler;

}

void handler_delete (void *handler_ptr) {

    if (handler_ptr) {
        Handler *handler = (Handler *) handler_ptr;

        job_queue_delete (handler->job_queue);

        free (handler_ptr);
    }

}

// creates a new handler
// handler method is your actual app packet handler
Handler *handler_create (Action handler_method) {

    Handler *handler = handler_new ();
    if (handler) {
        handler->handler = handler_method;

        handler->job_queue = job_queue_create ();
    }

    return handler;

}

// creates a new handler that will be used for cerver's multiple app handlers configuration
// it should be registered to the cerver before it starts
// the user is responsible for setting the unique id, which will be used to match
// incoming packets
// handler method is your actual app packet handler
Handler *handler_create_with_id (int id, Action handler_method) {

    Handler *handler = handler_create (handler_method);
    if (handler) {
        handler->id = id;
    }

    return handler;

}

// sets the handler's data directly
// this data will be passed to the handler method using a HandlerData structure
void handler_set_data (Handler *handler, void *data) {

    if (handler) handler->data = data;

}

// set a method to create the handler's data before it starts handling any packet
// this data will be passed to the handler method using a HandlerData structure
void handler_set_data_create (Handler *handler, 
    void *(*data_create) (void *args), void *data_create_args) {

    if (handler) {
        handler->data_create = data_create;
        handler->data_create_args = data_create_args;
    }

}

// set the method to be used to delete the handler's data
void handler_set_data_delete (Handler *handler, Action data_delete) {

    if (handler) handler->data_delete = data_delete;

}

// used to avoid pushing job to the queue and instead handle
// the packet directly in the same thread
void handler_set_direct_handle (Handler *handler, bool direct_handle) {

    if (handler) handler->direct_handle = direct_handle;

}

static void *handler_do (void *handler_ptr) {

    if (handler_ptr) {
        Handler *handler = (Handler *) handler_ptr;

        // set the thread name
        if (handler->id >= 0) {
            char thread_name[128] = { 0 };
            snprintf (thread_name, 128, "handler-%d", handler->id);
            prctl (PR_SET_NAME, thread_name);
        }

        // TODO: register to signals to handle multiple actions

        if (handler->data_create) 
            handler->data = handler->data_create (handler->data_create_args);

        // mark the handler as alive and ready
        pthread_mutex_lock (handler->cerver->handlers_lock);
        handler->cerver->num_handlers_alive += 1;
        pthread_mutex_unlock (handler->cerver->handlers_lock);

        // while cerver is running, check for new jobs and handle them
        while (handler->cerver->isRunning) {
            bsem_wait (handler->job_queue->has_jobs);

            if (handler->cerver->isRunning) {
                pthread_mutex_lock (handler->cerver->handlers_lock);
                handler->cerver->num_handlers_working += 1;
                pthread_mutex_unlock (handler->cerver->handlers_lock);

                // read job from queue
                Job *job = job_queue_pull (handler->job_queue);
                if (job) {
                    Packet *packet = (Packet *) job->args;
                    HandlerData *handler_data = handler_data_new (handler->id, handler->data, packet);

                    handler->handler (handler_data);

                    handler_data_delete (handler_data);
                    job_delete (job);
                    packet_delete (packet);
                }

                pthread_mutex_lock (handler->cerver->handlers_lock);
                handler->cerver->num_handlers_working -= 1;
                pthread_mutex_unlock (handler->cerver->handlers_lock);
            }
        }

        if (handler->data_delete)
            handler->data_delete (handler->data);

        pthread_mutex_lock (handler->cerver->handlers_lock);
        handler->cerver->num_handlers_alive -= 1;
        pthread_mutex_unlock (handler->cerver->handlers_lock);
    }

    return NULL;

}

// starts the new handler by creating a dedicated thread for it
// called by internal cerver methods
int handler_start (Handler *handler) {

    int retval = 1;

    if (handler) {
        // retval = pthread_create (
        //     &handler->thread_id,
        //     NULL,
        //     (void *(*)(void *)) handler_do,
        //     (void *) handler
        // );


        // 26/05/2020 -- 12:54 
        // handler's threads are not explicitly joined by pthread_join () 
        // on cerver teardown
        retval = thread_create_detachable (
            &handler->thread_id,
            (void *(*)(void *)) handler_do,
            (void *) handler
        );  
    }

    return retval;

}

#pragma endregion

#pragma region auxiliary structures

static ReceiveHandle *receive_handle_new (Cerver *cerver, Socket *socket, 
    char *buffer, size_t buffer_size, bool on_hold, Lobby *lobby) {

    ReceiveHandle *receive = (ReceiveHandle *) malloc (sizeof (ReceiveHandle));
    if (receive) {
        receive->cerver = cerver;
        // receive->sock_fd = sock_fd;
        receive->buffer = buffer;
        receive->buffer_size = buffer_size;
        receive->socket = socket;
        receive->on_hold = on_hold;
        receive->lobby = lobby;
    }

    return receive;

}

void receive_handle_delete (void *receive_ptr) {

    if (receive_ptr) {
        ReceiveHandle *receive = (ReceiveHandle *) receive_ptr;

        receive->cerver = NULL;
        // receive->buffer = NULL;
        receive->socket = NULL;
        receive->lobby = NULL;

        free (receive);
    }

}

SockReceive *sock_receive_new (void) {

    SockReceive *sr = (SockReceive *) malloc (sizeof (SockReceive));
    if (sr) {
        sr->spare_packet = NULL;
        sr->missing_packet = 0;

        sr->header = NULL;
        sr->header_end = NULL;
        // sr->curr_header_pos = 0;
        sr->remaining_header = 0;
        sr->complete_header = false;
    } 

    return sr;

}

void sock_receive_delete (void *sock_receive_ptr) {

    if (sock_receive_ptr) {
        SockReceive *sock_receive = (SockReceive *) sock_receive_ptr;

        packet_delete (sock_receive->spare_packet);
        if (sock_receive->header) free (sock_receive->header);

        free (sock_receive_ptr);
    }

}

static SockReceive *sock_receive_get (Cerver *cerver, i32 sock_fd, bool on_hold) {

    SockReceive *sock_receive = NULL;

    if (cerver) {
        if (on_hold) {
            Connection *connection = connection_get_by_sock_fd_from_on_hold (cerver, sock_fd);
            if (connection) {
                sock_receive = connection->sock_receive;
            }
        }

        else {
            Client *client = client_get_by_sock_fd (cerver, sock_fd);
            if (client) {
                Connection *connection = connection_get_by_sock_fd_from_client (client, sock_fd);
                if (connection) {
                    sock_receive = connection->sock_receive;
                }
            }
        }
    }

    return sock_receive;

}

CerverReceive *cerver_receive_new (Cerver *cerver, Socket *socket, bool on_hold, Lobby *lobby) {

    CerverReceive *cr = (CerverReceive *) malloc (sizeof (CerverReceive));
    if (cr) {
        cr->cerver = cerver;
        // cr->sock_fd = sock_fd;
        cr->socket = socket;
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
                case CLIENT_CLOSE_CONNECTION: {
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("Client %ld request to close the connection",
                        packet->client->id);
                    if (s) {
                        cerver_log_debug (s);
                        free (s);
                    }
                    #endif

                    // check if the client is inside a lobby
                    if (packet->lobby) {
                        #ifdef CERVER_DEBUG
                        char *s = c_string_create ("Client %ld inside lobby %s wants to close the connection...",
                            packet->client->id, packet->lobby->id->str);
                        if (s) {
                            cerver_log_msg (stdout, LOG_DEBUG, LOG_GAME, s);
                            free (s);
                        }
                        #endif

                        // remove the player from the lobby
                        Player *player = player_get_by_sock_fd_list (packet->lobby, packet->connection->socket->sock_fd);
                        player_unregister_from_lobby (packet->lobby, player);
                    }

                    client_remove_connection_by_sock_fd (packet->cerver, 
                        packet->client, packet->connection->socket->sock_fd); 
                } break;

                // the client is going to disconnect and will close all of its active connections
                // so drop it from the server
                case CLIENT_DISCONNET: {
                    // check if the client is inside a lobby
                    if (packet->lobby) {
                        #ifdef CERVER_DEBUG
                        char *s = c_string_create ("Client %ld inside lobby %s wants to close the connection...",
                            packet->client->id, packet->lobby->id->str);
                        if (s) {
                            cerver_log_msg (stdout, LOG_DEBUG, LOG_GAME, s);
                            free (s);
                        }
                        #endif

                        // remove the player from the lobby
                        Player *player = player_get_by_sock_fd_list (packet->lobby, packet->connection->socket->sock_fd);
                        player_unregister_from_lobby (packet->lobby, player);
                    }

                    client_drop (packet->cerver, packet->client);
                } break;

                default: {
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("Got an unknown request in cerver %s",
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, s);
                        free (s);
                    }
                    #endif
                } break;
            }
        }
    }

}

// sends back a test packet to the client!
void cerver_test_packet_handler (Packet *packet) {

    if (packet) {
        #ifdef CERVER_DEBUG
        char *s = c_string_create ("Got a test packet in cerver %s.", packet->cerver->info->name->str);
        if (s) {
            cerver_log_msg (stdout, LOG_DEBUG, LOG_PACKET, s);
            free (s);
        }
        #endif

        Packet *test_packet = packet_new ();
        if (test_packet) {
            packet_set_network_values (test_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
            test_packet->packet_type = TEST_PACKET;
            packet_generate (test_packet);
            if (packet_send (test_packet, 0, NULL, false)) {
                char *s = c_string_create ("Failed to send error packet from cerver %s.", 
                    packet->cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, s);
                    free (s);
                }
            }

            packet_delete (test_packet);
        }
    }

}

// 27/01/2020
// handles a APP_PACKET packet type
static void app_packet_handler (Packet *packet) {

    if (packet) {
        // 11/05/2020
        if (packet->cerver->multiple_handlers) {
            // select which handler to use
            if (packet->header->handler_id < packet->cerver->n_handlers) {
                if (packet->cerver->handlers[packet->header->handler_id]) {
                    // add the packet to the handler's job queueu to be handled
                    // as soon as the handler is available
                    if (job_queue_push (
                        packet->cerver->handlers[packet->header->handler_id]->job_queue,
                        job_create (NULL, packet)
                    )) {
                        char *s = c_string_create ("Failed to push a new job to cerver's %s <%d> handler!",
                            packet->cerver->info->name->str, packet->header->handler_id);
                        if (s) {
                            cerver_log_error (s);
                            free (s);
                        }
                    }
                }
            }
        }

        else {
            if (packet->cerver->app_packet_handler) {
                if (packet->cerver->app_packet_handler->direct_handle) {
                    // printf ("app_packet_handler - direct handle!\n");
                    packet->cerver->app_packet_handler->handler (packet);
                }

                else {
                    // add the packet to the handler's job queueu to be handled
                    // as soon as the handler is available
                    if (job_queue_push (
                        packet->cerver->app_packet_handler->job_queue,
                        job_create (NULL, packet)
                    )) {
                        char *s = c_string_create ("Failed to push a new job to cerver's %s app_packet_handler!",
                            packet->cerver->info->name->str);
                        if (s) {
                            cerver_log_error (s);
                            free (s);
                        }
                    }
                }
            }

            else {
                char *s = c_string_create ("Cerver %s does not have an app_packet_handler!",
                    packet->cerver->info->name->str);
                if (s) {
                    cerver_log_warning (s);
                    free (s);
                }
            }
        }
    }

}

// 27/05/2020
// handles a APP_ERROR_PACKET packet type
static void app_error_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->cerver->app_error_packet_handler) {
            if (packet->cerver->app_error_packet_handler->direct_handle) {
                // printf ("app_error_packet_handler - direct handle!\n");
                packet->cerver->app_error_packet_handler->handler (packet);
            }

            else {
                // add the packet to the handler's job queueu to be handled
                // as soon as the handler is available
                if (job_queue_push (
                    packet->cerver->app_error_packet_handler->job_queue,
                    job_create (NULL, packet)
                )) {
                    char *s = c_string_create ("Failed to push a new job to cerver's %s app_error_packet_handler!",
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            char *s = c_string_create ("Cerver %s does not have an app_error_packet_handler!",
                packet->cerver->info->name->str);
            if (s) {
                cerver_log_warning (s);
                free (s);
            }
        }
    }

}

// 27/05/2020
// handles a CUSTOM_PACKET packet type
static void custom_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->cerver->custom_packet_handler) {
            if (packet->cerver->custom_packet_handler->direct_handle) {
                // printf ("custom_packet_handler - direct handle!\n");
                packet->cerver->custom_packet_handler->handler (packet);
            }

            else {
                // add the packet to the handler's job queueu to be handled
                // as soon as the handler is available
                if (job_queue_push (
                    packet->cerver->custom_packet_handler->job_queue,
                    job_create (NULL, packet)
                )) {
                    char *s = c_string_create ("Failed to push a new job to cerver's %s custom_packet_handler!",
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_error (s);
                        free (s);
                    }
                }
            }
        }

        else {
            char *s = c_string_create ("Cerver %s does not have a custom_packet_handler!",
                packet->cerver->info->name->str);
            if (s) {
                cerver_log_warning (s);
                free (s);
            }
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
                    app_packet_handler (packet);
                    break;

                // user set handler to handle app specific errors
                case APP_ERROR_PACKET: 
                    packet->cerver->stats->received_packets->n_app_error_packets += 1;
                    packet->client->stats->received_packets->n_app_error_packets += 1;
                    packet->connection->stats->received_packets->n_app_error_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_app_error_packets += 1;
                    app_error_packet_handler (packet);
                    break;

                // custom packet hanlder
                case CUSTOM_PACKET: 
                    packet->cerver->stats->received_packets->n_custom_packets += 1;
                    packet->client->stats->received_packets->n_custom_packets += 1;
                    packet->connection->stats->received_packets->n_custom_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_custom_packets += 1;
                    custom_packet_handler (packet);
                    break;

                // acknowledge the client we have received his test packet
                case TEST_PACKET: 
                    packet->cerver->stats->received_packets->n_test_packets += 1;
                    packet->client->stats->received_packets->n_test_packets += 1;
                    packet->connection->stats->received_packets->n_test_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_test_packets += 1;
                    cerver_test_packet_handler (packet); 
                    break;

                default: {
                    packet->cerver->stats->received_packets->n_bad_packets += 1;
                    packet->client->stats->received_packets->n_bad_packets += 1;
                    packet->connection->stats->received_packets->n_bad_packets += 1;
                    if (packet->lobby) packet->lobby->stats->received_packets->n_bad_packets += 1;
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("Got a packet of unknown type in cerver %s.", 
                        packet->cerver->info->name->str);
                    if (s) {
                        cerver_log_msg (stdout, LOG_WARNING, LOG_PACKET, s);
                        free (s);
                    }
                    #endif
                } break;
            }
        // }

        switch (packet->header->packet_type) {
            case APP_PACKET: {
                if (packet->cerver->multiple_handlers) {
                    // do nothing - packet gets deleted in handler method
                }
            } break;

            default:
                packet_delete (packet);
                break;
        }
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
            char *s = c_string_create ("Failed to get on hold connection associated with sock: %i",
                sock_fd);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                free (s);
            }
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
            char *s = c_string_create ("Failed to get client associated with sock: %i.", sock_fd);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CLIENT, s);
                free (s);
            }
            #endif
            // no client - discard the data!
            packet_delete (packet);
        }
    }

}

#pragma endregion

#pragma region receive

static void cerver_receive_handle_spare_packet (Cerver *cerver, i32 sock_fd, bool on_hold,
    SockReceive *sock_receive,
    size_t buffer_size, char **end, size_t *buffer_pos) {

    if (sock_receive) {
        if (sock_receive->header) {
            // copy the remaining header size
            memcpy (sock_receive->header_end, (void *) *end, sock_receive->remaining_header);
            // *end += sock_receive->remaining_header;
            // *buffer_pos += sock_receive->remaining_header;
            
            // printf ("size in new header: %ld\n", ((PacketHeader *) sock_receive->header)->packet_size);
            // packet_header_print (((PacketHeader *) sock_receive->header));

            sock_receive->complete_header = true;
        }

        else if (sock_receive->spare_packet) {
            size_t copy_to_spare = 0;
            if (sock_receive->missing_packet < buffer_size) 
                copy_to_spare = sock_receive->missing_packet;

            else copy_to_spare = buffer_size;

            // printf ("copy to spare: %ld\n", copy_to_spare);

            // append new data from buffer to the spare packet
            if (copy_to_spare > 0) {
                packet_append_data (sock_receive->spare_packet, (void *) *end, copy_to_spare);

                // check if we can handle the packet 
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
                // printf ("buffer pos after copy to spare: %ld\n", *buffer_pos);
            }
        }
    }

}

// default cerver receive handler
void cerver_receive_handle_buffer (void *receive_ptr) {

    if (receive_ptr) {
        // printf ("cerver_receive_handle_buffer ()\n");
        ReceiveHandle *receive = (ReceiveHandle *) receive_ptr;

        Cerver *cerver = receive->cerver;
        // i32 sock_fd = receive->sock_fd;
        char *buffer = receive->buffer;
        size_t buffer_size = receive->buffer_size;
        i32 sock_fd = receive->socket->sock_fd;
        // char *buffer = receive->socket->packet_buffer;
        // size_t buffer_size = receive->socket->packet_buffer_size;
        bool on_hold = receive->on_hold;
        Lobby *lobby = receive->lobby;

        pthread_mutex_lock (receive->socket->mutex);

        SockReceive *sock_receive = sock_receive_get (cerver, sock_fd, on_hold);
        if (sock_receive) {
            if (buffer && (buffer_size > 0)) {
                char *end = buffer;
                size_t buffer_pos = 0;

                cerver_receive_handle_spare_packet (
                    cerver, sock_fd, on_hold,
                    sock_receive,
                    buffer_size, &end, &buffer_pos
                );

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
                                packet->cerver = cerver;
                                packet->lobby = lobby;

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
                                    cerver_packet_select_handler (cerver, sock_fd, packet, on_hold);
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

        else {
            #ifdef CERVER_DEBUG
            char *status = c_string_create ("Sock fd: %d does not have an associated sock_receive in cerver %s.",
                sock_fd, cerver->info->name->str);
            if (status) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, status);
                free (status);
            }
            #endif
        }

        // 28/05/2020 -- deleting the created buffer from cerver_receive ()
        // to correct handle both cases: using thpool and single threaded
        if (buffer) free (receive->buffer);

        // free (receive->socket->packet_buffer);
        // receive->socket->packet_buffer = NULL;

        pthread_mutex_unlock (receive->socket->mutex);

        receive_handle_delete (receive);
    }

}

// handles a failed recive from a connection associatd with a client
// end sthe connection to prevent seg faults or signals for bad sock fd
static void cerver_receive_handle_failed (void *cr_ptr) {

    if (cr_ptr) {
        CerverReceive *cr = (CerverReceive *) cr_ptr;

        pthread_mutex_lock (cr->socket->mutex);

        if (cr->socket->sock_fd > 0) {
            if (cr->on_hold) {
                Connection *connection = connection_get_by_sock_fd_from_on_hold (cr->cerver, cr->socket->sock_fd);
                if (connection) {
                    on_hold_connection_drop (cr->cerver, connection);
                }

                // for what ever reason we have a rogue connection
                else {
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("Sock fd %d is not associated with an on hold connection in cerver %s",
                        cr->socket->sock_fd, cr->cerver->info->name->str);
                    if (s) {
                        cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, s);
                        free (s);
                    }
                    #endif

                    close (cr->socket->sock_fd);
                    cr->socket->sock_fd = -1;
                    // cerver_sockets_pool_push (cr->cerver, cr->socket);
                }
            }

            else {
                // check if the socket belongs to a player inside a lobby
                if (cr->lobby) {
                    if (cr->lobby->players->size > 0) {
                        Player *player = player_get_by_sock_fd_list (cr->lobby, cr->socket->sock_fd);
                        if (player) player_unregister_from_lobby (cr->lobby, player);
                    }
                }

                // get to which client the connection is registered to
                Client *client = client_get_by_sock_fd (cr->cerver, cr->socket->sock_fd);
                if (client) {
                    client_remove_connection_by_sock_fd (cr->cerver, client, cr->socket->sock_fd);
                } 

                // for what ever reason we have a rogue connection
                else {
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("Sock fd: %d is not registered to a client in cerver %s",
                        cr->socket->sock_fd, cr->cerver->info->name->str);
                    if (s) {
                        cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, s);
                        free (s);
                    }
                    #endif

                    close (cr->socket->sock_fd);        // just close the socket
                    cr->socket->sock_fd = -1;
                    // cerver_sockets_pool_push (cr->cerver, cr->socket);
                }
            }
        }

        pthread_mutex_unlock (cr->socket->mutex);

        cerver_receive_delete (cr);
    }

}

// 28/05/2020 -- correctly call cerver_receive_handle_failed ()
static void cerver_switch_receive_handle_failed (CerverReceive *cr) {

    if (cr) {
        if (cr->cerver->thpool) {
            if (thpool_add_work (cr->cerver->thpool, cerver_receive_handle_failed, cr)) {
                char *s = c_string_create (
                    "Failed to add cerver_receive_handle_failed () to cerver's %s thpool!", 
                    cr->cerver->info->name->str
                );
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                    free (s);
                }
            }
        }

        else {
            cerver_receive_handle_failed (cr);
        }
    }

}

// receive all incoming data from the socket
void cerver_receive (void *ptr) {

    if (ptr) {
        CerverReceive *cr = (CerverReceive *) ptr;
        
        if (cr->cerver && (cr->socket->sock_fd != -1)) {
            char *packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
            // cr->socket->packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
            if (packet_buffer) {
                // ssize_t rc = read (cr->sock_fd, packet_buffer, cr->cerver->receive_buffer_size);
                // ssize_t rc = recv (cr->sock_fd, packet_buffer, cr->cerver->receive_buffer_size, 0);
                ssize_t rc = recv (cr->socket->sock_fd, packet_buffer, cr->cerver->receive_buffer_size, 0);

                if (rc < 0) {
                    // no more data to read 
                    if (errno != EWOULDBLOCK) {
                        #ifdef CERVER_DEBUG 
                        char *s = c_string_create ("cerver_receive () - rc < 0 - sock fd: %d", 
                            cr->socket->sock_fd);
                        if (s) {
                            cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                            free (s);
                        }
                        perror ("Error ");
                        #endif

                        cerver_switch_receive_handle_failed (cr);
                    }
                }

                else if (rc == 0) {
                    // man recv -> steam socket perfomed an orderly shutdown
                    // but in dgram it might mean something?
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("cerver_recieve () - rc == 0 - sock fd: %d",
                        cr->socket->sock_fd);
                    if (s) {
                        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, s);
                        free (s);
                    }
                    // perror ("Error ");
                    #endif

                    cerver_switch_receive_handle_failed (cr);
                }

                else {
                    cr->socket->packet_buffer_size = rc;

                    // char *status = c_string_create ("Cerver %s rc: %ld for sock fd: %d",
                    //     cr->cerver->info->name->str, rc, cr->sock_fd);
                    // if (status) {
                    //     cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                    //     free (status);
                    // }

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
                    ReceiveHandle *receive = receive_handle_new (
                        cr->cerver,
                        cr->socket,
                        packet_buffer,
                        rc,
                        cr->on_hold,
                        cr->lobby
                    );

                    if (cr->cerver->thpool) {
                        // 28/05/2020 -- 02:37 -- added thpool here instead of cerver_poll ()
                        // and it seems to be working as expected
                        if (thpool_add_work (cr->cerver->thpool, cr->cerver->handle_received_buffer, receive)) {
                            char *s = c_string_create (
                                "Failed to add cerver's cr->cerver->handle_received_buffer () to thpool!",
                                cr->cerver->info->name->str
                            );
                            if (s) {
                                cerver_log_error (s);
                                free (s);
                            }
                        }

                        // TODO: 28/05/2020 -- create a better method in thpool
                        // char *status = c_string_create ("Cerver %s active thpool threads: %i", 
                        //     cr->cerver->info->name->str,
                        //     thpool_num_threads_working (cr->cerver->thpool));
                        // if (status) {
                        //     cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, status);
                        //     free (status);
                        // }
                    }

                    else {
                        cr->cerver->handle_received_buffer (receive);

                        // 28/05/2020 -- called from inside cerver_receive_handle_buffer ()
                        // receive_handle_delete (receive);
                    }

                    cerver_receive_delete (cr);
                }

                // 28/05/2020 -- 02:40
                // packet_buffer is not free from inside cr->cerver->handle_received_buffer ()
                // free (packet_buffer);
            }

            else {
                #ifdef CERVER_DEBUG
                cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
                    "Failed to allocate a new packet buffer!");
                #endif
                // break;
            }
        }
    }
}

#pragma endregion

#pragma region accept

static void cerver_register_new_connection (Cerver *cerver, 
    const i32 new_fd, const struct sockaddr_storage client_address) {

    Connection *connection = connection_create (new_fd, client_address, cerver->protocol);
    if (connection) {
        #ifdef CERVER_DEBUG
        char *s = c_string_create ("New connection from IP address: %s -- Port: %d", 
            connection->ip->str, connection->port);
        if (s) {
            cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, s);
            free (s);
        }
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
                        char *s = c_string_create ("Generated client session id: %s", session_id);
                        if (s) {
                            cerver_log_msg (stdout, LOG_DEBUG, LOG_CLIENT, s);
                            free (s);
                        }
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
                    char *s = c_string_create ("Failed to send cerver %s info packet!",
                         cerver->info->name->str);
                    if (s) {
                        cerver_log_msg (stderr, LOG_ERROR, LOG_PACKET, s);
                        free (s);
                    }
                }   
            }
        }

        #ifdef CERVER_DEBUG
        s = c_string_create ("New connection to cerver %s!", cerver->info->name->str);
        if (s) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, s);
            free (s);
        }
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
            #pragma GCC diagnostic push
            #pragma GCC diagnostic ignored "-Wunused-value"
                for (u32 i = current_max; i < cerver->max_n_fds; i++)
                    cerver->fds[i].fd == -1;
            #pragma GCC diagnostic pop

            

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

// regsiters a client connection to the cerver's mains poll structure
// and maps the sock fd to the client
u8 cerver_poll_register_connection (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        pthread_mutex_lock (cerver->poll_lock);

        i32 idx = cerver_poll_get_free_idx (cerver);
        if (idx > 0) {
            cerver->fds[idx].fd = connection->socket->sock_fd;
            cerver->fds[idx].events = POLLIN;
            cerver->current_n_fds++;

            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Added new sock fd to cerver %s main poll, idx: %i", 
                cerver->info->name->str, idx);
            if (s) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_NO_TYPE, s);
                free (s);
            }
            #endif

            retval = 0;
        }

        else if (idx < 0) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Cerver %s main poll is full -- we need to realloc...", 
                cerver->info->name->str);
            if (s) {
                cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, s);
                free (s);
            }
            #endif
            if (cerver_realloc_main_poll_fds (cerver)) {
                char *s = c_string_create ("Failed to realloc cerver %s main poll fds!", 
                    cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, s);
                    free (s);
                }
            }

            // try again to register the connection
            else {
                retval = cerver_poll_register_connection (cerver, client, connection);
            }
        }

        pthread_mutex_unlock (cerver->poll_lock);
    }

    return retval;

}

// unregsiters a client connection from the cerver's main poll structure
// and removes the map from the socket to the client
u8 cerver_poll_unregister_connection (Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    if (cerver && client && connection) {
        pthread_mutex_lock (cerver->poll_lock);

        // get the idx of the connection sock fd in the cerver poll fds
        i32 idx = cerver_poll_get_idx_by_sock_fd (cerver, connection->socket->sock_fd);
        if (idx > 0) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Removed sock fd from cerver %s main poll, idx: %d",
                cerver->info->name->str, idx);
            if (s) {
                cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, s);
                free (s);
            }
            #endif

            cerver->fds[idx].fd = -1;
            cerver->fds[idx].events = -1;
            cerver->current_n_fds--;

            retval = 0;     // removed the sock fd form the cerver poll
        }

        pthread_mutex_unlock (cerver->poll_lock);
    }

    return retval;

}

// cerver poll loop to handle events in the registered socket's fds
u8 cerver_poll (Cerver *cerver) {

    u8 retval = 1;

    if (cerver) {
        char *s = c_string_create ("Cerver %s main handler has started!", cerver->info->name->str);
        if (s) {
            cerver_log_msg (stdout, LOG_SUCCESS, LOG_CERVER, s);
            free (s);
        }
        #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, LOG_DEBUG, LOG_CERVER, "Waiting for connections...");
        #endif

        int poll_retval = 0;
        while (cerver->isRunning) {
            pthread_mutex_lock (cerver->poll_lock);
            poll_retval = poll (cerver->fds, cerver->max_n_fds, cerver->poll_timeout);
            pthread_mutex_unlock (cerver->poll_lock);

            // poll failed
            if (poll_retval < 0) {
                char *s = c_string_create ("Cerver %s main poll has failed!", cerver->info->name->str);
                if (s) {
                    cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, s);
                    free (s);
                }

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
                    Socket *socket = socket_get_by_fd (cerver, cerver->fds[i].fd, false);
                    CerverReceive *cr = cerver_receive_new (cerver, socket, false, NULL);

                    switch (cerver->fds[i].revents) {
                        // A connection setup has been completed or new data arrived
                        case POLLIN: {
                            // accept incoming connections that are queued
                            if (i == 0) {
                                // if (thpool_add_work (cerver->thpool, cerver_accept, cerver))  {
                                //     cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
                                //         c_string_create ("Failed to add cerver_accept () to cerver's %s thpool!", 
                                //         cerver->info->name->str));
                                // }

                                cerver_accept (cerver);
                            }

                            // not the cerver socket, so a connection fd must be readable
                            else {
                                // printf ("Receive fd: %d\n", cerver->fds[i].fd);
                                
                                if (cerver->thpool) {
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
                                    cerver_receive (cr);

                                    // pthread_mutex_unlock (socket->mutex);
                                }

                                else {
                                    // handle all received packets in the same thread
                                    cerver_receive (cr);
                                }
                            } 
                        } break;

                        // A disconnection request has been initiated by the other end
                        // or a connection is broken (SIGPIPE is also set when we try to write to it)
                        // or The other end has shut down one direction
                        case POLLHUP: {
                            cerver_switch_receive_handle_failed (cr);
                        } break;

                        // An asynchronous error occurred
                        case POLLERR: {
                            cerver_switch_receive_handle_failed (cr);
                        } break;

                        // Urgent data arrived. SIGURG is sent then.
                        case POLLPRI: {
                            /*** TODO: ***/
                        } break;

                        default: break;
                    }
                }
            }
        }

        #ifdef CERVER_DEBUG
        s = c_string_create ("Cerver %s main poll has stopped!", cerver->info->name->str);
        if (s) {
            cerver_log_msg (stdout, LOG_CERVER, LOG_NO_TYPE, s);
            free (s);
        }
        #endif

        retval = 0;
    }

    else {
        cerver_log_msg (stderr, LOG_ERROR, LOG_CERVER, 
            "Can't listen for connections on a NULL cerver!");
    }

    return retval;

}

#pragma endregion