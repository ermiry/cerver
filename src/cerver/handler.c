#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <fcntl.h>

#include <sys/prctl.h>

#include "cerver/types/types.h"

#include "cerver/collections/htab.h"

#include "cerver/auth.h"
#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/events.h"
#include "cerver/files.h"
#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/socket.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/jobs.h"

#include "cerver/http/http.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

#pragma region handler

static int unique_handler_id = 0;

static HandlerData *handler_data_new (void) {

	HandlerData *handler_data = (HandlerData *) malloc (sizeof (HandlerData));
	if (handler_data) {
		handler_data->handler_id = 0;

		handler_data->data = NULL;
		handler_data->packet = NULL;
	}

	return handler_data;

}

static void handler_data_delete (HandlerData *handler_data) {

	if (handler_data) free (handler_data);

}

static Handler *handler_new (void) {

	Handler *handler = (Handler *) malloc (sizeof (Handler));
	if (handler) {
		handler->type = HANDLER_TYPE_NONE;
		handler->unique_id = -1;

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
		handler->client = NULL;
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
		handler->unique_id = unique_handler_id;
		unique_handler_id += 1;

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

// while cerver is running, check for new jobs and handle them
static void handler_do_while_cerver (Handler *handler) {

	if (handler) {
		Job *job = NULL;
		Packet *packet = NULL;
		PacketType packet_type = PACKET_TYPE_NONE;
		HandlerData *handler_data = handler_data_new ();
		while (handler->cerver->isRunning) {
			bsem_wait (handler->job_queue->has_jobs);

			if (handler->cerver->isRunning) {
				pthread_mutex_lock (handler->cerver->handlers_lock);
				handler->cerver->num_handlers_working += 1;
				pthread_mutex_unlock (handler->cerver->handlers_lock);

				// read job from queue
				job = job_queue_pull (handler->job_queue);
				if (job) {
					packet = (Packet *) job->args;
					packet_type = packet->header->packet_type;

					handler_data->handler_id = handler->id;
					handler_data->data = handler->data;
					handler_data->packet = packet;

					handler->handler (handler_data);

					job_delete (job);

					switch (packet_type) {
						case PACKET_TYPE_APP: if (handler->cerver->app_packet_handler_delete_packet) packet_delete (packet); break;
						case PACKET_TYPE_APP_ERROR: if (handler->cerver->app_error_packet_handler_delete_packet) packet_delete (packet); break;
						case PACKET_TYPE_CUSTOM: if (handler->cerver->custom_packet_handler_delete_packet) packet_delete (packet); break;

						default: packet_delete (packet); break;
					}
				}

				pthread_mutex_lock (handler->cerver->handlers_lock);
				handler->cerver->num_handlers_working -= 1;
				pthread_mutex_unlock (handler->cerver->handlers_lock);
			}
		}

		handler_data_delete (handler_data);
	}

}

// while client is running, check for new jobs and handle them
static void handler_do_while_client (Handler *handler) {

	if (handler) {
		Job *job = NULL;
		Packet *packet = NULL;
		HandlerData *handler_data = handler_data_new ();
		while (handler->client->running) {
			bsem_wait (handler->job_queue->has_jobs);

			if (handler->client->running) {
				pthread_mutex_lock (handler->client->handlers_lock);
				handler->client->num_handlers_working += 1;
				pthread_mutex_unlock (handler->client->handlers_lock);

				// read job from queue
				job = job_queue_pull (handler->job_queue);
				if (job) {
					packet = (Packet *) job->args;

					handler_data->handler_id = handler->id;
					handler_data->data = handler->data;
					handler_data->packet = packet;

					handler->handler (handler_data);

					job_delete (job);
					packet_delete (packet);
				}

				pthread_mutex_lock (handler->client->handlers_lock);
				handler->client->num_handlers_working -= 1;
				pthread_mutex_unlock (handler->client->handlers_lock);
			}
		}

		handler_data_delete (handler_data);
	}

}

// while cerver is running, check for new jobs and handle them
static void handler_do_while_admin (Handler *handler) {

	if (handler) {
		Job *job = NULL;
		Packet *packet = NULL;
		PacketType packet_type = PACKET_TYPE_NONE;
		HandlerData *handler_data = handler_data_new ();
		while (handler->cerver->isRunning) {
			bsem_wait (handler->job_queue->has_jobs);

			if (handler->cerver->isRunning) {
				pthread_mutex_lock (handler->cerver->admin->handlers_lock);
				handler->cerver->admin->num_handlers_working += 1;
				pthread_mutex_unlock (handler->cerver->admin->handlers_lock);

				// read job from queue
				job = job_queue_pull (handler->job_queue);
				if (job) {
					packet = (Packet *) job->args;
					packet_type = packet->header->packet_type;

					handler_data->handler_id = handler->id;
					handler_data->data = handler->data;
					handler_data->packet = packet;

					handler->handler (handler_data);

					job_delete (job);

					switch (packet_type) {
						case PACKET_TYPE_APP: if (handler->cerver->admin->app_packet_handler_delete_packet) packet_delete (packet); break;
						case PACKET_TYPE_APP_ERROR: if (handler->cerver->admin->app_error_packet_handler_delete_packet) packet_delete (packet); break;
						case PACKET_TYPE_CUSTOM: if (handler->cerver->admin->custom_packet_handler_delete_packet) packet_delete (packet); break;

						default: packet_delete (packet); break;
					}
				}

				pthread_mutex_lock (handler->cerver->admin->handlers_lock);
				handler->cerver->admin->num_handlers_working -= 1;
				pthread_mutex_unlock (handler->cerver->admin->handlers_lock);
			}
		}

		handler_data_delete (handler_data);
	}

}

static void *handler_do (void *handler_ptr) {

	if (handler_ptr) {
		Handler *handler = (Handler *) handler_ptr;

		pthread_mutex_t *handlers_lock = NULL;
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handlers_lock = handler->cerver->handlers_lock; break;
			case HANDLER_TYPE_CLIENT: handlers_lock = handler->client->handlers_lock; break;
			case HANDLER_TYPE_ADMIN: handlers_lock = handler->cerver->admin->handlers_lock; break;
			default: break;
		}

		// set the thread name
		if (handler->id >= 0) {
			char thread_name[128] = { 0 };

			switch (handler->type) {
				case HANDLER_TYPE_CERVER: snprintf (thread_name, 128, "cerver-handler-%d", handler->unique_id); break;
				case HANDLER_TYPE_CLIENT: snprintf (thread_name, 128, "client-handler-%d", handler->unique_id); break;
				case HANDLER_TYPE_ADMIN: snprintf (thread_name, 128, "admin-handler-%d", handler->unique_id); break;
				default: break;
			}

			// printf ("%s\n", thread_name);
			prctl (PR_SET_NAME, thread_name);
		}

		// TODO: register to signals to handle multiple actions

		if (handler->data_create)
			handler->data = handler->data_create (handler->data_create_args);

		// mark the handler as alive and ready
		pthread_mutex_lock (handlers_lock);
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handler->cerver->num_handlers_alive += 1; break;
			case HANDLER_TYPE_CLIENT: handler->client->num_handlers_alive += 1; break;
			case HANDLER_TYPE_ADMIN: handler->cerver->admin->num_handlers_alive += 1; break;
			default: break;
		}
		pthread_mutex_unlock (handlers_lock);

		// while cerver / client is running, check for new jobs and handle them
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handler_do_while_cerver (handler); break;
			case HANDLER_TYPE_CLIENT: handler_do_while_client (handler); break;
			case HANDLER_TYPE_ADMIN: handler_do_while_admin (handler); break;
			default: break;
		}

		if (handler->data_delete)
			handler->data_delete (handler->data);

		pthread_mutex_lock (handlers_lock);
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handler->cerver->num_handlers_alive -= 1; break;
			case HANDLER_TYPE_CLIENT: handler->client->num_handlers_alive -= 1; break;
			case HANDLER_TYPE_ADMIN: handler->cerver->admin->num_handlers_alive -= 1; break;
			default: break;
		}
		pthread_mutex_unlock (handlers_lock);
	}

	return NULL;

}

