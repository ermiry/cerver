#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/config.h"

#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <sys/prctl.h>
#include <sys/poll.h>

#include "cerver/types/types.h"

#include "cerver/collections/htab.h"

#include "cerver/admin.h"
#include "cerver/auth.h"
#include "cerver/balancer.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/events.h"
#include "cerver/errors.h"
#include "cerver/files.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/receive.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"
#include "cerver/threads/jobs.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

#pragma region handler

static int unique_handler_id = 0;

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
void handler_set_data_create (
	Handler *handler,
	void *(*data_create) (void *args), void *data_create_args
) {

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

	Job *job = NULL;
	Packet *packet = NULL;
	PacketType packet_type = PACKET_TYPE_NONE;
	HandlerData *handler_data = handler_data_new ();
	while (handler->cerver->isRunning) {
		bsem_wait (handler->job_queue->has_jobs);

		if (handler->cerver->isRunning) {
			(void) pthread_mutex_lock (handler->cerver->handlers_lock);
			handler->cerver->num_handlers_working += 1;
			(void) pthread_mutex_unlock (handler->cerver->handlers_lock);

			// read job from queue
			job = job_queue_pull (handler->job_queue);
			if (job) {
				packet = (Packet *) job->args;
				packet_type = packet->header.packet_type;

				handler_data->handler_id = handler->id;
				handler_data->data = handler->data;
				handler_data->packet = packet;

				handler->handler (handler_data);

				job_delete (job);

				switch (packet_type) {
					case PACKET_TYPE_APP: {
						if (handler->cerver->app_packet_handler_delete_packet)
							packet_delete (packet);
					} break;
					case PACKET_TYPE_APP_ERROR: {
						if (handler->cerver->app_error_packet_handler_delete_packet)
							packet_delete (packet);
					} break;
					case PACKET_TYPE_CUSTOM: {
						if (handler->cerver->custom_packet_handler_delete_packet)
							packet_delete (packet);
					} break;

					default: packet_delete (packet); break;
				}
			}

			(void) pthread_mutex_lock (handler->cerver->handlers_lock);
			handler->cerver->num_handlers_working -= 1;
			(void) pthread_mutex_unlock (handler->cerver->handlers_lock);
		}
	}

	handler_data_delete (handler_data);

}

// while client is running, check for new jobs and handle them
static void handler_do_while_client (Handler *handler) {

	Job *job = NULL;
	Packet *packet = NULL;
	HandlerData *handler_data = handler_data_new ();
	while (handler->client->running) {
		bsem_wait (handler->job_queue->has_jobs);

		if (handler->client->running) {
			(void) pthread_mutex_lock (handler->client->handlers_lock);
			handler->client->num_handlers_working += 1;
			(void) pthread_mutex_unlock (handler->client->handlers_lock);

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

			(void) pthread_mutex_lock (handler->client->handlers_lock);
			handler->client->num_handlers_working -= 1;
			(void) pthread_mutex_unlock (handler->client->handlers_lock);
		}
	}

	handler_data_delete (handler_data);

}

// while cerver is running, check for new jobs and handle them
static void handler_do_while_admin (Handler *handler) {

	Job *job = NULL;
	Packet *packet = NULL;
	PacketType packet_type = PACKET_TYPE_NONE;
	HandlerData *handler_data = handler_data_new ();
	while (handler->cerver->isRunning) {
		bsem_wait (handler->job_queue->has_jobs);

		if (handler->cerver->isRunning) {
			(void) pthread_mutex_lock (handler->cerver->admin->handlers_lock);
			handler->cerver->admin->num_handlers_working += 1;
			(void) pthread_mutex_unlock (handler->cerver->admin->handlers_lock);

			// read job from queue
			job = job_queue_pull (handler->job_queue);
			if (job) {
				packet = (Packet *) job->args;
				packet_type = packet->header.packet_type;

				handler_data->handler_id = handler->id;
				handler_data->data = handler->data;
				handler_data->packet = packet;

				handler->handler (handler_data);

				job_delete (job);

				switch (packet_type) {
					case PACKET_TYPE_APP: {
						if (handler->cerver->admin->app_packet_handler_delete_packet)
							packet_delete (packet);
					} break;
					case PACKET_TYPE_APP_ERROR: {
						if (handler->cerver->admin->app_error_packet_handler_delete_packet)
							packet_delete (packet);
					} break;
					case PACKET_TYPE_CUSTOM: {
						if (handler->cerver->admin->custom_packet_handler_delete_packet)
							packet_delete (packet);
					} break;

					default: packet_delete (packet); break;
				}
			}

			(void) pthread_mutex_lock (handler->cerver->admin->handlers_lock);
			handler->cerver->admin->num_handlers_working -= 1;
			(void) pthread_mutex_unlock (handler->cerver->admin->handlers_lock);
		}
	}

	handler_data_delete (handler_data);

}