// starts the new handler by creating a dedicated thread for it
// called by internal cerver methods
int handler_start (Handler *handler) {

	int retval = 1;

	if (handler) {
		if (handler->type != HANDLER_TYPE_NONE) {
			// retval = pthread_create (
			//     &handler->thread_id,
			//     NULL,
			//     (void *(*)(void *)) handler_do,
			//     (void *) handler
			// );


			// 26/05/2020 -- 12:54
			// handler's threads are not explicitly joined by pthread_join ()
			// on cerver teardown
			if (!thread_create_detachable (
				&handler->thread_id,
				(void *(*)(void *)) handler_do,
				(void *) handler
			)) {
				#ifdef HANDLER_DEBUG
				char *s = c_string_create ("Created handler %d thread!", handler->unique_id);
				if (s) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, s);
					free (s);
				}
				#endif

				retval = 0;
			}
		}

		else {
			char *s = c_string_create ("handler_start () - Handler %d is of invalid type!",
				handler->unique_id);
			if (s) {
				cerver_log_error (s);
				free (s);
			}
		}
	}

	return retval;

}

#pragma endregion

#pragma region sock receive

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

#pragma endregion

#pragma region handlers

static void cerver_client_packet_handler_by_header (
	const PacketHeader *header,
	Cerver *cerver, Client *client, Connection *connection, Lobby *lobby
) {

	switch (header->request_type) {
		// the client is going to close its current connection
		// but will remain in the cerver if it has another connection active
		// if not, it will be dropped
		case CLIENT_PACKET_TYPE_CLOSE_CONNECTION: {
			#ifdef CERVER_DEBUG
			char *s = c_string_create ("Client %ld requested to close the connection", client->id);
			if (s) {
				cerver_log_debug (s);
				free (s);
			}
			#endif

			// check if the client is inside a lobby
			if (lobby) {
				#ifdef CERVER_DEBUG
				char *s = c_string_create ("Client %ld inside lobby %s wants to close the connection...",
					client->id, lobby->id->str);
				if (s) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_GAME, s);
					free (s);
				}
				#endif

				// remove the player from the lobby
				Player *player = player_get_by_sock_fd_list (lobby, connection->socket->sock_fd);
				player_unregister_from_lobby (lobby, player);
			}

			client_remove_connection_by_sock_fd (cerver, client, connection->socket->sock_fd);
		} break;

		// the client is going to disconnect and will close all of its active connections
		// so drop it from the server
		case CLIENT_PACKET_TYPE_DISCONNECT: {
			// check if the client is inside a lobby
			if (lobby) {
				#ifdef CERVER_DEBUG
				char *s = c_string_create ("Client %ld inside lobby %s wants to close the connection...",
					client->id, lobby->id->str);
				if (s) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_GAME, s);
					free (s);
				}
				#endif

				// remove the player from the lobby
				Player *player = player_get_by_sock_fd_list (lobby, connection->socket->sock_fd);
				player_unregister_from_lobby (lobby, player);
			}

			client_drop (cerver, client);

			cerver_event_trigger (
				CERVER_EVENT_CLIENT_DISCONNECTED,
				cerver,
				NULL, NULL
			);
		} break;

		default: {
			#ifdef HANDLER_DEBUG
			char *s = c_string_create ("Got an unknown client packet in cerver %s", cerver->info->name->str);
			if (s) {
				cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_HANDLER, s);
				free (s);
			}
			#endif
		} break;
	}

}

// handles a packet of type PACKET_TYPE_CLIENT
static void cerver_client_packet_handler (Packet *packet) {

	cerver_client_packet_handler_by_header (
		packet->header,
		packet->cerver, packet->client, packet->connection, packet->lobby
	);

}

 // handles a request from a client to get a file
static void cerver_request_get_file (Packet *packet) {

	// TODO:

}

// handles a request from a client to upload a file
static void cerver_request_send_file (Packet *packet) {

	// TODO:

}

// handles a request made from the client
static void cerver_request_packet_handler (Packet *packet) {

	if (packet->header) {
		switch (packet->header->request_type) {
			// request from a client to get a file
			case REQUEST_PACKET_TYPE_GET_FILE: cerver_request_get_file (packet); break;

			// request from a client to upload a file
			case REQUEST_PACKET_TYPE_SEND_FILE: cerver_request_send_file (packet); break;

			default: {
				#ifdef HANDLER_DEBUG
				char *s = c_string_create ("Got an unknown request packet in cerver %s",
					packet->cerver->info->name->str);
				if (s) {
					cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_HANDLER, s);
					free (s);
				}
				#endif
			} break;
		}
	}

}

// sends back a test packet to the client!
void cerver_test_packet_handler (Packet *packet) {

	#ifdef HANDLER_DEBUG
	char *s = c_string_create ("Got a test packet in cerver %s.", packet->cerver->info->name->str);
	if (s) {
		cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_PACKET, s);
		free (s);
	}
	#endif

	Packet *test_packet = packet_new ();
	if (test_packet) {
		packet_set_network_values (test_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
		test_packet->packet_type = PACKET_TYPE_TEST;
		packet_generate (test_packet);
		if (packet_send (test_packet, 0, NULL, false)) {
			char *s = c_string_create ("Failed to send error packet from cerver %s.",
				packet->cerver->info->name->str);
			if (s) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_PACKET, s);
				free (s);
			}
		}

		packet_delete (test_packet);
	}

}