static void *handler_do (void *handler_ptr) {

	if (handler_ptr) {
		Handler *handler = (Handler *) handler_ptr;

		pthread_mutex_t *handlers_lock = NULL;
		switch (handler->type) {
			case HANDLER_TYPE_CERVER:
				handlers_lock = handler->cerver->handlers_lock;
				break;
			case HANDLER_TYPE_CLIENT:
				handlers_lock = handler->client->handlers_lock;
				break;
			case HANDLER_TYPE_ADMIN:
				handlers_lock = handler->cerver->admin->handlers_lock;
				break;
			default: break;
		}

		// set the thread name
		if (handler->id >= 0) {
			char thread_name[THREAD_NAME_BUFFER_LEN] = { 0 };

			switch (handler->type) {
				case HANDLER_TYPE_CERVER:
					(void) snprintf (
						thread_name, THREAD_NAME_BUFFER_LEN,
						"cerver-handler-%d", handler->unique_id
					);
					break;
				case HANDLER_TYPE_CLIENT:
					(void) snprintf (
						thread_name, THREAD_NAME_BUFFER_LEN,
						"client-handler-%d", handler->unique_id
					);
					break;
				case HANDLER_TYPE_ADMIN:
					(void) snprintf (
						thread_name, THREAD_NAME_BUFFER_LEN,
						"admin-handler-%d", handler->unique_id
					);
					break;
				default: break;
			}

			// printf ("%s\n", thread_name);
			(void) prctl (PR_SET_NAME, thread_name);
		}

		// TODO: register to signals to handle multiple actions

		if (handler->data_create)
			handler->data = handler->data_create (handler->data_create_args);

		// mark the handler as alive and ready
		(void) pthread_mutex_lock (handlers_lock);
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handler->cerver->num_handlers_alive += 1; break;
			case HANDLER_TYPE_CLIENT: handler->client->num_handlers_alive += 1; break;
			case HANDLER_TYPE_ADMIN: handler->cerver->admin->num_handlers_alive += 1; break;
			default: break;
		}
		(void) pthread_mutex_unlock (handlers_lock);

		// while cerver / client is running, check for new jobs and handle them
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handler_do_while_cerver (handler); break;
			case HANDLER_TYPE_CLIENT: handler_do_while_client (handler); break;
			case HANDLER_TYPE_ADMIN: handler_do_while_admin (handler); break;
			default: break;
		}

		if (handler->data_delete)
			handler->data_delete (handler->data);

		(void) pthread_mutex_lock (handlers_lock);
		switch (handler->type) {
			case HANDLER_TYPE_CERVER: handler->cerver->num_handlers_alive -= 1; break;
			case HANDLER_TYPE_CLIENT: handler->client->num_handlers_alive -= 1; break;
			case HANDLER_TYPE_ADMIN: handler->cerver->admin->num_handlers_alive -= 1; break;
			default: break;
		}
		(void) pthread_mutex_unlock (handlers_lock);
	}

	return NULL;

}

// starts the new handler by creating a dedicated thread for it
// called by internal cerver methods
int handler_start (Handler *handler) {

	int retval = 1;

	if (handler) {
		if (handler->type != HANDLER_TYPE_NONE) {
			if (!thread_create_detachable (
				&handler->thread_id,
				(void *(*)(void *)) handler_do,
				(void *) handler
			)) {
				#ifdef HANDLER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
					"Created handler %d thread!",
					handler->unique_id
				);
				#endif

				retval = 0;
			}
		}

		else {
			cerver_log_error (
				"handler_start () - Handler %d is of invalid type!",
				handler->unique_id
			);
		}
	}

	return retval;

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
			cerver_log_debug (
				"Client %ld requested to close the connection", client->id
			);
			#endif

			// check if the client is inside a lobby
			if (lobby) {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_GAME,
					"Client %ld inside lobby %s wants to close the connection...",
					client->id, lobby->id->str
				);
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
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_GAME,
					"Client %ld inside lobby %s wants to close the connection...",
					client->id, lobby->id->str
				);
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
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
				"Got an unknown client packet in cerver %s",
				cerver->info->name->str
			);
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

static inline void cerver_request_get_file_actual (Packet *packet) {

	FileCerver *file_cerver = (FileCerver *) packet->cerver->cerver_data;

	file_cerver->stats->n_files_requests += 1;

	// get the necessary information to fulfil the request
	if (packet->data_size >= sizeof (FileHeader)) {
		char *end = (char *) packet->data;
		FileHeader *file_header = (FileHeader *) end;

		// search for the requested file in the configured paths
		String *actual_filename = file_cerver_search_file (file_cerver, file_header->filename);
		if (actual_filename) {
			#ifdef HANDLER_DEBUG
			cerver_log_debug (
				"cerver_request_get_file () - Sending %s...\n",
				actual_filename->str
			);
			#endif

			// if found, pipe the file contents to the client's socket fd
			// the socket should be blocked during the entire operation
			ssize_t sent = file_cerver_send_file (
				packet->cerver, packet->client, packet->connection,
				actual_filename->str
			);

			if (sent > 0) {
				file_cerver->stats->n_success_files_requests += 1;
				file_cerver->stats->n_files_sent += 1;
				file_cerver->stats->n_bytes_sent += sent;

				#ifdef HANDLER_DEBUG
				cerver_log_success (
					"Sent file %s", actual_filename->str
				);
				#endif
			}

			else {
				cerver_log_error (
					"Failed to send file %s", actual_filename->str
				);

				file_cerver->stats->n_bad_files_sent += 1;
			}

			str_delete (actual_filename);
		}

		else {
			#ifdef HANDLER_DEBUG
			cerver_log_warning ("cerver_request_get_file () - file not found");
			#endif

			// if not found, return an error to the client
			(void) error_packet_generate_and_send (
				CERVER_ERROR_FILE_NOT_FOUND, "File not found",
				packet->cerver, packet->client, packet->connection
			);

			file_cerver->stats->n_bad_files_requests += 1;
		}

	}

	else {
		#ifdef HANDLER_DEBUG
		cerver_log_warning ("cerver_request_get_file () - missing file header");
		#endif

		// return a bad request error packet
		(void) error_packet_generate_and_send (
			CERVER_ERROR_GET_FILE, "Missing file header",
			packet->cerver, packet->client, packet->connection
		);

		file_cerver->stats->n_bad_files_requests += 1;
	}

}