// 27/01/2020
// handles an PACKET_TYPE_APP packet type
static void cerver_app_packet_handler (Packet *packet) {

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
				if (packet->cerver->app_packet_handler_delete_packet) packet_delete (packet);
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

// 27/05/2020
// handles an PACKET_TYPE_APP_ERROR packet type
static void cerver_app_error_packet_handler (Packet *packet) {

	if (packet->cerver->app_error_packet_handler) {
		if (packet->cerver->app_error_packet_handler->direct_handle) {
			// printf ("app_error_packet_handler - direct handle!\n");
			packet->cerver->app_error_packet_handler->handler (packet);
			if (packet->cerver->app_error_packet_handler_delete_packet) packet_delete (packet);
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

// 27/05/2020
// handles a PACKET_TYPE_CUSTOM packet type
static void cerver_custom_packet_handler (Packet *packet) {

	if (packet->cerver->custom_packet_handler) {
		if (packet->cerver->custom_packet_handler->direct_handle) {
			// printf ("custom_packet_handler - direct handle!\n");
			packet->cerver->custom_packet_handler->handler (packet);
			if (packet->cerver->custom_packet_handler_delete_packet) packet_delete (packet);
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

// handle packet based on type
static void cerver_packet_handler (void *packet_ptr) {

	if (packet_ptr) {
		Packet *packet = (Packet *) packet_ptr;

		packet->cerver->stats->client_n_packets_received += 1;
		packet->cerver->stats->total_n_packets_received += 1;
		if (packet->lobby) packet->lobby->stats->n_packets_received += 1;

		bool good = true;
		if (packet->cerver->check_packets) {
			// we expect the packet version in the packet's data
			if (packet->data) {
				packet->version = (PacketVersion *) packet->data_ptr;
				packet->data_ptr += sizeof (PacketVersion);
				good = packet_check (packet);
			}

			else {
				cerver_log_error ("cerver_packet_handler () - No packet version to check!");
				good = false;
			}
		}

		if (good) {
			switch (packet->header->packet_type) {
				case PACKET_TYPE_NONE: break;

				case PACKET_TYPE_CERVER: break;

				case PACKET_TYPE_CLIENT:
					packet->cerver->stats->received_packets->n_client_packets += 1;
					packet->client->stats->received_packets->n_client_packets += 1;
					packet->connection->stats->received_packets->n_client_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_client_packets += 1;
					cerver_client_packet_handler (packet);
					packet_delete (packet);
					break;

				// handles an error from the client
				case PACKET_TYPE_ERROR:
					packet->cerver->stats->received_packets->n_error_packets += 1;
					packet->client->stats->received_packets->n_error_packets += 1;
					packet->connection->stats->received_packets->n_error_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_error_packets += 1;
					/* TODO: */
					packet_delete (packet);
					break;

				// handles a request made from the client
				case PACKET_TYPE_REQUEST:
					packet->cerver->stats->received_packets->n_request_packets += 1;
					packet->client->stats->received_packets->n_request_packets += 1;
					packet->connection->stats->received_packets->n_request_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_request_packets += 1;
					cerver_request_packet_handler (packet);
					packet_delete (packet);
					break;

				// handles authentication packets
				case PACKET_TYPE_AUTH:
					packet->cerver->stats->received_packets->n_auth_packets += 1;
					packet->client->stats->received_packets->n_auth_packets += 1;
					packet->connection->stats->received_packets->n_auth_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_auth_packets += 1;
					/* TODO: */
					packet_delete (packet);
					break;

				// handles a game packet sent from the client
				case PACKET_TYPE_GAME:
					packet->cerver->stats->received_packets->n_game_packets += 1;
					packet->client->stats->received_packets->n_game_packets += 1;
					packet->connection->stats->received_packets->n_game_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_game_packets += 1;
					game_packet_handler (packet);
					break;

				// user set handler to handle app specific packets
				case PACKET_TYPE_APP:
					packet->cerver->stats->received_packets->n_app_packets += 1;
					packet->client->stats->received_packets->n_app_packets += 1;
					packet->connection->stats->received_packets->n_app_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_app_packets += 1;
					cerver_app_packet_handler (packet);
					break;

				// user set handler to handle app specific errors
				case PACKET_TYPE_APP_ERROR:
					packet->cerver->stats->received_packets->n_app_error_packets += 1;
					packet->client->stats->received_packets->n_app_error_packets += 1;
					packet->connection->stats->received_packets->n_app_error_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_app_error_packets += 1;
					cerver_app_error_packet_handler (packet);
					break;

				// custom packet hanlder
				case PACKET_TYPE_CUSTOM:
					packet->cerver->stats->received_packets->n_custom_packets += 1;
					packet->client->stats->received_packets->n_custom_packets += 1;
					packet->connection->stats->received_packets->n_custom_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_custom_packets += 1;
					cerver_custom_packet_handler (packet);
					break;

				// acknowledge the client we have received his test packet
				case PACKET_TYPE_TEST:
					packet->cerver->stats->received_packets->n_test_packets += 1;
					packet->client->stats->received_packets->n_test_packets += 1;
					packet->connection->stats->received_packets->n_test_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_test_packets += 1;
					cerver_test_packet_handler (packet);
					packet_delete (packet);
					break;

				default: {
					packet->cerver->stats->received_packets->n_bad_packets += 1;
					packet->client->stats->received_packets->n_bad_packets += 1;
					packet->connection->stats->received_packets->n_bad_packets += 1;
					if (packet->lobby) packet->lobby->stats->received_packets->n_bad_packets += 1;
					#ifdef HANDLER_DEBUG
					char *s = c_string_create ("Got a packet of unknown type in cerver %s.",
						packet->cerver->info->name->str);
					if (s) {
						cerver_log_msg (stdout, LOG_TYPE_WARNING, LOG_TYPE_PACKET, s);
						free (s);
					}
					#endif
					packet_delete (packet);
				} break;
			}
		}
	}

}

static void cerver_packet_select_handler (ReceiveHandle *receive_handle, Packet *packet) {

	switch (receive_handle->type) {
		case RECEIVE_TYPE_NONE: break;

		case RECEIVE_TYPE_NORMAL: {
			packet->cerver = receive_handle->cerver;
			packet->client = receive_handle->client;
			packet->connection = receive_handle->connection;

			packet->cerver->stats->client_n_packets_received += 1;
			packet->client->stats->n_packets_received += 1;
			packet->connection->stats->n_packets_received += 1;

			cerver_packet_handler (packet);
		} break;

		case RECEIVE_TYPE_ON_HOLD: {
			packet->cerver = receive_handle->cerver;
			packet->connection = receive_handle->connection;

			packet->cerver->stats->on_hold_n_packets_received += 1;
			packet->connection->stats->n_packets_received += 1;

			on_hold_packet_handler (packet);
		} break;

		case RECEIVE_TYPE_ADMIN: {
			packet->cerver = receive_handle->cerver;
			packet->connection = receive_handle->connection;
			packet->client = receive_handle->admin->client;

			packet->cerver->admin->stats->total_n_packets_received += 1;

			receive_handle->admin->client->stats->n_packets_received += 1;

			packet->connection->stats->n_packets_received += 1;

			admin_packet_handler (packet);
		} break;

		default: break;
	}

}

#pragma endregion

#pragma region receive

u8 cerver_poll_unregister_sock_fd (Cerver *cerver, const i32 sock_fd);

static ReceiveHandle *receive_handle_new (void) {

	ReceiveHandle *receive_handle = (ReceiveHandle *) malloc (sizeof (ReceiveHandle));
	if (receive_handle) {
		receive_handle->type = RECEIVE_TYPE_NONE;

		receive_handle->cerver = NULL;

		receive_handle->socket = NULL;
		receive_handle->connection = NULL;
		receive_handle->client = NULL;
		receive_handle->admin = NULL;

		receive_handle->lobby = NULL;

		receive_handle->buffer = NULL;
		receive_handle->buffer_size = 0;
	}

	return receive_handle;

}

void receive_handle_delete (void *receive_ptr) { if (receive_ptr) free (receive_ptr); }

CerverReceive *cerver_receive_new (void) {

	CerverReceive *cr = (CerverReceive *) malloc (sizeof (CerverReceive));
	if (cr) {
		cr->type = RECEIVE_TYPE_NONE;

		cr->cerver = NULL;

		cr->socket = NULL;
		cr->connection = NULL;
		cr->client = NULL;
		cr->admin = NULL;

		cr->lobby = NULL;
	}

	return cr;

}

void cerver_receive_delete (void *ptr) { if (ptr) free (ptr); }

static inline void cerver_receive_create_normal (CerverReceive *cr, Cerver *cerver, const i32 sock_fd) {

	if (cr) {
		cr->client = client_get_by_sock_fd (cerver, sock_fd);
		if (cr->client) {
			cr->connection = connection_get_by_sock_fd_from_client (cr->client, sock_fd);
			if (cr->connection) {
				cr->socket = cr->connection->socket;
			}
		}

		// for what ever reason we have a rogue connection
		else {
			// #ifdef CERVER_DEBUG
			char *s = c_string_create (
				"cerver_receive_create () - RECEIVE_TYPE_NORMAL - no client with sock fd <%d>",
				sock_fd
			);
			if (s) {
				cerver_log_error (s);
				free (s);
			}
			// #endif

			// remove the sock fd from the cerver's main poll array
			cerver_poll_unregister_sock_fd (cerver, sock_fd);

			// try to remove the sock fd from the cerver's map
			const void *key = &sock_fd;
			htab_remove (cerver->client_sock_fd_map, key, sizeof (i32));

			close (sock_fd);        // just close the socket
		}
	}

}

static inline void cerver_receive_create_on_hold (CerverReceive *cr, Cerver *cerver, const i32 sock_fd) {

	if (cr) {
		cr->connection = connection_get_by_sock_fd_from_on_hold (cerver, sock_fd);
		if (cr->connection) {
			cr->socket = cr->connection->socket;
		}

		// for what ever reason we have a rogue connection
		else {
			// #ifdef CERVER_DEBUG
			char *s = c_string_create (
				"cerver_receive_create () - RECEIVE_TYPE_ON_HOLD - no connection with sock fd <%d>",
				sock_fd
			);
			if (s) {
				cerver_log_error (s);
				free (s);
			}
			// #endif

			// remove the sock fd from the cerver's on hold poll array
			on_hold_poll_unregister_sock_fd (cerver, sock_fd);

			close (sock_fd);        // just close the socket
		}
	}

}

static inline void cerver_receive_create_admin (CerverReceive *cr, Cerver *cerver, const i32 sock_fd) {

	if (cr) {
		cr->admin = admin_get_by_sock_fd (cerver->admin, sock_fd);
		if (cr->admin) {
			cr->client = cr->admin->client;
			cr->connection = connection_get_by_sock_fd_from_client (cr->client, sock_fd);
			if (cr->connection) {
				cr->socket = cr->connection->socket;
			}
		}

		// for what ever reason we have a rogue connection
		else {
			// #ifdef ADMIN_DEBUG
			char *s = c_string_create (
				"cerver_receive_create () - RECEIVE_TYPE_ADMIN - no admin with sock fd <%d>",
				sock_fd
			);
			if (s) {
				cerver_log_error (s);
				free (s);
			}
			// #endif

			// remove the sock fd from the cerver's admin poll array
			admin_cerver_poll_unregister_sock_fd (cerver->admin, sock_fd);

			close (sock_fd);        // just close the socket
		}
	}

}

CerverReceive *cerver_receive_create (ReceiveType receive_type, Cerver *cerver, const i32 sock_fd) {

	CerverReceive *cr = cerver_receive_new ();
	if (cr) {
		cr->type = receive_type;

		cr->cerver = cerver;

		switch (cr->type) {
			case RECEIVE_TYPE_NONE: break;

			case RECEIVE_TYPE_NORMAL: cerver_receive_create_normal (cr, cerver, sock_fd); break;

			case RECEIVE_TYPE_ON_HOLD: cerver_receive_create_on_hold (cr, cerver, sock_fd); break;

			case RECEIVE_TYPE_ADMIN: cerver_receive_create_admin (cr, cerver, sock_fd); break;

			default: break;
		}
	}

	return cr;

}

CerverReceive *cerver_receive_create_full (ReceiveType receive_type,
	Cerver *cerver,
	Client *client, Connection *connection
) {

	CerverReceive *cr = cerver_receive_new ();
	if (cr) {
		cr->type = receive_type;

		cr->cerver = cerver;

		cr->socket = connection ? connection->socket : NULL;
		cr->connection = connection;
		cr->client = client;
	}

	return cr;

}

static void cerver_receive_handle_spare_packet (ReceiveHandle *receive_handle,
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
					cerver_packet_select_handler (receive_handle, sock_receive->spare_packet);
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
void cerver_receive_handle_buffer (void *receive_handle_ptr) {

	if (receive_handle_ptr) {
		// printf ("cerver_receive_handle_buffer ()\n");
		ReceiveHandle *receive_handle = (ReceiveHandle *) receive_handle_ptr;

		Cerver *cerver = receive_handle->cerver;
		// i32 sock_fd = receive_handle->sock_fd;
		char *buffer = receive_handle->buffer;
		size_t buffer_size = receive_handle->buffer_size;
		i32 sock_fd = receive_handle->socket->sock_fd;
		// char *buffer = receive_handle->socket->packet_buffer;
		// size_t buffer_size = receive_handle->socket->packet_buffer_size;
		Lobby *lobby = receive_handle->lobby;

		pthread_mutex_lock (receive_handle->socket->read_mutex);

		SockReceive *sock_receive = receive_handle->connection ? receive_handle->connection->sock_receive : NULL;
		if (sock_receive) {
			if (buffer && (buffer_size > 0)) {
				char *end = buffer;
				size_t buffer_pos = 0;

				cerver_receive_handle_spare_packet (
					receive_handle,
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
									cerver_packet_select_handler (receive_handle, packet);
								}

							}

							else {
								cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_PACKET,
									"Failed to create a new packet in cerver_handle_receive_buffer ()");
							}
						}

						else {
							char *status = c_string_create ("Got a packet of invalid size: %ld", packet_size);
							if (status) {
								cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_PACKET, status);
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

					header = NULL;
				}
			}
		}

		else {
			#ifdef CERVER_DEBUG
			char *status = c_string_create ("Sock fd: %d does not have an associated sock_receive in cerver %s.",
				sock_fd, cerver->info->name->str);
			if (status) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, status);
				free (status);
			}
			#endif
		}

		// 28/05/2020 -- deleting the created buffer from cerver_receive ()
		// to correct handle both cases: using thpool and single threaded
		if (buffer) free (receive_handle->buffer);

		// free (receive->socket->packet_buffer);
		// receive->socket->packet_buffer = NULL;

		pthread_mutex_unlock (receive_handle->socket->read_mutex);

		receive_handle_delete (receive_handle);
	}

}

// handles a failed recive from a connection associatd with a client
// end sthe connection to prevent seg faults or signals for bad sock fd
static void cerver_receive_handle_failed (void *cr_ptr) {

	if (cr_ptr) {
		CerverReceive *cr = (CerverReceive *) cr_ptr;

		if (cr->socket) {
			pthread_mutex_lock (cr->socket->read_mutex);

			if (cr->socket->sock_fd > 0) {
				switch (cr->type) {
					case RECEIVE_TYPE_NORMAL: {
						// check if the socket belongs to a player inside a lobby
						if (cr->lobby) {
							if (cr->lobby->players->size > 0) {
								Player *player = player_get_by_sock_fd_list (cr->lobby, cr->socket->sock_fd);
								if (player) player_unregister_from_lobby (cr->lobby, player);
							}
						}

						client_remove_connection_by_sock_fd (cr->cerver, cr->client, cr->socket->sock_fd);
					} break;

					case RECEIVE_TYPE_ON_HOLD: {
						on_hold_connection_drop (cr->cerver, cr->connection);
					} break;

					case RECEIVE_TYPE_ADMIN: {
						admin_remove_connection_by_sock_fd (cr->cerver->admin, cr->admin, cr->socket->sock_fd);
					} break;

					default: break;
				}
			}

			pthread_mutex_unlock (cr->socket->read_mutex);
		}

		cerver_receive_delete (cr);
	}

}

// 28/05/2020 -- correctly call cerver_receive_handle_failed ()
void cerver_switch_receive_handle_failed (CerverReceive *cr) {

	if (cr) {
		if (cr->cerver->thpool) {
			if (thpool_add_work (cr->cerver->thpool, cerver_receive_handle_failed, cr)) {
				char *s = c_string_create (
					"Failed to add cerver_receive_handle_failed () to cerver's %s thpool!",
					cr->cerver->info->name->str
				);
				if (s) {
					cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_NONE, s);
					free (s);
				}
			}
		}

		else {
			cerver_receive_handle_failed (cr);
		}
	}

}

static inline void cerver_receive_success_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer) {

	ReceiveHandle *receive_handle = receive_handle_new ();
	if (receive_handle) {
		receive_handle->type = cr->type;

		receive_handle->cerver = cr->cerver;

		receive_handle->socket = cr->socket;
		receive_handle->connection = cr->connection;
		receive_handle->client = cr->client;
		receive_handle->admin = cr->admin;

		receive_handle->lobby = cr->lobby;

		receive_handle->buffer = packet_buffer;
		receive_handle->buffer_size = rc;

		switch (receive_handle->cerver->handler_type) {
			case CERVER_HANDLER_TYPE_NONE: break;

			case CERVER_HANDLER_TYPE_POLL: {
				if (cr->cerver->thpool) {
					// 28/05/2020 -- 02:37 -- added thpool here instead of cerver_poll ()
					// and it seems to be working as expected
					if (!thpool_add_work (cr->cerver->thpool, cr->cerver->handle_received_buffer, receive_handle)) {
						// char *s = c_string_create ("Added %s cr->cerver->handle_received_buffer () to thpool!",
						//     cr->cerver->info->name->str);
						// if (s) {
						//     cerver_log_debug (s);
						//     free (s);
						// }
					}

					else {
						char *s = c_string_create (
							"Failed to add %s cr->cerver->handle_received_buffer () to thpool!",
							cr->cerver->info->name->str
						);
						if (s) {
							cerver_log_error (s);
							free (s);
						}
					}

					// char *status = c_string_create ("Cerver %s active thpool threads: %d",
					//     cr->cerver->info->name->str,
					//     thpool_get_num_threads_working (cr->cerver->thpool)
					// );
					// if (status) {
					//     cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, status);
					//     free (status);
					// }
				}

				else {
					cr->cerver->handle_received_buffer (receive_handle);

					// 28/05/2020 -- called from inside cerver_receive_handle_buffer ()
					// receive_handle_delete (receive);
				}

				cerver_receive_delete (cr);
			} break;

			case CERVER_HANDLER_TYPE_THREADS:
				cr->cerver->handle_received_buffer (receive_handle);
				break;

			default: break;
		}
	}

}

static void cerver_receive_success_update_stats (CerverReceive *cr, ssize_t rc) {

	cr->socket->packet_buffer_size = rc;

	cr->cerver->stats->total_n_receives_done += 1;
	cr->cerver->stats->total_bytes_received += rc;

	switch (cr->cerver->type) {
		case CERVER_TYPE_WEB: break;

		default: {
			if (cr->lobby) {
				cr->lobby->stats->n_receives_done += 1;
				cr->lobby->stats->bytes_received += rc;
			}

			switch (cr->type) {
				case RECEIVE_TYPE_NORMAL: {
					cr->cerver->stats->client_receives_done += 1;
					cr->cerver->stats->client_bytes_received += rc;

					cr->client->stats->n_receives_done += 1;
					cr->client->stats->total_bytes_received += rc;

					cr->connection->stats->n_receives_done += 1;
					cr->connection->stats->total_bytes_received += rc;
				} break;

				case RECEIVE_TYPE_ON_HOLD: {
					cr->cerver->stats->on_hold_receives_done += 1;
					cr->cerver->stats->on_hold_bytes_received += rc;

					cr->connection->stats->n_receives_done += 1;
					cr->connection->stats->total_bytes_received += rc;
				} break;

				case RECEIVE_TYPE_ADMIN: {
					cr->cerver->admin->stats->total_n_receives_done += 1;
					cr->cerver->admin->stats->total_bytes_received += rc;

					cr->client->stats->n_receives_done += 1;
					cr->client->stats->total_bytes_received += rc;

					cr->connection->stats->n_receives_done += 1;
					cr->connection->stats->total_bytes_received += rc;
				} break;

				default: break;
			}
		} break;
	}

}

static void cerver_receive_success (CerverReceive *cr, ssize_t rc, char *packet_buffer) {

	// char *status = c_string_create ("Cerver %s rc: %ld for sock fd: %d",
	//     cr->cerver->info->name->str, rc, cr->socket->sock_fd);
	// if (status) {
	//     cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
	//     free (status);
	// }

	cerver_receive_success_update_stats (cr, rc);

	switch (cr->cerver->type) {
		case CERVER_TYPE_WEB:
			http_receive_handle (cr, rc, packet_buffer);
			break;

		default:
			cerver_receive_success_receive_handle (cr, rc, packet_buffer);
			break;
	}

}

// TODO: do we want to update other cerver stats?
// FIXME: discard buffer on bad types
static inline void balancer_receive_success (CerverReceive *cr, PacketHeader *header) {

	switch (header->packet_type) {
		case PACKET_TYPE_NONE: break;

		case PACKET_TYPE_CLIENT:
			cr->cerver->stats->received_packets->n_client_packets += 1;
			cr->client->stats->received_packets->n_client_packets += 1;
			cr->connection->stats->received_packets->n_client_packets += 1;
			cerver_client_packet_handler_by_header (
				header,
				cr->cerver, cr->client, cr->connection, cr->lobby
			);
			break;

		case PACKET_TYPE_ERROR: break;

		case PACKET_TYPE_AUTH: break;

		// only route packets of these types to services
		case PACKET_TYPE_CERVER:
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
		case PACKET_TYPE_APP:
		case PACKET_TYPE_APP_ERROR:
		case PACKET_TYPE_CUSTOM:
		case PACKET_TYPE_TEST: {
			// TODO: select the correct service
			Connection *service = cr->cerver->balancer->services[0];

			header->sock_fd = cr->connection->socket->sock_fd;

			// send the header to the selected service
			send (service->socket->sock_fd, header, sizeof (PacketHeader), 0);

			// splice remaining packet to service
			size_t left = header->packet_size - sizeof (PacketHeader);
			if (left) {
				splice (cr->socket->sock_fd, NULL, service->socket->sock_fd, NULL, left, 0);
			}
		} break;

		default: {
			cr->cerver->stats->received_packets->n_bad_packets += 1;
			cr->client->stats->received_packets->n_bad_packets += 1;
			cr->connection->stats->received_packets->n_bad_packets += 1;
			if (cr->lobby) cr->lobby->stats->received_packets->n_bad_packets += 1;
			#ifdef HANDLER_DEBUG
			char *s = c_string_create ("balancer_receive () - packet of unknown type in cerver %s.",
				cr->cerver->info->name->str);
			if (s) {
				cerver_log_msg (stdout, LOG_TYPE_WARNING, LOG_TYPE_PACKET, s);
				free (s);
			}
			#endif
		} break;
	}

	// FIXME: update stats based on action
	// cerver_receive_success_update_stats (cr, rc);

}

static void balancer_receive (void *cerver_receive_ptr) {

	if (cerver_receive_ptr) {
		CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

		if (cr->cerver && cr->socket) {
			if (cr->socket > 0) {
				char header_buffer[sizeof (PacketHeader)] = { 0 };
				ssize_t rc = recv (cr->socket->sock_fd, header_buffer, sizeof (PacketHeader), MSG_DONTWAIT);

				switch (rc) {
					case -1: {
						// no more data to read
						if (errno != EWOULDBLOCK) {
							// #ifdef HANDLER_DEBUG
							char *s = c_string_create (
								"balancer_receive () - rc < 0 - sock fd: %d",
								cr->socket->sock_fd
							);

							if (s) {
								cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, s);
								free (s);
							}

							perror ("Error");
							// #endif

							cerver_switch_receive_handle_failed (cr);
						}
					} break;

					case 0: {
						#ifdef HANDLER_DEBUG
						char *s = c_string_create (
							"balancer_receive () - rc == 0 - sock fd: %d",
							cr->socket->sock_fd
						);

						if (s) {
							cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, s);
							free (s);
						}
						#endif

						cerver_switch_receive_handle_failed (cr);
					} break;

					default: {
						balancer_receive_success (cr, (PacketHeader *) header_buffer);
					} break;
				}
			}

			else {
				cerver_log_warning ("balancer_receive () - cr->socket <= 0");
				cerver_receive_delete (cr);
			}
		}

		else {
			cerver_receive_delete (cr);
		}
	}

}

// receive all incoming data from the socket
void cerver_receive (void *cerver_receive_ptr) {

	if (cerver_receive_ptr) {
		CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

		if (cr->cerver && cr->socket) {
			if (cr->socket > 0) {
				char *packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
				// cr->socket->packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
				if (packet_buffer) {
					// ssize_t rc = read (cr->sock_fd, packet_buffer, cr->cerver->receive_buffer_size);
					ssize_t rc = recv (cr->socket->sock_fd, packet_buffer, cr->cerver->receive_buffer_size, 0);

					switch (rc) {
						case -1: {
							// no more data to read
							if (errno != EWOULDBLOCK) {
								#ifdef HANDLER_DEBUG
								char *s = c_string_create (
									"cerver_receive () - rc < 0 - sock fd: %d",
									cr->socket->sock_fd
								);

								if (s) {
									cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, s);
									free (s);
								}

								perror ("Error");
								#endif

								cerver_switch_receive_handle_failed (cr);
							}

							free (packet_buffer);
						} break;

						case 0: {
							// man recv -> steam socket perfomed an orderly shutdown
							#ifdef HANDLER_DEBUG
							char *s = c_string_create (
								"cerver_recieve () - rc == 0 - sock fd: %d",
								cr->socket->sock_fd
							);

							if (s) {
								cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, s);
								free (s);
							}

							// perror ("Error ");
							#endif

							cerver_switch_receive_handle_failed (cr);

							free (packet_buffer);
						} break;

						default: {
							cerver_receive_success (cr, rc, packet_buffer);
						} break;
					}

					// 28/05/2020 -- 02:40
					// packet_buffer is not free from inside cr->cerver->handle_received_buffer ()
					// free (packet_buffer);
				}

				else {
					char *status = c_string_create (
						"cerver_receive () - Failed to allocate packet buffer for connection with sock fd <%d>!",
						cr->connection->socket->sock_fd
					);

					if (status) {
						cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_HANDLER, status);
						free (status);
					}
				}
			}

			else {
				cerver_log_warning ("cerver_receive () - cr->socket <= 0");
				cerver_receive_delete (cr);
			}
		}

		else {
			cerver_receive_delete (cr);
		}
	}

}