// handles a request from a client to get a file
void cerver_request_get_file (Packet *packet) {

	switch (packet->cerver->type) {
		case CERVER_TYPE_CUSTOM:
		case CERVER_TYPE_FILES: {
			cerver_request_get_file_actual (packet);
		} break;

		default: {
			#ifdef HANDLER_DEBUG
			cerver_log_warning (
				"Cerver %s is not able to handle REQUEST_PACKET_TYPE_GET_FILE requests",
				packet->cerver->info->name->str
			);
			#endif

			(void) error_packet_generate_and_send (
				CERVER_ERROR_GET_FILE, "Unable to process request",
				packet->cerver, packet->client, packet->connection
			);
		} break;
	}

}

static inline void cerver_request_send_file_actual (Packet *packet) {

	FileCerver *file_cerver = (FileCerver *) packet->cerver->cerver_data;

	file_cerver->stats->n_files_upload_requests += 1;

	// get the necessary information to fulfil the request
	if (packet->data_size >= sizeof (FileHeader)) {
		char *end = (char *) packet->data;
		FileHeader *file_header = (FileHeader *) end;

		const char *file_data = NULL;
		size_t file_data_len = 0;
		// printf (
		// 	"\n\npacket->data_size %ld > sizeof (FileHeader) %ld\n\n",
		// 	packet->data_size, sizeof (FileHeader)
		// );
		if (packet->data_size > sizeof (FileHeader)) {
			file_data = end += sizeof (FileHeader);
			file_data_len = packet->data_size - sizeof (FileHeader);
		}

		char *saved_filename = NULL;
		if (!file_cerver->file_upload_handler (
			packet->cerver, packet->client, packet->connection,
			file_header,
			file_data, file_data_len,
			&saved_filename
		)) {
			file_cerver->stats->n_success_files_uploaded += 1;

			file_cerver->stats->n_bytes_received += file_header->len;

			if (file_cerver->file_upload_cb) {
				file_cerver->file_upload_cb (
					packet->cerver, packet->client, packet->connection,
					saved_filename
				);
			}
			
			if (saved_filename) free (saved_filename);
		}

		else {
			cerver_log_error ("Failed to receive file");

			file_cerver->stats->n_bad_files_received += 1;
		}
	}

	else {
		// return a bad request error packet
		(void) error_packet_generate_and_send (
			CERVER_ERROR_SEND_FILE, "Missing file header",
			packet->cerver, packet->client, packet->connection
		);

		file_cerver->stats->n_bad_files_upload_requests += 1;
	}

}

// handles a request from a client to upload a file
void cerver_request_send_file (Packet *packet) {

	switch (packet->cerver->type) {
		case CERVER_TYPE_CUSTOM:
		case CERVER_TYPE_FILES: {
			cerver_request_send_file_actual (packet);
		} break;

		default: {
			#ifdef HANDLER_DEBUG
			cerver_log_warning (
				"Cerver %s is not able to handle REQUEST_PACKET_TYPE_SEND_FILE requests",
				packet->cerver->info->name->str
			);
			#endif

			(void) error_packet_generate_and_send (
				CERVER_ERROR_GET_FILE, "Unable to process request",
				packet->cerver, packet->client, packet->connection
			);
		} break;
	}	

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
				cerver_log (
					LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
					"Got an unknown request packet in cerver %s",
					packet->cerver->info->name->str
				);
				#endif
			} break;
		}
	}

}