// packet buffer only gets deleted if cerver_receive_handle_buffer () is used
static inline u8 cerver_receive_threads_actual (CerverReceive *cr) {

	u8 retval = 1;

	#ifdef HANDLER_DEBUG
	char *s = NULL;
	#endif

	char *packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
	if (packet_buffer) {
		ssize_t rc = recv (cr->socket->sock_fd, packet_buffer, cr->cerver->receive_buffer_size, 0);

		switch (rc) {
			case -1: {
				if (cr->socket->sock_fd > -1) {
					switch (errno) {
						case EAGAIN: {
							#ifdef HANDLER_DEBUG
							s = c_string_create (
								"cerver_receive_threads () - sock fd: %d timed out",
								cr->socket->sock_fd
							);

							if (s) {
								cerver_log_debug (s);
								free (s);
							}
							#endif

							retval = 0;
						} break;

						default: {
							#ifdef HANDLER_DEBUG
							s = c_string_create (
								"cerver_receive_threads () - rc < 0 - sock fd: %d",
								cr->socket->sock_fd
							);

							if (s) {
								cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, s);
								free (s);
							}

							perror ("Error ");
							#endif
						} break;
					}
				}

				free (packet_buffer);
			} break;

			case 0: {
				#ifdef HANDLER_DEBUG
				s = c_string_create (
					"cerver_receive_threads () - rc == 0 - sock fd: %d",
					cr->socket->sock_fd
				);

				if (s) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, s);
					free (s);
				}

				// perror ("Error ");
				#endif

				free (packet_buffer);
			} break;

			default: {
				cerver_receive_success (cr, rc, packet_buffer);

				retval = 0;
			} break;
		}
	}

	else {
		char *status = c_string_create (
			"cerver_receive_threads () - Failed to allocate packet buffer for sock fd <%d> connection!",
			cr->connection->socket->sock_fd
		);

		if (status) {
			cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_HANDLER, status);
			free (status);
		}
	}

	return retval;

}