// sends back a test packet to the client!
void cerver_test_packet_handler (Packet *packet) {

	#ifdef HANDLER_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_PACKET,
		"Got a test packet in cerver %s.", packet->cerver->info->name->str
	);
	#endif

	Packet *test_packet = packet_new ();
	if (test_packet) {
		packet_set_network_values (test_packet, packet->cerver, packet->client, packet->connection, packet->lobby);
		test_packet->packet_type = PACKET_TYPE_TEST;
		packet_generate (test_packet);
		if (packet_send (test_packet, 0, NULL, false)) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_PACKET,
				"Failed to send error packet from cerver %s.",
				packet->cerver->info->name->str
			);
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
					cerver_log_error (
						"Failed to push a new job to cerver's %s <%d> handler!",
						packet->cerver->info->name->str, packet->header->handler_id
					);
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
					cerver_log_error (
						"Failed to push a new job to cerver's %s app_packet_handler!",
						packet->cerver->info->name->str
					);
				}
			}
		}

		else {
			cerver_log_warning (
				"Cerver %s does not have an app_packet_handler!",
				packet->cerver->info->name->str
			);
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
				cerver_log_error (
					"Failed to push a new job to cerver's %s app_error_packet_handler!",
					packet->cerver->info->name->str
				);
			}
		}
	}

	else {
		cerver_log_warning (
			"Cerver %s does not have an app_error_packet_handler!",
			packet->cerver->info->name->str
		);
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
				cerver_log_error (
					"Failed to push a new job to cerver's %s custom_packet_handler!",
					packet->cerver->info->name->str
				);
			}
		}
	}

	else {
		cerver_log_warning (
			"Cerver %s does not have a custom_packet_handler!",
			packet->cerver->info->name->str
		);
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
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_client_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_client_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_client_packets += 1;
					cerver_client_packet_handler (packet);
					packet_delete (packet);
					break;

				// handles an error from the client
				case PACKET_TYPE_ERROR:
					packet->cerver->stats->received_packets->n_error_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_error_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_error_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_error_packets += 1;
					cerver_error_packet_handler (packet);
					packet_delete (packet);
					break;

				// handles a request made from the client
				case PACKET_TYPE_REQUEST:
					packet->cerver->stats->received_packets->n_request_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_request_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_request_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_request_packets += 1;
					cerver_request_packet_handler (packet);
					packet_delete (packet);
					break;

				// handles authentication packets
				case PACKET_TYPE_AUTH:
					packet->cerver->stats->received_packets->n_auth_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_auth_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_auth_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_auth_packets += 1;
					/* TODO: */
					packet_delete (packet);
					break;

				// handles a game packet sent from the client
				case PACKET_TYPE_GAME:
					packet->cerver->stats->received_packets->n_game_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_game_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_game_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_game_packets += 1;
					game_packet_handler (packet);
					break;

				// user set handler to handle app specific packets
				case PACKET_TYPE_APP:
					packet->cerver->stats->received_packets->n_app_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_app_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_app_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_app_packets += 1;
					cerver_app_packet_handler (packet);
					break;

				// user set handler to handle app specific errors
				case PACKET_TYPE_APP_ERROR:
					packet->cerver->stats->received_packets->n_app_error_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_app_error_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_app_error_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_app_error_packets += 1;
					cerver_app_error_packet_handler (packet);
					break;

				// custom packet hanlder
				case PACKET_TYPE_CUSTOM:
					packet->cerver->stats->received_packets->n_custom_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_custom_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_custom_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_custom_packets += 1;
					cerver_custom_packet_handler (packet);
					break;

				// acknowledge the client we have received his test packet
				case PACKET_TYPE_TEST:
					packet->cerver->stats->received_packets->n_test_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_test_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_test_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_test_packets += 1;
					cerver_test_packet_handler (packet);
					packet_delete (packet);
					break;

				default: {
					packet->cerver->stats->received_packets->n_bad_packets += 1;
					#ifdef CLIENT_STATS
					packet->client->stats->received_packets->n_bad_packets += 1;
					#endif
					#ifdef CONNECTION_STATS
					packet->connection->stats->received_packets->n_bad_packets += 1;
					#endif
					if (packet->lobby) packet->lobby->stats->received_packets->n_bad_packets += 1;

					#ifdef HANDLER_DEBUG
					cerver_log (
						LOG_TYPE_WARNING, LOG_TYPE_PACKET,
						"Got a packet of unknown type in cerver %s.",
						packet->cerver->info->name->str
					);
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
			#ifdef CLIENT_STATS
			packet->client->stats->n_packets_received += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->n_packets_received += 1;
			#endif

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

static inline void cerver_receive_create_normal (
	CerverReceive *cr, Cerver *cerver, const i32 sock_fd
) {

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
			cerver_log_error (
				"cerver_receive_create () - RECEIVE_TYPE_NORMAL - no client with sock fd <%d>",
				sock_fd
			);
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

static inline void cerver_receive_create_on_hold (
	CerverReceive *cr, Cerver *cerver, const i32 sock_fd
) {

	if (cr) {
		cr->connection = connection_get_by_sock_fd_from_on_hold (cerver, sock_fd);
		if (cr->connection) {
			cr->socket = cr->connection->socket;
		}

		// for what ever reason we have a rogue connection
		else {
			// #ifdef CERVER_DEBUG
			cerver_log_error (
				"cerver_receive_create () - RECEIVE_TYPE_ON_HOLD - no connection with sock fd <%d>",
				sock_fd
			);
			// #endif

			// remove the sock fd from the cerver's on hold poll array
			on_hold_poll_unregister_sock_fd (cerver, sock_fd);

			close (sock_fd);        // just close the socket
		}
	}

}

static inline void cerver_receive_create_admin (
	CerverReceive *cr, Cerver *cerver, const i32 sock_fd
) {

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
			cerver_log_error (
				"cerver_receive_create () - RECEIVE_TYPE_ADMIN - no admin with sock fd <%d>",
				sock_fd
			);
			// #endif

			// remove the sock fd from the cerver's admin poll array
			admin_cerver_poll_unregister_sock_fd (cerver->admin, sock_fd);

			close (sock_fd);        // just close the socket
		}
	}

}

CerverReceive *cerver_receive_create (
	ReceiveType receive_type, Cerver *cerver, const i32 sock_fd
) {

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

CerverReceive *cerver_receive_create_full (
	ReceiveType receive_type,
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

static void cerver_receive_handle_spare_packet (
	ReceiveHandle *receive_handle,
	SockReceive *sock_receive,
	size_t buffer_size, char **end, size_t *buffer_pos
) {

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
		#ifdef HANDLER_DEBUG
		i32 sock_fd = receive_handle->socket->sock_fd;
		#endif
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
									if ((header->packet_type == PACKET_TYPE_REQUEST) && (header->request_type == REQUEST_PACKET_TYPE_SEND_FILE)) {
										to_copy_size = remaining_buffer_size - sizeof (PacketHeader);
									}

									else {
										to_copy_size = packet_real_size;
									}

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
								cerver_log (
									LOG_TYPE_ERROR, LOG_TYPE_PACKET,
									"Failed to create a new packet in cerver_handle_receive_buffer ()"
								);
							}
						}

						else {
							cerver_log (
								LOG_TYPE_WARNING, LOG_TYPE_PACKET,
								"Got a packet of invalid size: %ld", packet_size
							);

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
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Sock fd: %d does not have an associated sock_receive in cerver %s.",
				sock_fd, cerver->info->name->str
			);
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
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to add cerver_receive_handle_failed () to cerver's %s thpool!",
					cr->cerver->info->name->str
				);
			}
		}

		else {
			cerver_receive_handle_failed (cr);
		}
	}

}