static void *cerver_receive_threads (void *cerver_receive_ptr) {

	CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

	// set the socket's timeout to prevent thread from getting stuck if no more data to read
	(void) sock_set_timeout (cr->connection->socket->sock_fd, DEFAULT_SOCKET_RECV_TIMEOUT);

	while (!cerver_receive_threads_actual (cr) && (cr->socket->sock_fd > 0) && cr->cerver->isRunning);

	// check if the connection has already ended
	if (cr->socket->sock_fd > 0) {
		client_remove_connection_by_sock_fd (cr->cerver, cr->client, cr->socket->sock_fd);
	}

	cerver_receive_delete (cr);

	#ifdef HANDLER_DEBUG
	cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, "cerver_receive_threads () - loop has ended");
	#endif

	return NULL;

}

// FIXME: 28/07/2020 - 14:08 - will handle the buffer each recv ()
// we expect only one request to be sent, so keep reading the socket until no more data is left,
// and then handle the complete buffer
static void *cerver_receive_http (void *cerver_receive_ptr) {

	CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

	i32 sock_fd = cr->socket->sock_fd;
	ssize_t rc = 0;
	do {
		char *packet_buffer = (char *) calloc (cr->cerver->receive_buffer_size, sizeof (char));
		if (packet_buffer) {
			rc = recv (cr->socket->sock_fd, packet_buffer, cr->cerver->receive_buffer_size, 0);

			switch (rc) {
				case -1: {
					if (cr->socket->sock_fd > -1) {
						#ifdef CERVER_DEBUG
						char *s = c_string_create (
							"cerver_receive_http () - rc < 0 - sock fd: %d",
							cr->socket->sock_fd
						);

						if (s) {
							cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, s);
							free (s);
						}

						perror ("Error ");
						#endif
					}
				} break;

				case 0: {
					#ifdef CERVER_DEBUG
					char *s = c_string_create (
						"cerver_receive_http () - rc == 0 - sock fd: %d",
						cr->socket->sock_fd
					);

					if (s) {
						cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, s);
						free (s);
					}

					// perror ("Error ");
					#endif
				} break;

				default: {
					cerver_receive_success (cr, rc, packet_buffer);
				} break;
			}

			free (packet_buffer);
		}

		else {
			char *status = c_string_create (
				"cerver_receive_http () - Failed to allocate packet buffer for connection with sock fd <%d>!",
				cr->connection->socket->sock_fd
			);

			if (status) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_HANDLER, status);
				free (status);
			}
		}
	} while (rc > 0);

	char *status = c_string_create (
		"cerver_receive_http () - loop has ended - dropping sock fd <%d> connection...",
		sock_fd
	);

	if (status) {
		cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, status);
		free (status);
	}

	// the connection has ended
	connection_drop (cr->cerver, cr->connection);

	cerver_receive_delete (cr);

	return NULL;

}

#pragma endregion

#pragma region accept

// 07/06/2020 - create a new connection but check if we can use the cerver's socket pool first
static Connection *cerver_connection_create (Cerver *cerver,
	const i32 new_fd, const struct sockaddr_storage client_address) {

	Connection *retval = NULL;

	if (cerver) {
		if (cerver->sockets_pool) {
			// use a socket from the pool to create a new connection
			Socket *socket = cerver_sockets_pool_pop (cerver);
			if (socket) {
				// manually create the connection
				retval = connection_new ();
				if (retval) {
					// from connection_create_empty ()
					// retval->socket = (Socket *) socket_create_empty ();
					retval->socket = socket;
					retval->sock_receive = sock_receive_new ();
					retval->stats = connection_stats_new ();

					// from connection_create ()
					retval->socket->sock_fd = new_fd;
					memcpy (&retval->address, &client_address, sizeof (struct sockaddr_storage));
					retval->protocol = cerver->protocol;

					connection_get_values (retval);
				}
			}

			else {
				retval = connection_create (new_fd, client_address, cerver->protocol);
			}
		}

		else {
			retval = connection_create (new_fd, client_address, cerver->protocol);
		}
	}

	return retval;

}

// if the cerver requires auth, we put the connection on hold
static u8 cerver_register_new_connection_auth_required (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (!on_hold_connection (cerver, connection)) {
		#ifdef CERVER_DEBUG
		char *status = c_string_create ("Connection is on hold on cerver %s!", cerver->info->name->str);
		if (status) {
			cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, status);
			free (status);
		}
		#endif

		cerver->stats->total_on_hold_connections += 1;

		connection->active = true;

		cerver_info_send_info_packet (cerver, NULL, connection);

		cerver_event_trigger (
			CERVER_EVENT_ON_HOLD_CONNECTED,
			cerver,
			NULL, connection
		);

		retval = 0;
	}

	else {
		char *status = c_string_create ("Failed to put connection on hold in cerver %s",
			cerver->info->name->str);
		if (status) {
			cerver_log_error (status);
			free (status);
		}
	}

	return retval;

}

static u8 cerver_register_new_connection_normal_web (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	CerverReceive *cr = cerver_receive_create_full (
		RECEIVE_TYPE_NORMAL,
		cerver,
		NULL, connection
	);

	if (cr) {
		if (thpool_is_full (cerver->thpool)) {
			#ifdef HANDLER_DEBUG
			char *status = c_string_create (
				"Cerver %s thpool is full! Creating a detachable thread for sock fd <%d> connection...",
				cerver->info->name->str, connection->socket->sock_fd
			);

			if (status) {
				cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
				free (status);
			}
			#endif

			pthread_t thread_id = 0;
			if (!thread_create_detachable (
				&thread_id,
				cerver_receive_http,
				cr
			)) {
				retval = 0;     // success
			}

			else {
				cerver_log_error ("cerver_register_new_connection_normal_web () - failed to create detachable thread!");
				cerver_receive_delete (cr);
			}
		}

		else {
			if (!thpool_add_work (
				cerver->thpool,
				(void (*) (void *)) cerver_receive_http,
				cr
			)) {
				#ifdef HANDLER_DEBUG
				char *status = c_string_create (
					"Added work for sock fd <%d> connection to the thpool!",
					connection->socket->sock_fd
				);

				if (status) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
					free (status);
				}

				status = c_string_create (
					"Cerver %s thpool - %d / %d threads working",
					cerver->info->name->str,
					thpool_get_num_threads_working (cerver->thpool),
					cerver->thpool->num_threads_alive
				);

				if (status) {
					cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
					free (status);
				}
				#endif

				retval = 0;     // success
			}

			else {
				cerver_receive_delete (cr);
			}
		}
	}

	return retval;

}

static u8 cerver_register_new_connection_normal_default_create_detachable (CerverReceive *cr) {

	u8 retval = 1;

	pthread_t thread_id = 0;
	if (!thread_create_detachable (
		&thread_id,
		cerver_receive_threads,
		cr
	)) {
		#ifdef HANDLER_DEBUG
		char *status = c_string_create (
			"Created detachable thread for sock fd <%d> connection",
			cr->connection->socket->sock_fd
		);

		if (status) {
			cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
			free (status);
		}
		#endif

		retval = 0;     // success
	}

	else {
		char *status = c_string_create (
			"cerver_register_new_connection_normal_default () - Failed to create detachable thread for sock fd <%d> connection!",
			cr->connection->socket->sock_fd
		);

		if (status) {
			cerver_log_error (status);
			free (status);
		}

		cerver_receive_delete (cr);
	}

	return retval;

}

static u8 cerver_register_new_connection_normal_default (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	Client *client = client_create ();
	if (client) {
		(void) connection_register_to_client (client, connection);

		if (!client_register_to_cerver (cerver, client)) {
			connection->active = true;

			cerver_info_send_info_packet (cerver, client, connection);

			cerver_event_trigger (
				CERVER_EVENT_CLIENT_CONNECTED,
				cerver,
				client, connection
			);

			switch (cerver->handler_type) {
				case CERVER_HANDLER_TYPE_NONE: break;

				case CERVER_HANDLER_TYPE_POLL:
					// nothing to be done, as connection will be handled by poll ()
					// after being registered to the cerver
					retval = 0;     // success
					break;

				// handle connection in dedicated thread
				case CERVER_HANDLER_TYPE_THREADS: {
					CerverReceive *cr = cerver_receive_create_full (
						RECEIVE_TYPE_NORMAL,
						cerver,
						client, connection
					);

					if (cr) {
						// create a new detachable thread directly
						if (cerver->handle_detachable_threads) {
							retval = cerver_register_new_connection_normal_default_create_detachable (cr);
						}

						else {
							if (thpool_is_full (cerver->thpool)) {
								#ifdef HANDLER_DEBUG
								char *status = c_string_create (
									"Cerver %s thpool is full! Creating a detachable thread for sock fd <%d> connection...",
									cerver->info->name->str, connection->socket->sock_fd
								);

								if (status) {
									cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
									free (status);
								}
								#endif

								retval = cerver_register_new_connection_normal_default_create_detachable (cr);
							}

							else {
								if (!thpool_add_work (
									cerver->thpool,
									(void (*) (void *)) cerver_receive_threads,
									cr
								)) {
									#ifdef HANDLER_DEBUG
									char *status = c_string_create (
										"Added work for sock fd <%d> connection to the thpool!",
										connection->socket->sock_fd
									);

									if (status) {
										cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
										free (status);
									}

									status = c_string_create (
										"Cerver %s thpool - %d / %d threads working",
										cerver->info->name->str,
										thpool_get_num_threads_working (cerver->thpool),
										cerver->thpool->num_threads_alive
									);

									if (status) {
										cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HANDLER, status);
										free (status);
									}
									#endif

									retval = 0;     // success
								}

								else {
									cerver_receive_delete (cr);
								}
							}
						}
					}
				} break;

				default: break;
			}
		}
	}

	else {
		char *status = c_string_create (
			"cerver_register_new_connection_normal_default () - Failed to create new client for new connection with sock fd <%d>!",
			connection->socket->sock_fd
		);

		if (status) {
			cerver_log_error (status);
			free (status);
		}
	}

	return retval;

}

static u8 cerver_register_new_connection_normal (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	switch (cerver->type) {
		case CERVER_TYPE_WEB: {
			retval = cerver_register_new_connection_normal_web (cerver, connection);
		} break;

		default: {
			retval = cerver_register_new_connection_normal_default (cerver, connection);
		} break;
	}

	return retval;

}

static inline u8 cerver_register_new_connection_select (Cerver *cerver, Connection *connection) {

	return cerver->auth_required ?
		cerver_register_new_connection_auth_required (cerver, connection) :
		cerver_register_new_connection_normal (cerver, connection);

}

static void cerver_register_new_connection (Cerver *cerver,
	const i32 new_fd, const struct sockaddr_storage client_address) {

	Connection *connection = cerver_connection_create (cerver, new_fd, client_address);
	if (connection) {
		// #ifdef CERVER_DEBUG
		char *s = c_string_create ("New connection from IP address: %s -- Port: %d",
			connection->ip->str, connection->port);
		if (s) {
			cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CLIENT, s);
			free (s);
		}
		// #endif

		connection->active = true;

		if (!cerver_register_new_connection_select (cerver, connection)) {
			#ifdef CERVER_DEBUG
			s = c_string_create ("New connection to cerver %s!", cerver->info->name->str);
			if (s) {
				cerver_log_msg (stdout, LOG_TYPE_SUCCESS, LOG_TYPE_CERVER, s);
				free (s);
			}
			#endif
		}

		// internal server error - failed to handle the new connection
		else {
			char *status = c_string_create (
				"cerver_register_new_connection () - internal error - dropping sock fd <%d> connection...",
				connection->socket->sock_fd
			);

			if (status) {
				cerver_log_error (status);
				free (status);
			}

			connection_drop (cerver, connection);
		}
	}

	else {
		// #ifdef CERVER_DEBUG
		cerver_log_msg (
			stdout, LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
			"cerver_register_new_connection () - failed to create a new connection!"
		);
		// #endif
	}

}