static inline void cerver_receive_success_receive_handle (
	CerverReceive *cr, ssize_t rc, char *packet_buffer
) {

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
						// cerver_log_debug (
						// 	"Added %s cr->cerver->handle_received_buffer () to thpool!",
						//     cr->cerver->info->name->str
						// );
					}

					else {
						cerver_log_error (
							"Failed to add %s cr->cerver->handle_received_buffer () to thpool!",
							cr->cerver->info->name->str
						);
					}

					// cerver_log (
					// 	LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
					// 	"Cerver %s active thpool threads: %d",
					//     cr->cerver->info->name->str,
					//     thpool_get_num_threads_working (cr->cerver->thpool)
					// );
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

					#ifdef CLIENT_STATS
					cr->client->stats->n_receives_done += 1;
					cr->client->stats->total_bytes_received += rc;
					#endif

					#ifdef CONNECTION_STATS
					cr->connection->stats->n_receives_done += 1;
					cr->connection->stats->total_bytes_received += rc;
					#endif
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

					#ifdef CLIENT_STATS
					cr->client->stats->n_receives_done += 1;
					cr->client->stats->total_bytes_received += rc;
					#endif

					#ifdef CONNECTION_STATS
					cr->connection->stats->n_receives_done += 1;
					cr->connection->stats->total_bytes_received += rc;
					#endif
				} break;

				default: break;
			}
		} break;
	}

}

static void cerver_receive_success (CerverReceive *cr, ssize_t rc, char *packet_buffer) {

	// cerver_log (
	// 	LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
	// 	"Cerver %s rc: %ld for sock fd: %d",
	//     cr->cerver->info->name->str, rc, cr->socket->sock_fd
	// );

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

void balancer_receive_consume_from_connection (CerverReceive *cr, size_t data_size) {

	size_t to_read = 0;

	do {
		char buffer[BALANCER_CONSUME_BUFFER_SIZE] = { 0 };
		to_read = data_size >= BALANCER_CONSUME_BUFFER_SIZE ? BALANCER_CONSUME_BUFFER_SIZE : data_size;
		if (recv (cr->socket->sock_fd, buffer, to_read, 0) <= 0) {
			#ifdef HANDLER_DEBUG
			cerver_log_warning (
				"balancer_receive_consume_from_connection () - rc <= 0 - sock fd %d", 
				cr->socket->sock_fd
			);
			#endif

			cerver_switch_receive_handle_failed (cr);
			break;
		}

		data_size -= to_read;
	} while (data_size <= 0);

}

// we received a packet of a bad type (the handler is unable to handle it)
// consume from the socket until the next packet header
static void balancer_receive_bad_type (CerverReceive *cr, PacketHeader *header) {

	if (header->packet_size > sizeof (PacketHeader)) {
		size_t data_size = header->packet_size - sizeof (PacketHeader);
		if (data_size < MAX_UDP_PACKET_SIZE) {
			balancer_receive_consume_from_connection (cr, data_size);
		}

		else {
			// end connection directly if remaining packet is to large
			#ifdef HANDLER_DEBUG
			cerver_log_warning (
				"balancer_receive_bad_type () - sock fd %d - data size %ld > %d", 
				cr->socket->sock_fd, data_size, MAX_UDP_PACKET_SIZE
			);
			#endif

			cerver_switch_receive_handle_failed (cr);
		}
	}

}

static inline void balancer_receive_success (CerverReceive *cr, PacketHeader *header) {

	size_t rc = sizeof (PacketHeader);

	switch (header->packet_type) {
		case PACKET_TYPE_CLIENT:
			cr->cerver->stats->received_packets->n_client_packets += 1;
			// cr->client->stats->received_packets->n_client_packets += 1;
			// cr->connection->stats->received_packets->n_client_packets += 1;
			rc = header->packet_size;
			cerver_client_packet_handler_by_header (
				header,
				cr->cerver, cr->client, cr->connection, cr->lobby
			);
			break;

		case PACKET_TYPE_ERROR:
			cr->cerver->stats->received_packets->n_error_packets += 1;
			balancer_receive_bad_type (cr, header);
			break;

		case PACKET_TYPE_AUTH:
			cr->cerver->stats->received_packets->n_auth_packets += 1;
			balancer_receive_bad_type (cr, header);
			break;

		// only route packets of these types to services
		case PACKET_TYPE_CERVER:
		case PACKET_TYPE_REQUEST:
		case PACKET_TYPE_GAME:
		case PACKET_TYPE_APP:
		case PACKET_TYPE_APP_ERROR:
		case PACKET_TYPE_CUSTOM:
		case PACKET_TYPE_TEST: {
			balancer_route_to_service (
				cr,
				cr->cerver->balancer, cr->connection,
				header
			);
		} break;

		case PACKET_TYPE_NONE:
		default: {
			cr->cerver->stats->received_packets->n_bad_packets += 1;
			// cr->client->stats->received_packets->n_bad_packets += 1;
			// cr->connection->stats->received_packets->n_bad_packets += 1;
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_PACKET,
				"balancer_receive () - packet of unknown type in cerver %s.",
				cr->cerver->info->name->str
			);
			#endif

			balancer_receive_bad_type (cr, header);
		} break;
	}

	cerver_receive_success_update_stats (cr, rc);

}

static u8 balancer_receive (
	CerverReceive *cr,
	char *buffer, const size_t buffer_size
) {

	u8 retval = 1;

	ssize_t rc = recv (cr->socket->sock_fd, buffer, buffer_size, 0);
	// ssize_t rc = recv (cr->socket->sock_fd, buffer, buffer_size, MSG_DONTWAIT);
	switch (rc) {
		case -1: {
			if (errno == EAGAIN) {
				#ifdef HANDLER_DEBUG
				cerver_log_debug (
					"balancer_receive () - sock fd: %d timed out",
					cr->socket->sock_fd
				);
				#endif

				retval = 0;
			}

			else {
				#ifdef HANDLER_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"balancer_receive () - rc < 0 - sock fd: %d",
					cr->socket->sock_fd
				);

				perror ("Error");
				#endif
			}
		} break;

		case 0: {
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"balancer_receive () - rc == 0 - sock fd: %d",
				cr->socket->sock_fd
			);
			#endif
		} break;

		default: {
			balancer_receive_success (cr, (PacketHeader *) buffer);
			retval = 0;
		} break;
	}

	return retval;

}