// accepst a new connection to the cerver
static void cerver_accept (void *cerver_ptr) {

	if (cerver_ptr) {
		Cerver *cerver = (Cerver *) cerver_ptr;

		struct sockaddr_storage client_address;
		memset (&client_address, 0, sizeof (struct sockaddr_storage));
		socklen_t socklen = sizeof (struct sockaddr_storage);

		// accept the new connection
		i32 new_fd = accept (cerver->sock, (struct sockaddr *) &client_address, &socklen);
		if (new_fd > 0) {
			printf ("Accepted fd: %d\n", new_fd);
			cerver_register_new_connection (cerver, new_fd, client_address);
		}

		else {
			// if we get EWOULDBLOCK, we have accepted all connections
			if (errno != EWOULDBLOCK) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, "Accept failed!");
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
		cerver->max_n_fds *= 2;
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

// get a free index in the main cerver poll array
i32 cerver_poll_get_free_idx (Cerver *cerver) {

	if (cerver) {
		for (u32 i = 0; i < cerver->max_n_fds; i++)
			if (cerver->fds[i].fd == -1) return i;
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

static u8 cerver_poll_register_connection_internal (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	i32 idx = cerver_poll_get_free_idx (cerver);
	if (idx > 0) {
		cerver->fds[idx].fd = connection->socket->sock_fd;
		cerver->fds[idx].events = POLLIN;
		cerver->current_n_fds++;

		cerver->stats->current_active_client_connections++;

		#ifdef CERVER_DEBUG
		char *s = c_string_create ("Added sock fd <%d> to cerver %s MAIN poll, idx: %i",
			connection->socket->sock_fd, cerver->info->name->str, idx);
		if (s) {
			cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, s);
			free (s);
		}
		#endif

		#ifdef CERVER_STATS
		char *status = c_string_create ("Cerver %s current active connections: %ld",
			cerver->info->name->str, cerver->stats->current_active_client_connections);
		if (status) {
			cerver_log_msg (stdout, LOG_TYPE_CERVER, LOG_TYPE_NONE, status);
			free (status);
		}
		#endif

		retval = 0;
	}

	return retval;

}

// regsiters a client connection to the cerver's mains poll structure
// and maps the sock fd to the client
// returns 0 on success, 1 on error
u8 cerver_poll_register_connection (Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (cerver && connection) {
		pthread_mutex_lock (cerver->poll_lock);

		if (!cerver_poll_register_connection_internal (cerver, connection)) {
			retval = 0;
		}

		else {
			#ifdef CERVER_DEBUG
			char *s = c_string_create ("Cerver %s main poll is full -- we need to realloc...",
				cerver->info->name->str);
			if (s) {
				cerver_log_msg (stderr, LOG_TYPE_WARNING, LOG_TYPE_NONE, s);
				free (s);
			}
			#endif
			if (cerver_realloc_main_poll_fds (cerver)) {
				char *s = c_string_create ("Failed to realloc cerver %s main poll fds!",
					cerver->info->name->str);
				if (s) {
					cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_NONE, s);
					free (s);
				}
			}

			// try again to register the connection
			else {
				retval = cerver_poll_register_connection_internal (cerver, connection);
			}
		}

		pthread_mutex_unlock (cerver->poll_lock);
	}

	return retval;

}

// removes a sock fd from the cerver's main poll array
// returns 0 on success, 1 on error
u8 cerver_poll_unregister_sock_fd (Cerver *cerver, const i32 sock_fd) {

	u8 retval = 1;

	if (cerver) {
		pthread_mutex_lock (cerver->poll_lock);

		// get the idx of the sock fd in the cerver poll fds
		i32 idx = cerver_poll_get_idx_by_sock_fd (cerver, sock_fd);
		if (idx > 0) {
			cerver->fds[idx].fd = -1;
			cerver->fds[idx].events = -1;
			cerver->current_n_fds--;

			cerver->stats->current_active_client_connections--;

			#ifdef CERVER_DEBUG
			char *s = c_string_create ("Removed sock fd <%d> from cerver %s MAIN poll, idx: %d",
				sock_fd, cerver->info->name->str, idx);
			if (s) {
				cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, s);
				free (s);
			}
			#endif

			#ifdef CERVER_STATS
			char *status = c_string_create ("Cerver %s current active connections: %ld",
				cerver->info->name->str, cerver->stats->current_active_client_connections);
			if (status) {
				cerver_log_msg (stdout, LOG_TYPE_CERVER, LOG_TYPE_NONE, status);
				free (status);
			}
			#endif

			retval = 0;     // removed the sock fd form the cerver poll
		}

		else {
			// #ifdef CERVER_DEBUG
			char *s = c_string_create ("Sock fd <%d> was NOT found in cerver %s MAIN poll!",
				sock_fd, cerver->info->name->str);
			if (s) {
				cerver_log_msg (stdout, LOG_TYPE_WARNING, LOG_TYPE_CERVER, s);
				free (s);
			}
			// #endif
		}

		pthread_mutex_unlock (cerver->poll_lock);
	}

	return retval;

}

// unregsiters a client connection from the cerver's main poll structure
// returns 0 on success, 1 on error
u8 cerver_poll_unregister_connection (Cerver *cerver, Connection *connection) {

	return (cerver && connection) ?
		cerver_poll_unregister_sock_fd (cerver, connection->socket->sock_fd) : 1;

}

static inline void cerver_poll_handle_actual_accept (Cerver *cerver) {

	if (cerver->thpool) {
		if (thpool_add_work (cerver->thpool, cerver_accept, cerver))  {
			char *s = c_string_create (
				"Failed to add cerver_accept () to cerver's %s thpool!",
				cerver->info->name->str
			);

			if (s) {
				cerver_log_error (s);
				free (s);
			}
		}
	}

	else {
		cerver_accept (cerver);
	}

}

static inline void cerver_poll_handle_actual_receive (Cerver *cerver, const u32 idx, CerverReceive *cr) {

	switch (cerver->fds[idx].revents) {
		// A connection setup has been completed or new data arrived
		case POLLIN: {
			// printf ("Receive fd: %d\n", cerver->fds[i].fd);

			if (cerver->thpool) {
				// pthread_mutex_lock (socket->mutex);

				// handle received packets using multiple threads
				// if (thpool_add_work (cerver->thpool, cerver_receive, cr)) {
				//     char *s = c_string_create ("Failed to add cerver_receive () to cerver's %s thpool!",
				//         cerver->info->name->str);
				//     if (s) {
				//         cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_NONE, s);
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
				switch (cr->cerver->type) {
					case CERVER_TYPE_BALANCER: balancer_receive (cr); break;

					default: cerver_receive (cr); break;
				}
			}
		} break;

		// A disconnection request has been initiated by the other end
		// or a connection is broken (SIGPIPE is also set when we try to write to it)
		// or The other end has shut down one direction
		case POLLHUP: {
			// printf ("POLLHUP\n");
			cerver_switch_receive_handle_failed (cr);
		} break;

		// An asynchronous error occurred
		case POLLERR: {
			// printf ("POLLERR\n");
			cerver_switch_receive_handle_failed (cr);
		} break;

		// Urgent data arrived. SIGURG is sent then.
		case POLLPRI: break;

		default: {
			if (cerver->fds[idx].revents != 0) {
				// 17/06/2020 -- 15:06 -- handle as failed any other signal
				// to avoid hanging up at 100% or getting a segfault
				cerver_switch_receive_handle_failed (cr);
			}
		} break;
	}

}

static inline void cerver_poll_handle (Cerver *cerver) {

	if (cerver) {
		if (cerver->thpool) pthread_mutex_lock (cerver->poll_lock);

		// one or more fd(s) are readable, need to determine which ones they are
		for (u32 idx = 0; idx < cerver->max_n_fds; idx++) {
			if (cerver->fds[idx].fd != -1) {
				if (idx == 0) {
					cerver_poll_handle_actual_accept (cerver);
				}

				else {
					CerverReceive *cr = cerver_receive_create (RECEIVE_TYPE_NORMAL, cerver, cerver->fds[idx].fd);
					if (cr) {
						cerver_poll_handle_actual_receive (cerver, idx, cr);
					}
				}
			}
		}

		if (cerver->thpool) pthread_mutex_unlock (cerver->poll_lock);
	}

}

// cerver poll loop to handle events in the registered socket's fds
u8 cerver_poll (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		char *s = c_string_create ("Cerver %s ready in port %d!", cerver->info->name->str, cerver->port);
		if (s) {
			cerver_log_msg (stdout, LOG_TYPE_SUCCESS, LOG_TYPE_CERVER, s);
			free (s);
		}
		#ifdef CERVER_DEBUG
		cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, "Waiting for connections...");
		#endif

		int poll_retval = 0;
		while (cerver->isRunning) {
			poll_retval = poll (cerver->fds, cerver->max_n_fds, cerver->poll_timeout);

			switch (poll_retval) {
				case -1: {
					char *s = c_string_create ("Cerver %s main poll has failed!", cerver->info->name->str);
					if (s) {
						cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_CERVER, s);
						free (s);
					}

					perror ("Error");
					cerver->isRunning = false;
				} break;

				case 0: {
					// #ifdef CERVER_DEBUG
					// char *status = c_string_create ("Cerver %s MAIN poll timeout", cerver->info->name->str);
					// if (status) {
					//     cerver_log_debug (status);
					//     free (status);
					// }
					// #endif
				} break;

				default: {
					cerver_poll_handle (cerver);
				} break;
			}
		}

		#ifdef CERVER_DEBUG
		s = c_string_create ("Cerver %s main poll has stopped!", cerver->info->name->str);
		if (s) {
			cerver_log_msg (stdout, LOG_TYPE_CERVER, LOG_TYPE_NONE, s);
			free (s);
		}
		#endif

		retval = 0;
	}

	else {
		cerver_log_msg (
			stderr,
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Can't listen for connections on a NULL cerver!"
		);
	}

	return retval;

}

#pragma endregion

#pragma region threads

// handle new connections in dedicated threads
u8 cerver_threads (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		char *s = c_string_create ("Cerver %s ready in port %d!", cerver->info->name->str, cerver->port);
		if (s) {
			cerver_log_msg (stdout, LOG_TYPE_SUCCESS, LOG_TYPE_CERVER, s);
			free (s);
		}
		#ifdef CERVER_DEBUG
		cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_CERVER, "Waiting for connections...");
		#endif

		while (cerver->isRunning) {
			cerver_accept (cerver);
		}

		#ifdef CERVER_DEBUG
		s = c_string_create ("Cerver %s accept thread has stopped!", cerver->info->name->str);
		if (s) {
			cerver_log_msg (stdout, LOG_TYPE_CERVER, LOG_TYPE_NONE, s);
			free (s);
		}
		#endif

		retval = 0;
	}

	else {
		cerver_log_msg (
			stderr,
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Can't listen for connections on a NULL cerver!"
		);
	}

	return retval;

}

#pragma endregion