// receive all incoming data from the socket
void cerver_receive (void *cerver_receive_ptr) {

	if (cerver_receive_ptr) {
		CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

		if (cr->cerver && cr->socket) {
			if (cr->socket->sock_fd > 0) {
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
								cerver_log (
									LOG_TYPE_ERROR, LOG_TYPE_CERVER,
									"cerver_receive () - rc < 0 - sock fd: %d",
									cr->socket->sock_fd
								);

								perror ("Error");
								#endif

								cerver_switch_receive_handle_failed (cr);
							}

							free (packet_buffer);
						} break;

						case 0: {
							// man recv -> steam socket perfomed an orderly shutdown
							#ifdef HANDLER_DEBUG
							cerver_log (
								LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
								"cerver_recieve () - rc == 0 - sock fd: %d",
								cr->socket->sock_fd
							);

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
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_HANDLER,
						"cerver_receive () - Failed to allocate packet buffer for connection with sock fd <%d>!",
						cr->connection->socket->sock_fd
					);
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
static inline u8 cerver_receive_threads_actual (
	CerverReceive *cr,
	char *buffer, const size_t buffer_size
) {

	u8 retval = 1;
	
	ssize_t rc = recv (cr->socket->sock_fd, buffer, buffer_size, 0);
	switch (rc) {
		case -1: {
			if (errno == EAGAIN) {
				#ifdef HANDLER_DEBUG
				cerver_log_debug (
					"cerver_receive_threads () - sock fd: %d timed out",
					cr->socket->sock_fd
				);
				#endif

				retval = 0;
			}

			else {
				#ifdef HANDLER_DEBUG
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_CERVER,
					"cerver_receive_threads () - rc < 0 - sock fd: %d",
					cr->socket->sock_fd
				);

				perror ("Error ");
				#endif
			}
		} break;

		case 0: {
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"cerver_receive_threads () - rc == 0 - sock fd: %d",
				cr->socket->sock_fd
			);

			// perror ("Error ");
			#endif
		} break;

		default: {
			cerver_receive_success (cr, rc, buffer);

			retval = 0;
		} break;
	}

	return retval;

}

static void *cerver_receive_threads (void *cerver_receive_ptr) {

	CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

	i32 sock_fd = cr->socket->sock_fd;

	#ifdef HANDLER_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
		"cerver_receive_threads () - sock fd <%d> connection thread has started",
		sock_fd
	);
	#endif

	// set the socket's timeout to prevent thread from getting stuck if no more data to read
	(void) sock_set_timeout (sock_fd, DEFAULT_SOCKET_RECV_TIMEOUT);

	u8 (*receive)(CerverReceive *, char *, const size_t) = NULL;
	size_t buffer_size = 0;
	char *buffer = NULL;

	switch (cr->cerver->type) {
		case CERVER_TYPE_BALANCER: {
			receive = balancer_receive;
			buffer_size = sizeof (PacketHeader);
		} break;

		default: {
			receive = cerver_receive_threads_actual;
			buffer_size = cr->cerver->receive_buffer_size;
		} break;
	}

	buffer = (char *) calloc (buffer_size, sizeof (char));
	if (buffer) {
		while (
			(cr->socket->sock_fd > 0)
			&& cr->cerver->isRunning
			&& !receive (cr, buffer, buffer_size)
		);

		free (buffer);
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_HANDLER,
			"cerver_receive_threads () - Failed to allocate packet buffer for sock fd <%d> connection!",
			cr->connection->socket->sock_fd
		);
	}

	// check if the connection has already ended
	if (cr->socket->sock_fd > 0) {
		client_remove_connection_by_sock_fd (cr->cerver, cr->client, cr->socket->sock_fd);
	}

	cerver_receive_delete (cr);

	#ifdef HANDLER_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
		"cerver_receive_threads () - loop has ended - dropping sock fd <%d> connection...",
		sock_fd
	);
	#endif

	return NULL;

}

#pragma endregion

#pragma region accept

// create a new connection but check if we can use the cerver's socket pool first
static Connection *cerver_connection_create (
	Cerver *cerver,
	const i32 new_fd, const struct sockaddr_storage *client_address
) {

	Connection *retval = NULL;

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

				// FIXME:
				retval->sock_receive = sock_receive_new ();

				retval->stats = connection_stats_new ();

				// from connection_create ()
				retval->socket->sock_fd = new_fd;
				(void) memcpy (
					&retval->address,
					&client_address,
					sizeof (struct sockaddr_storage)
				);

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

	return retval;

}

// if the cerver requires auth, we put the connection on hold
static u8 cerver_register_new_connection_auth_required (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (!on_hold_connection (cerver, connection)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Connection is on hold on cerver %s!",
			cerver->info->name->str
		);
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
		cerver_log_error (
			"Failed to put connection on hold in cerver %s",
			cerver->info->name->str
		);
	}

	return retval;

}

static u8 cerver_register_new_connection_normal_default_create_detachable (
	CerverReceive *cr
) {

	u8 retval = 1;

	pthread_t thread_id = 0;
	if (!thread_create_detachable (
		&thread_id,
		cerver_receive_threads,
		cr
	)) {
		#ifdef HANDLER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
			"Created detachable thread for sock fd <%d> connection",
			cr->connection->socket->sock_fd
		);
		#endif

		retval = 0;     // success
	}

	else {
		cerver_log_error (
			"cerver_register_new_connection_normal_default () - "
			"Failed to create detachable thread for sock fd <%d> connection!",
			cr->connection->socket->sock_fd
		);

		cerver_receive_delete (cr);
	}

	return retval;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-function-type"

static u8 cerver_register_new_connection_normal_default_select_handler_threads (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

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
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
					"Cerver %s thpool is full! "
					"Creating a detachable thread for sock fd <%d> connection...",
					cerver->info->name->str, connection->socket->sock_fd
				);
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
					cerver_log (
						LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
						"Added work for sock fd <%d> connection to the thpool!",
						connection->socket->sock_fd
					);

					cerver_log (
						LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
						"Cerver %s thpool - %d / %d threads working",
						cerver->info->name->str,
						thpool_get_num_threads_working (cerver->thpool),
						cerver->thpool->num_threads_alive
					);
					#endif

					retval = 0;     // success
				}

				else {
					cerver_receive_delete (cr);
				}
			}
		}
	}

	return retval;

}

#pragma GCC diagnostic pop

// select how client connection will be handled based on cerver's handler type
u8 cerver_register_new_connection_normal_default_select_handler (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	switch (cerver->handler_type) {
		case CERVER_HANDLER_TYPE_NONE: break;

		case CERVER_HANDLER_TYPE_POLL: {
			// nothing to be done, as connection will be handled by poll ()
			// after being registered to the cerver
			retval = 0;     // success
		} break;

		// handle connection in dedicated thread
		case CERVER_HANDLER_TYPE_THREADS: {
			retval = cerver_register_new_connection_normal_default_select_handler_threads (
				cerver, client, connection
			);
		} break;

		default: break;
	}

	return retval;

}

static u8 cerver_register_new_connection_normal_default (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	Client *client = client_create ();
	if (client) {
		(void) connection_register_to_client (client, connection);

		if (!client_register_to_cerver (cerver, client)) {
			connection->active = true;

			(void) cerver_info_send_info_packet (cerver, client, connection);

			// TODO: better error handling
			if (!cerver_register_new_connection_normal_default_select_handler (
				cerver, client, connection
			)) {
				cerver_event_trigger (
					CERVER_EVENT_CLIENT_CONNECTED,
					cerver,
					client, connection
				);

				retval = 0;
			}
		}
	}

	else {
		cerver_log_error (
			"cerver_register_new_connection_normal_default () - "
			"Failed to create new client for new connection with sock fd <%d>!",
			connection->socket->sock_fd
		);
	}

	return retval;

}

static u8 cerver_register_new_connection_normal (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	switch (cerver->type) {
		case CERVER_TYPE_WEB: break;

		default: {
			retval = cerver_register_new_connection_normal_default (
				cerver, connection
			);
		} break;
	}

	return retval;

}

static inline u8 cerver_register_new_connection_select (
	Cerver *cerver, Connection *connection
) {

	return cerver->auth_required ?
		cerver_register_new_connection_auth_required (cerver, connection) :
		cerver_register_new_connection_normal (cerver, connection);

}

static void cerver_register_new_connection (
	Cerver *cerver,
	const i32 new_fd, const struct sockaddr_storage *client_address
) {

	Connection *connection = cerver_connection_create (
		cerver, new_fd, client_address
	);
	
	if (connection) {
		// #ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
			"New connection from IP address: %s -- Port: %d",
			connection->ip, connection->port
		);
		// #endif

		connection->active = true;

		if (!cerver_register_new_connection_select (cerver, connection)) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"New connection to cerver %s!", cerver->info->name->str
			);
			#endif
		}

		// internal server error - failed to handle the new connection
		else {
			cerver_log_error (
				"cerver_register_new_connection () "
				"- internal error - dropping sock fd <%d> connection...",
				connection->socket->sock_fd
			);

			connection_drop (cerver, connection);
		}
	}

	else {
		// FIXME: close the socket and cleanup
		// #ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
			"cerver_register_new_connection () - failed to create a new connection!"
		);
		// #endif
	}

}

// accepst a new connection to the cerver
static void cerver_accept (void *cerver_ptr) {

	Cerver *cerver = (Cerver *) cerver_ptr;

	struct sockaddr_storage client_address = { 0 };
	// (void) memset (&client_address, 0, sizeof (struct sockaddr_storage));
	socklen_t socklen = sizeof (struct sockaddr_storage);

	// accept the new connection
	i32 new_fd = accept (cerver->sock, (struct sockaddr *) &client_address, &socklen);
	if (new_fd > 0) {
		#ifdef HANDLER_DEBUG
		cerver_log_debug ("Accepted fd: %d", new_fd);
		#endif
		cerver_register_new_connection (cerver, new_fd, &client_address);
	}

	else {
		// if we get EWOULDBLOCK, we have accepted all connections
		if (errno != EWOULDBLOCK) {
			cerver_log (LOG_TYPE_ERROR, LOG_TYPE_CERVER, "Accept failed!");
			perror ("Error");
		}
	}

}

#pragma endregion

#pragma region register

// select how a connection will be handled
// based on cerver's handler type
// returns 0 on success, 1 on error
u8 cerver_handler_register_connection (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		switch (cerver->handler_type) {
			case CERVER_HANDLER_TYPE_NONE: break;

			// handle connection using the cerver's poll
			case CERVER_HANDLER_TYPE_POLL: {
				retval = cerver_poll_register_connection (
					cerver, connection
				);
			} break;

			// handle connection in dedicated thread
			case CERVER_HANDLER_TYPE_THREADS: {
				retval = cerver_register_new_connection_normal_default_select_handler_threads (
					cerver, client, connection
				);
			} break;

			default: break;
		}
	}

	return retval;

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
		cerver->fds = (struct pollfd *) realloc (
			cerver->fds, cerver->max_n_fds * sizeof (struct pollfd)
		);

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

static u8 cerver_poll_register_connection_internal (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	i32 idx = cerver_poll_get_free_idx (cerver);
	if (idx > 0) {
		cerver->fds[idx].fd = connection->socket->sock_fd;
		cerver->fds[idx].events = POLLIN;
		cerver->current_n_fds++;

		cerver->stats->current_active_client_connections++;

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Added sock fd <%d> to cerver %s MAIN poll, idx: %i",
			connection->socket->sock_fd, cerver->info->name->str, idx
		);
		#endif

		#ifdef CERVER_STATS
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Cerver %s current active connections: %ld",
			cerver->info->name->str,
			cerver->stats->current_active_client_connections
		);
		#endif

		retval = 0;
	}

	return retval;

}

// regsiters a client connection to the cerver's mains poll structure
// and maps the sock fd to the client
// returns 0 on success, 1 on error
u8 cerver_poll_register_connection (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		pthread_mutex_lock (cerver->poll_lock);

		if (!cerver_poll_register_connection_internal (
			cerver, connection
		)) {
			retval = 0;
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"Cerver %s main poll is full -- we need to realloc...",
				cerver->info->name->str
			);
			#endif

			if (cerver_realloc_main_poll_fds (cerver)) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to realloc cerver %s main poll fds!",
					cerver->info->name->str
				);
			}

			// try again to register the connection
			else {
				retval = cerver_poll_register_connection_internal (
					cerver, connection
				);
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
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Removed sock fd <%d> from cerver %s MAIN poll, idx: %d",
				sock_fd, cerver->info->name->str, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_NONE,
				"Cerver %s current active connections: %ld",
				cerver->info->name->str,
				cerver->stats->current_active_client_connections
			);
			#endif

			retval = 0;     // removed the sock fd form the cerver poll
		}

		else {
			// #ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Sock fd <%d> was NOT found in cerver %s MAIN poll!",
				sock_fd, cerver->info->name->str
			);
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
			cerver_log_error (
				"Failed to add cerver_accept () to cerver's %s thpool!",
				cerver->info->name->str
			);
		}
	}

	else {
		cerver_accept (cerver);
	}

}

static inline void cerver_poll_handle_actual_receive (
	Cerver *cerver,
	struct pollfd *active_fd,
	char *packet_buffer
) {

	// TODO: as we will not use threads anymore,
	// there is no need of allocating a new structure each time
	// we need to handle a connection
	CerverReceive *cr = cerver_receive_create (
		RECEIVE_TYPE_NORMAL, cerver, active_fd->fd
	);

	if (cr) {
		switch (active_fd->revents) {
			// A connection setup has been completed or new data arrived
			case POLLIN: {
				// printf ("Receive fd: %d\n", cerver->fds[i].fd);

				// receive all incoming data from the socket
				// and handle the packets in the same thread
				cerver_receive_internal (
					cr,
					packet_buffer, cerver->receive_buffer_size
				);
			} break;

			// A disconnection request has been initiated by the other end
			// or a connection is broken (SIGPIPE is also set when we try to write to it)
			// or The other end has shut down one direction
			case POLLHUP: {
				// printf ("POLLHUP\n");
				cerver_receive_handle_failed (cr);
			} break;

			// An asynchronous error occurred
			case POLLERR: {
				// printf ("POLLERR\n");
				cerver_receive_handle_failed (cr);
			} break;

			// Urgent data arrived. SIGURG is sent then.
			case POLLPRI: break;

			default: {
				if (active_fd->revents != 0) {
					cerver_receive_handle_failed (cr);
				}
			} break;
		}

		cerver_receive_delete (cr);
	}

}

static inline void cerver_poll_handle (
	Cerver *cerver, char *packet_buffer
) {

	// one or more fd(s) are readable, need to determine which ones they are
	for (u32 idx = 0; idx < cerver->max_n_fds; idx++) {
		if (cerver->fds[idx].fd > -1) {
			if (idx == 0) {
				// the cerver's sock fd has an event
				cerver_poll_handle_actual_accept (cerver);
			}

			else {
				cerver_poll_handle_actual_receive (
					cerver,
					&cerver->fds[idx],
					packet_buffer
				);
			}
		}
	}

}

// cerver poll loop to handle events in the registered socket's fds
u8 cerver_poll (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
			"Cerver %s is ready in port %d!",
			cerver->info->name->str, cerver->port
		);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Waiting for connections..."
		);
		#endif

		char *packet_buffer = (char *) calloc (
			cerver->receive_buffer_size, sizeof (char)
		);

		if (packet_buffer) {
			int poll_retval = 0;
			while (cerver->isRunning) {
				poll_retval = poll (
					cerver->fds,
					cerver->max_n_fds,
					cerver->poll_timeout
				);

				switch (poll_retval) {
					case -1: {
						cerver_log (
							LOG_TYPE_ERROR, LOG_TYPE_CERVER,
							"Cerver %s main poll has failed!",
							cerver->info->name->str
						);

						perror ("Error");
						cerver->isRunning = false;
					} break;

					case 0: {
						// #ifdef CERVER_DEBUG
						// cerver_log_debug (
						// 	"Cerver %s MAIN poll timeout",
						// 	cerver->info->name->str
						// );
						// #endif
					} break;

					default: {
						cerver_poll_handle (cerver, packet_buffer);
					} break;
				}
			}

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_NONE,
				"Cerver %s main poll has stopped!",
				cerver->info->name->str
			);
			#endif

			free (packet_buffer);

			retval = 0;
		}

		else {
			cerver_log_error (
				"Failed to allocate cerver poll's packet buffer!"
			);
		}
	}

	else {
		cerver_log (
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
		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
			"Cerver %s ready in port %d!",
			cerver->info->name->str, cerver->port
		);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Waiting for connections..."
		);
		#endif

		while (cerver->isRunning) {
			cerver_accept (cerver);
		}

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Cerver %s accept thread has stopped!",
			cerver->info->name->str
		);
		#endif

		retval = 0;
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Can't listen for connections on a NULL cerver!"
		);
	}

	return retval;

}

#pragma endregion