#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/config.h"

#include <errno.h>
#include <fcntl.h>
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
#include "cerver/errors.h"
#include "cerver/events.h"
#include "cerver/files.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/socket.h"
#include "cerver/timer.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the fd that is kept open to accept and close connections when the process runs out of fds
static const char *HANDLER_RESERVE_FD_PATH = "/dev/null";

// how long accept () waits after the process ran out of fds (2^24 ns)
static const long HANDLER_ACCEPT_BACKOFF = 16777216L;

static const u32 HANDLER_POLL_FDS_GROWTH_FACTOR = 2;

// how long handler_stop () waits for the thread before waking it again (2^24 ns)
static const long HANDLER_STOP_WAKE_NANOSECONDS = 16777216L;

static const long HANDLER_NANOSECONDS_PER_SECOND = 1000000000L;

static const long long HANDLER_MICROSECONDS_PER_SECOND = 1000000LL;
static const long long HANDLER_NANOSECONDS_PER_MICROSECOND = 1000LL;

#pragma region handler

// handlers are created by every client and cerver, also from different threads at the same time
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
		handler->joinable = false;

		handler->data = NULL;
		handler->data_create = NULL;
		handler->data_create_args = NULL;
		handler->data_delete = NULL;

		handler->handler = NULL;
		handler->direct_handle = false;

		handler->job_queue = NULL;

		handler->cerver = NULL;
		handler->client = NULL;

		histogram_init (&handler->stats.durations);
		histogram_init (&handler->stats.sizes);
	}

	return handler;

}

void handler_delete (void *handler_ptr) {

	if (handler_ptr) {
		Handler *handler = (Handler *) handler_ptr;

		// a handler that was never stopped does not keep its thread resources
		if (handler->joinable) {
			(void) pthread_detach (handler->thread_id);
		}

		job_queue_delete (handler->job_queue);

		free (handler_ptr);
	}

}

// creates a new handler
// handler method is your actual app packet handler
Handler *handler_create (Action handler_method) {

	Handler *handler = handler_new ();
	if (handler) {
		handler->unique_id = __atomic_fetch_add (&unique_handler_id, 1, __ATOMIC_RELAXED);

		handler->handler = handler_method;

		handler->job_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
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

// copies the stats of the packets that the handler handled
void handler_get_stats (const Handler *handler, HandlerStats *stats) {

	if (handler && stats) {
		histogram_copy (&stats->durations, &handler->stats.durations);
		histogram_copy (&stats->sizes, &handler->stats.sizes);
	}

}

// prints the stats of the handler with the cerver log
void handler_stats_print (const Handler *handler) {

	if (handler) {
		HandlerStats stats;
		handler_get_stats (handler, &stats);

		cerver_log_msg ("Handler %d stats:", handler->id);
		histogram_print ("Durations", "us", &stats.durations);
		histogram_print ("Sizes", "bytes", &stats.sizes);
	}

}

// the owner of the handler stopped, read with the lock of the handler's queue
static bool handler_cerver_stopped (const void *handler_ptr) {

	return !cerver_is_running (((const Handler *) handler_ptr)->cerver);

}

static bool handler_client_stopped (const void *handler_ptr) {

	return !client_is_running (((const Handler *) handler_ptr)->client);

}

// a packet whose handler owns it is released after the handler returns
static void handler_packet_release (
	Packet *packet, const PacketType packet_type,
	const bool app_delete, const bool app_error_delete, const bool custom_delete
) {

	switch (packet_type) {
		case PACKET_TYPE_APP: if (app_delete) packet_delete (packet); break;
		case PACKET_TYPE_APP_ERROR: if (app_error_delete) packet_delete (packet); break;
		case PACKET_TYPE_CUSTOM: if (custom_delete) packet_delete (packet); break;

		default: packet_delete (packet); break;
	}

}

// the values of a packet for the stats, taken before its handler runs,
// because the handler can release the packet
typedef struct HandlerPacketValues {

	PacketType packet_type;
	u32 request_type;
	u64 size;

	struct timespec start;

} HandlerPacketValues;

static void handler_packet_values_start (
	HandlerPacketValues *values, const Packet *packet
) {

	values->packet_type = packet->header.packet_type;
	values->request_type = packet->header.request_type;
	values->size = packet->header.packet_size;

	(void) clock_gettime (CLOCK_MONOTONIC, &values->start);

}

static u64 handler_elapsed_microseconds (const struct timespec *start) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	const long long elapsed =
		((long long) (now.tv_sec - start->tv_sec) * HANDLER_MICROSECONDS_PER_SECOND)
		+ ((long long) (now.tv_nsec - start->tv_nsec) / HANDLER_NANOSECONDS_PER_MICROSECOND);

	return (elapsed > 0) ? (u64) elapsed : 0;

}

// adds the packet to the stats of the handler,
// and to the stats of its request type when the handler is of a cerver
static void handler_stats_add (Handler *handler, const HandlerPacketValues *values) {

	const u64 elapsed = handler_elapsed_microseconds (&values->start);

	histogram_add (&handler->stats.durations, elapsed);
	histogram_add (&handler->stats.sizes, values->size);

	if ((handler->type == HANDLER_TYPE_CERVER) && handler->cerver) {
		cerver_request_stats_add (
			handler->cerver,
			values->packet_type, values->request_type,
			values->size, elapsed
		);
	}

}

// runs the handler with the packet of the job, the job goes back to the pool of the queue
// and the time it ran goes to the stats of the queue and of the handler
// working counts the handlers that are handling a packet
// returns the type of the packet
static PacketType handler_job_run (
	Handler *handler, HandlerData *handler_data, Job *job,
	unsigned int *working
) {

	Packet *packet = (Packet *) job->args;

	(void) __atomic_add_fetch (working, 1, __ATOMIC_SEQ_CST);

	HandlerPacketValues values;
	handler_packet_values_start (&values, packet);

	handler_data->handler_id = handler->id;
	handler_data->data = handler->data;
	handler_data->packet = packet;

	handler->handler (handler_data);

	job_queue_record_run (handler->job_queue, &values.start);
	handler_stats_add (handler, &values);

	job_return (handler->job_queue, job);

	(void) __atomic_sub_fetch (working, 1, __ATOMIC_SEQ_CST);

	return values.packet_type;

}

// while cerver is running, check for new jobs and handle them
static void handler_do_while_cerver (Handler *handler) {

	HandlerData *handler_data = handler_data_new ();
	while (cerver_is_running (handler->cerver)) {
		Job *job = (Job *) job_queue_pull_interruptible (
			handler->job_queue, handler_cerver_stopped, handler
		);

		if (job) {
			Packet *packet = (Packet *) job->args;
			const PacketType packet_type = handler_job_run (
				handler, handler_data, job, &handler->cerver->num_handlers_working
			);

			handler_packet_release (
				packet, packet_type,
				handler->cerver->app_packet_handler_delete_packet,
				handler->cerver->app_error_packet_handler_delete_packet,
				handler->cerver->custom_packet_handler_delete_packet
			);
		}
	}

	handler_data_delete (handler_data);

}

// while client is running, check for new jobs and handle them
static void handler_do_while_client (Handler *handler) {

	HandlerData *handler_data = handler_data_new ();
	while (client_is_running (handler->client)) {
		Job *job = (Job *) job_queue_pull_interruptible (
			handler->job_queue, handler_client_stopped, handler
		);

		if (job) {
			Packet *packet = (Packet *) job->args;
			(void) handler_job_run (
				handler, handler_data, job, &handler->client->num_handlers_working
			);

			packet_delete (packet);
		}
	}

	handler_data_delete (handler_data);

}

// while cerver is running, check for new jobs and handle them
static void handler_do_while_admin (Handler *handler) {

	HandlerData *handler_data = handler_data_new ();
	while (cerver_is_running (handler->cerver)) {
		Job *job = (Job *) job_queue_pull_interruptible (
			handler->job_queue, handler_cerver_stopped, handler
		);

		if (job) {
			Packet *packet = (Packet *) job->args;
			const PacketType packet_type = handler_job_run (
				handler, handler_data, job, &handler->cerver->admin->num_handlers_working
			);

			handler_packet_release (
				packet, packet_type,
				handler->cerver->admin->app_packet_handler_delete_packet,
				handler->cerver->admin->app_error_packet_handler_delete_packet,
				handler->cerver->admin->custom_packet_handler_delete_packet
			);
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
			char thread_name[THREAD_NAME_BUFFER_SIZE] = { 0 };

			switch (handler->type) {
				case HANDLER_TYPE_CERVER:
					(void) snprintf (
						thread_name, THREAD_NAME_BUFFER_SIZE,
						"cerver-handler-%d", handler->unique_id
					);
					break;
				case HANDLER_TYPE_CLIENT:
					(void) snprintf (
						thread_name, THREAD_NAME_BUFFER_SIZE,
						"client-handler-%d", handler->unique_id
					);
					break;
				case HANDLER_TYPE_ADMIN:
					(void) snprintf (
						thread_name, THREAD_NAME_BUFFER_SIZE,
						"admin-handler-%d", handler->unique_id
					);
					break;
				default: break;
			}

			// printf ("%s\n", thread_name);
			(void) prctl (PR_SET_NAME, thread_name);
		}

		// the signals belong to the process, a handler is stopped with its cerver (cerver_stop_request ())
		if (handler->data_create)
			handler->data = handler->data_create (handler->data_create_args);

		// mark the handler as alive and ready, the cerver waits for it in its condition
		(void) pthread_mutex_lock (handlers_lock);
		switch (handler->type) {
			case HANDLER_TYPE_CERVER:
				handler->cerver->num_handlers_alive += 1;
				if (handler->cerver->handlers_alive_cond) {
					(void) pthread_cond_broadcast (handler->cerver->handlers_alive_cond);
				}
				break;
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
			if (!thread_create_joinable (
				&handler->thread_id,
				(void *(*)(void *)) handler_do,
				(void *) handler
			)) {
				handler->joinable = true;

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

// the absolute time until handler_stop () waits for the thread to end
static void handler_stop_deadline (struct timespec *deadline) {

	(void) clock_gettime (CLOCK_REALTIME, deadline);

	deadline->tv_nsec += HANDLER_STOP_WAKE_NANOSECONDS;
	if (deadline->tv_nsec >= HANDLER_NANOSECONDS_PER_SECOND) {
		deadline->tv_sec += 1;
		deadline->tv_nsec -= HANDLER_NANOSECONDS_PER_SECOND;
	}

}

// wakes the handler's thread until it sees that its owner stopped and joins it,
// the thread checks its owner with the queue's lock, so a wake is never lost,
// it is repeated in case the thread was handling a job
void handler_stop (Handler *handler) {

	if (handler && handler->joinable) {
		struct timespec deadline = { 0, 0 };

		int joined = 0;
		do {
			job_queue_signal (handler->job_queue);

			handler_stop_deadline (&deadline);
			joined = pthread_timedjoin_np (handler->thread_id, NULL, &deadline);
		} while (joined == ETIMEDOUT);

		handler->joinable = false;
	}

}

// pushes the packet to the handler's queue in a job of the queue's pool,
// the job goes back to the pool if it could not be pushed
// returns 0 on success, 1 on error
static unsigned int handler_push_packet (Handler *handler, Packet *packet) {

	unsigned int retval = 1;

	Job *job = job_get (handler->job_queue);
	if (job) {
		job->args = packet;

		retval = job_queue_push (handler->job_queue, job);
		if (retval) job_return (handler->job_queue, job);
	}

	return retval;

}

// passes the packet to the handler of its type, directly or by its job queue,
// a direct handler releases it after it returns only if delete_packet is set,
// and the packet is always released when there is no handler or it can not be queued
void handler_packet_dispatch (
	Handler *handler, Packet *packet, const bool delete_packet,
	const char *owner_name, const char *handler_name
) {

	if (handler) {
		if (handler->direct_handle) {
			HandlerPacketValues values;
			handler_packet_values_start (&values, packet);

			handler->handler (packet);

			handler_stats_add (handler, &values);

			if (delete_packet) packet_delete (packet);
		}

		// added to the handler's job queue to be handled as soon as the handler is available
		else if (handler_push_packet (handler, packet)) {
			cerver_log_error (
				"Failed to push a new job to %s %s!", owner_name, handler_name
			);

			packet_delete (packet);
		}
	}

	else {
		cerver_log_warning ("%s does not have a %s!", owner_name, handler_name);

		packet_delete (packet);
	}

}

#pragma endregion

#pragma region handlers

const char *cerver_handler_error_to_string (
	const CerverHandlerError error
) {

	switch (error) {
		#define XX(num, name, string, description) case CERVER_HANDLER_ERROR_##name: return #string;
		CERVER_HANDLER_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_handler_error_to_string (CERVER_HANDLER_ERROR_NONE);

}

const char *cerver_handler_error_description (
	const CerverHandlerError error
) {

	switch (error) {
		#define XX(num, name, string, description) case CERVER_HANDLER_ERROR_##name: return #description;
		CERVER_HANDLER_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_handler_error_description (CERVER_HANDLER_ERROR_NONE);

}

// handles a packet of type PACKET_TYPE_CLIENT
static CerverHandlerError cerver_client_packet_handler (
	Packet *packet
) {

	CerverHandlerError error = CERVER_HANDLER_ERROR_NONE;

	switch (packet->header.request_type) {
		// the client is going to close its current connection
		// but will remain in the cerver if it has another connection active
		// if not, it will be dropped
		case CLIENT_PACKET_TYPE_CLOSE_CONNECTION: {
			#ifdef HANDLER_DEBUG
			cerver_log_debug (
				"Client %ld requested to close the connection",
				packet->client->id
			);
			#endif

			// check if the client is inside a lobby
			if (packet->lobby) {
				#ifdef HANDLER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_GAME,
					"Client %ld inside lobby %s wants to close the connection...",
					packet->client->id, packet->lobby->id->str
				);
				#endif

				// remove the player from the lobby
				(void) player_unregister_from_lobby (
					packet->lobby,
					player_get_by_sock_fd_list (
						packet->lobby, packet->connection->socket->sock_fd
					)
				);
			}

			switch (client_remove_connection_by_sock_fd (
				packet->cerver,
				packet->client, packet->connection->socket->sock_fd
			)) {
				case CLIENT_CONNECTIONS_STATUS_DROPPED:
					error = CERVER_HANDLER_ERROR_DROPPED;
					break;

				default: break;
			}
		} break;

		// the client is going to disconnect
		// and will close all of its active connections
		// so drop it from the server
		case CLIENT_PACKET_TYPE_DISCONNECT: {
			// check if the client is inside a lobby
			if (packet->lobby) {
				#ifdef HANDLER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_GAME,
					"Client %ld inside lobby %s wants to close the connection...",
					packet->client->id, packet->lobby->id->str
				);
				#endif

				// remove the player from the lobby
				(void) player_unregister_from_lobby (
					packet->lobby,
					player_get_by_sock_fd_list (
						packet->lobby, packet->connection->socket->sock_fd
					)
				);
			}

			client_drop (packet->cerver, packet->client);

			cerver_event_trigger (
				CERVER_EVENT_CLIENT_DISCONNECTED,
				packet->cerver,
				NULL, NULL
			);
		} break;

		default: {
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
				"Got an unknown client packet in cerver %s",
				packet->cerver->info->name
			);
			#endif
		} break;
	}

	return error;

}

// handles a packet of type PACKET_TYPE_CLIENT that was received outside the packet handler,
// like the ones that the load balancer receives
CerverHandlerError cerver_client_packet_handle (Packet *packet) {

	return packet ? cerver_client_packet_handler (packet) : CERVER_HANDLER_ERROR_PACKET;

}

static inline void cerver_request_get_file_actual (Packet *packet) {

	FileCerver *file_cerver = (FileCerver *) packet->cerver->cerver_data;

	file_cerver->stats->n_files_requests += 1;

	// get the necessary information to fulfil the request
	FileHeader requested = { { 0 }, 0 };
	if (file_header_get (packet, &requested)) {
		const FileHeader *file_header = &requested;

		// search for the requested file in the configured paths
		String *actual_filename = file_cerver_search_file (
			file_cerver, file_header->filename
		);

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
			CERVER_ERROR_GET_FILE, "Missing or invalid file header",
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
				packet->cerver->info->name
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
	FileHeader received = { { 0 }, 0 };
	if (file_header_get (packet, &received)) {
		FileHeader *file_header = &received;

		const char *file_data = NULL;
		size_t file_data_len = 0;

		// the file is sent after its packet, so its first bytes can be in the same buffer
		if (packet->connection) {
			file_data_len = receive_handle_take_trailing (
				&packet->connection->receive_handle, &file_data, (size_t) file_header->len
			);
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
			CERVER_ERROR_SEND_FILE, "Missing or invalid file header",
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
				packet->cerver->info->name
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

	switch (packet->header.request_type) {
		// request from a client to get a file
		case REQUEST_PACKET_TYPE_GET_FILE:
			cerver_request_get_file (packet);
			break;

		// request from a client to upload a file
		case REQUEST_PACKET_TYPE_SEND_FILE:
			cerver_request_send_file (packet);
			break;

		default: {
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_HANDLER,
				"Got an unknown request packet in cerver %s",
				packet->cerver->info->name
			);
			#endif
		} break;
	}

}

// sends back a test packet to the client!
void cerver_test_packet_handler (Packet *packet) {

	#ifdef HANDLER_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_PACKET,
		"Got a test packet in cerver %s.",
		packet->cerver->info->name
	);
	#endif

	Packet request = {
		.cerver = packet->cerver,
		.client = packet->client,
		.connection = packet->connection,
		.lobby = packet->lobby,

		.header = packet->header,

		// the header of the test packet is encoded again from here when it is sent
		.packet_size = PACKET_HEADER_SIZE,
		.packet = (void *) request.encoded_header,
		.packet_ref = true,

		.release = NULL,
		.release_args = NULL
	};

	// only the header goes back, a test packet with data must not announce it
	request.header.packet_size = PACKET_HEADER_SIZE;

	(void) packet_send (&request, 0, NULL, false);

}

// pushes the packet to the handler selected by its id
// a packet without a handler, or that could not be pushed, is released here
static void cerver_multiple_app_packet_handler (Packet *packet) {

	const u8 handler_id = packet->header.handler_id;

	Handler *handler = (handler_id < packet->cerver->n_handlers) ?
		packet->cerver->handlers[handler_id] : NULL;

	if (!handler) {
		cerver_log_warning (
			"Cerver %s does not have an app handler with id %u!",
			packet->cerver->info->name, (unsigned int) handler_id
		);

		packet_delete (packet);
	}

	else if (handler_push_packet (handler, packet)) {
		cerver_log_error (
			"Failed to push a new job to cerver's %s <%u> handler!",
			packet->cerver->info->name, (unsigned int) handler_id
		);

		packet_delete (packet);
	}

}

// passes the packet to the handler of its request type, set with cerver_set_request_handler ()
// returns true if the request type has a handler, false to use the one of its packet type
static bool cerver_request_type_packet_handler (Packet *packet, const bool delete_packet) {

	Handler *handler = cerver_get_request_handler (
		packet->cerver, packet->header.packet_type, packet->header.request_type
	);

	if (handler) {
		handler_packet_dispatch (
			handler, packet, delete_packet,
			packet->cerver->info->name, "request_handler"
		);
	}

	return (handler != NULL);

}

// handles an PACKET_TYPE_APP packet type
static void cerver_app_packet_handler (Packet *packet) {

	const bool delete_packet = packet->cerver->app_packet_handler_delete_packet;

	if (cerver_request_type_packet_handler (packet, delete_packet)) {
		// the handler of its request type has the packet
	}

	else if (packet->cerver->multiple_handlers) {
		cerver_multiple_app_packet_handler (packet);
	}

	else {
		handler_packet_dispatch (
			packet->cerver->app_packet_handler, packet, delete_packet,
			packet->cerver->info->name, "app_packet_handler"
		);
	}

}

// handles an PACKET_TYPE_APP_ERROR packet type
static void cerver_app_error_packet_handler (Packet *packet) {

	const bool delete_packet = packet->cerver->app_error_packet_handler_delete_packet;

	if (!cerver_request_type_packet_handler (packet, delete_packet)) {
		handler_packet_dispatch (
			packet->cerver->app_error_packet_handler, packet, delete_packet,
			packet->cerver->info->name, "app_error_packet_handler"
		);
	}

}

// handles a PACKET_TYPE_CUSTOM packet type
static void cerver_custom_packet_handler (Packet *packet) {

	const bool delete_packet = packet->cerver->custom_packet_handler_delete_packet;

	if (!cerver_request_type_packet_handler (packet, delete_packet)) {
		handler_packet_dispatch (
			packet->cerver->custom_packet_handler, packet, delete_packet,
			packet->cerver->info->name, "custom_packet_handler"
		);
	}

}

static const char *cerver_bad_packet_version = { "The packet has an incompatible version" };
static const char *cerver_bad_packet_type = { "The packet has an unknown type" };
static const char *cerver_already_authenticated = { "The connection is already authenticated" };

// a bad packet of an authenticated connection: its sender gets an error,
// and the connection is dropped when it reaches the cerver limit
// returns CERVER_HANDLER_ERROR_DROPPED when the connection is being dropped
// the address of an UDP peer that did not authenticate can be forged,
// so it is not answered, the answer could flood someone else
static bool cerver_bad_packet_answer_allowed (const Connection *connection) {

	return (connection->protocol != PROTOCOL_UDP) || connection_has_datagram_token (connection);

}

// an UDP peer does not have a socket, its peers remove it (udp.c)
static void cerver_bad_packet_drop (const Packet *packet) {

	if (packet->connection->protocol == PROTOCOL_UDP) {
		cerver_log_warning (
			"Cerver %s UDP peer %s:%u sent %u bad packets - dropping...",
			packet->cerver->info->name,
			packet->connection->ip, (unsigned int) packet->connection->port,
			(unsigned int) packet->connection->bad_packets
		);
	}

	else {
		cerver_log_warning (
			"Cerver %s connection <%d> sent %u bad packets - dropping...",
			packet->cerver->info->name, packet->connection->socket->sock_fd,
			(unsigned int) packet->connection->bad_packets
		);

		// the receive loop removes it, like when the client closes it
		(void) connection_drop_request (packet->connection);
	}

}

static CerverHandlerError cerver_bad_packet (const Packet *packet, const char *reason) {

	CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_bad_packets, 1);

	if (cerver_bad_packet_answer_allowed (packet->connection)) {
		(void) error_packet_generate_and_send (
			CERVER_ERROR_PACKET_ERROR, reason,
			packet->cerver, packet->client, packet->connection
		);
	}

	packet->connection->bad_packets += 1;

	const u8 max_bad_packets = packet->cerver->max_bad_packets;
	if (!max_bad_packets || (packet->connection->bad_packets < max_bad_packets)) {
		return CERVER_HANDLER_ERROR_NONE;
	}

	cerver_bad_packet_drop (packet);

	return CERVER_HANDLER_ERROR_DROPPED;

}

static CerverHandlerError cerver_packet_handler_check_version (
	Packet *packet
) {

	// the version came in the header, it used to be expected in the data,
	// and the check was inverted, so every valid packet was rejected
	// a rejected packet is handled by cerver_bad_packet ()
	return packet_check (packet) ? CERVER_HANDLER_ERROR_NONE : CERVER_HANDLER_ERROR_PACKET;

}

static CerverHandlerError cerver_packet_handler_actual (
	Packet *packet
) {

	CerverHandlerError error = CERVER_HANDLER_ERROR_NONE;

	switch (packet->header.packet_type) {
		// a cerver does not handle these packets from a client, they were leaked
		case PACKET_TYPE_NONE:
		case PACKET_TYPE_CERVER:
			packet_delete (packet);
			break;

		case PACKET_TYPE_CLIENT: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_client_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_client_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_client_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_client_packets += 1;
			error = cerver_client_packet_handler (packet);
			packet_delete (packet);
		} break;

		// handles an error from the client
		case PACKET_TYPE_ERROR: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_error_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_error_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_error_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_error_packets += 1;
			cerver_error_packet_handler (packet);
			packet_delete (packet);
		} break;

		// handles a request made from the client
		case PACKET_TYPE_REQUEST: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_request_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_request_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_request_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_request_packets += 1;
			cerver_request_packet_handler (packet);
			packet_delete (packet);
		} break;

		// handles authentication packets
		case PACKET_TYPE_AUTH: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_auth_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_auth_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_auth_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_auth_packets += 1;

			// the connection is already authenticated, the packet is answered and discarded
			(void) error_packet_generate_and_send (
				CERVER_ERROR_PACKET_ERROR, cerver_already_authenticated,
				packet->cerver, packet->client, packet->connection
			);

			packet_delete (packet);
		} break;

		// handles a game packet sent from the client
		case PACKET_TYPE_GAME: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_game_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_game_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_game_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_game_packets += 1;
			game_packet_handler (packet);
			// the game handlers only read it, it was leaked
			packet_delete (packet);
		} break;

		// user set handler to handle app specific packets
		case PACKET_TYPE_APP: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_app_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_app_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_app_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_app_packets += 1;
			cerver_app_packet_handler (packet);
		} break;

		// user set handler to handle app specific errors
		case PACKET_TYPE_APP_ERROR: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_app_error_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_app_error_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_app_error_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_app_error_packets += 1;
			cerver_app_error_packet_handler (packet);
		} break;

		// custom packet hanlder
		case PACKET_TYPE_CUSTOM: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_custom_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_custom_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_custom_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_custom_packets += 1;
			cerver_custom_packet_handler (packet);
		} break;

		// acknowledge the client we have received his test packet
		case PACKET_TYPE_TEST: {
			CERVER_STATS_ADD (packet->cerver->stats->received_packets->n_test_packets, 1);
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_test_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_test_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_test_packets += 1;
			cerver_test_packet_handler (packet);
			packet_delete (packet);
		} break;

		default: {
			#ifdef CLIENT_STATS
			packet->client->stats->received_packets->n_bad_packets += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->received_packets->n_bad_packets += 1;
			#endif
			if (packet->lobby) packet->lobby->stats->received_packets->n_bad_packets += 1;

			error = cerver_bad_packet (packet, cerver_bad_packet_type);
			packet_delete (packet);
		} break;
	}

	return error;

}

// handle packet based on type
static u8 cerver_packet_handler (Packet *packet) {

	u8 retval = 1;

	CerverHandlerError error = CERVER_HANDLER_ERROR_NONE;
	if (packet->cerver->check_packets) {
		if (!cerver_packet_handler_check_version (packet)) {
			error = cerver_packet_handler_actual (packet);
		}

		// a rejected packet is a bad one, it used to be leaked
		else {
			error = cerver_bad_packet (packet, cerver_bad_packet_version);
			packet_delete (packet);
		}
	}

	else {
		error = cerver_packet_handler_actual (packet);
	}

	switch (error) {
		case CERVER_HANDLER_ERROR_NONE:
			retval = 0;
			break;

		default: break;
	}

	return retval;

}

// handles the packet of a datagram of an UDP cerver, like the packets of a TCP connection
// returns 0 on success, 1 if the packet was bad or its peer must be removed
u8 cerver_udp_packet_handle (Packet *packet) {

	CERVER_STATS_ADD (packet->cerver->stats->client_n_packets_received, 1);
	CERVER_STATS_ADD (packet->cerver->stats->total_n_packets_received, 1);

	#ifdef CLIENT_STATS
	packet->client->stats->n_packets_received += 1;
	#endif
	#ifdef CONNECTION_STATS
	packet->connection->stats->n_packets_received += 1;
	#endif

	return cerver_packet_handler (packet);

}

static u8 cerver_packet_select_handler (
	ReceiveHandle *receive_handle, Packet *packet
) {

	u8 retval = 1;

	switch (receive_handle->type) {
		case RECEIVE_TYPE_NONE: break;

		case RECEIVE_TYPE_NORMAL: {
			packet->cerver = receive_handle->cerver;
			packet->client = receive_handle->client;
			packet->connection = receive_handle->connection;

			CERVER_STATS_ADD (packet->cerver->stats->client_n_packets_received, 1);
			CERVER_STATS_ADD (packet->cerver->stats->total_n_packets_received, 1);

			#ifdef CLIENT_STATS
			packet->client->stats->n_packets_received += 1;
			#endif
			#ifdef CONNECTION_STATS
			packet->connection->stats->n_packets_received += 1;
			#endif

			if (packet->lobby) packet->lobby->stats->n_packets_received += 1;

			retval = cerver_packet_handler (packet);
		} break;

		case RECEIVE_TYPE_ON_HOLD: {
			packet->cerver = receive_handle->cerver;
			packet->connection = receive_handle->connection;

			CERVER_STATS_ADD (packet->cerver->stats->on_hold_n_packets_received, 1);
			packet->connection->stats->n_packets_received += 1;

			retval = on_hold_packet_handler (packet);
		} break;

		case RECEIVE_TYPE_ADMIN: {
			packet->cerver = receive_handle->cerver;
			packet->connection = receive_handle->connection;
			packet->client = receive_handle->admin->client;

			packet->cerver->admin->stats->total_n_packets_received += 1;

			receive_handle->admin->client->stats->n_packets_received += 1;

			packet->connection->stats->n_packets_received += 1;

			retval = admin_packet_handler (packet);
		} break;

		default: break;
	}

	return retval;

}

#pragma endregion

#pragma region receive

u8 cerver_poll_unregister_sock_fd (Cerver *cerver, const i32 sock_fd);

static FreeList cerver_receive_recycled = FREELIST_INITIALIZER (
	CerverReceive, recycle_next, CERVER_RECEIVE_RECYCLE_LIMIT
);

// the recycled receives are released when the library is unloaded
__attribute__ ((destructor)) static void cerver_receive_recycled_drain (void) {

	freelist_drain (&cerver_receive_recycled, free);

}

CerverReceive *cerver_receive_new (void) {

	CerverReceive *cr = (CerverReceive *) freelist_pop (&cerver_receive_recycled);
	if (!cr) cr = (CerverReceive *) malloc (sizeof (CerverReceive));

	if (cr) {
		cr->type = RECEIVE_TYPE_NONE;

		cr->cerver = NULL;

		cr->socket = NULL;
		cr->connection = NULL;
		cr->client = NULL;
		cr->admin = NULL;

		cr->lobby = NULL;

		cr->recycle_next = NULL;
	}

	return cr;

}

// releases the reference that the receive holds to its socket
// and keeps the receive to be reused
void cerver_receive_delete (void *ptr) {

	if (ptr) {
		CerverReceive *cr = (CerverReceive *) ptr;

		socket_delete (cr->socket);

		if (freelist_push (&cerver_receive_recycled, cr)) free (cr);
	}

}

static inline void cerver_receive_create_normal (
	CerverReceive *cr,
	Cerver *cerver, const i32 sock_fd
) {

	cr->client = client_get_by_sock_fd (cerver, sock_fd);
	if (cr->client) {
		cr->connection = connection_get_by_sock_fd_from_client (cr->client, sock_fd);
		if (cr->connection) {
			cr->socket = socket_reference (cr->connection->socket);
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

static inline void cerver_receive_create_on_hold (
	CerverReceive *cr,
	Cerver *cerver, const i32 sock_fd
) {

	cr->connection = connection_get_by_sock_fd_from_on_hold (cerver, sock_fd);
	if (cr->connection) {
		cr->socket = socket_reference (cr->connection->socket);
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

static inline void cerver_receive_create_admin (
	CerverReceive *cr,
	Cerver *cerver, const i32 sock_fd
) {

	cr->admin = admin_get_by_sock_fd (cerver->admin, sock_fd);
	if (cr->admin) {
		cr->client = cr->admin->client;
		cr->connection = connection_get_by_sock_fd_from_client (cr->client, sock_fd);
		if (cr->connection) {
			cr->socket = socket_reference (cr->connection->socket);
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

CerverReceive *cerver_receive_create (
	ReceiveType receive_type,
	Cerver *cerver, const i32 sock_fd
) {

	CerverReceive *cr = cerver_receive_new ();
	if (cr) {
		cr->type = receive_type;

		cr->cerver = cerver;

		switch (cr->type) {
			case RECEIVE_TYPE_NONE: break;

			case RECEIVE_TYPE_NORMAL:
				cerver_receive_create_normal (cr, cerver, sock_fd);
				break;

			case RECEIVE_TYPE_ON_HOLD:
				cerver_receive_create_on_hold (cr, cerver, sock_fd);
				break;

			case RECEIVE_TYPE_ADMIN:
				cerver_receive_create_admin (cr, cerver, sock_fd);
				break;

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

		cr->socket = connection ? socket_reference (connection->socket) : NULL;
		cr->connection = connection;
		cr->client = client;
	}

	return cr;

}

// default cerver receive handler
// assembles packets from the received buffers, no matter where the buffers are split
void cerver_receive_handle_buffer (
	void *receive_handle_ptr
) {

	ReceiveHandle *receive_handle = (ReceiveHandle *) receive_handle_ptr;

	receive_handle_buffer (
		receive_handle,
		receive_handle->cerver->max_received_packet_size,
		cerver_packet_select_handler
	);

}

// handles a failed receive from a connection associatd with a client
// ends the connection to prevent seg faults or signals for bad sock fd
void cerver_receive_handle_failed (CerverReceive *cr) {

	if (cr->socket) {
		(void) pthread_mutex_lock (cr->socket->read_mutex);

		// 0 is a valid fd, a closed socket has -1
		if (cr->socket->sock_fd > -1) {
			switch (cr->type) {
				case RECEIVE_TYPE_NORMAL: {
					// check if the socket belongs to a player inside a lobby
					if (cr->lobby) {
						if (cr->lobby->players->size > 0) {
							(void) player_unregister_from_lobby (
								cr->lobby,
								player_get_by_sock_fd_list (
									cr->lobby, cr->socket->sock_fd
								)
							);
						}
					}

					(void) client_remove_connection_by_sock_fd (
						cr->cerver, cr->client, cr->socket->sock_fd
					);
				} break;

				case RECEIVE_TYPE_ON_HOLD: {
					on_hold_connection_drop (cr->cerver, cr->connection);
				} break;

				case RECEIVE_TYPE_ADMIN: {
					(void) admin_remove_connection_by_sock_fd (
						cr->cerver->admin, cr->admin, cr->socket->sock_fd
					);
				} break;

				default: break;
			}
		}

		(void) pthread_mutex_unlock (cr->socket->read_mutex);
	}

}

static inline void cerver_receive_success_receive_handle (
	CerverReceive *cr,
	const size_t received,
	char *packet_buffer, const size_t packet_buffer_size
) {

	ReceiveHandle *receive_handle = &cr->connection->receive_handle;
	receive_handle->type = cr->type;

	receive_handle->cerver = cr->cerver;

	receive_handle->socket = cr->socket;
	receive_handle->connection = cr->connection;
	receive_handle->client = cr->client;
	receive_handle->admin = cr->admin;

	receive_handle->lobby = cr->lobby;

	receive_handle->buffer = packet_buffer;
	receive_handle->buffer_size = packet_buffer_size;
	receive_handle->received_size = received;

	if (receive_handle->state == RECEIVE_HANDLE_STATE_NONE) {
		receive_handle->state = RECEIVE_HANDLE_STATE_NORMAL;
	}

	cr->cerver->handle_received_buffer (receive_handle);

}

static void cerver_receive_success (
	CerverReceive *cr,
	const size_t received,
	char *packet_buffer, const size_t packet_buffer_size
) {

	cr->socket->packet_buffer_size = received;

	CERVER_STATS_ADD (cr->cerver->stats->total_n_receives_done, 1);
	CERVER_STATS_ADD (cr->cerver->stats->total_bytes_received, received);

	if (cr->lobby) {
		cr->lobby->stats->n_receives_done += 1;
		cr->lobby->stats->bytes_received += received;
	}

	switch (cr->type) {
		case RECEIVE_TYPE_NORMAL: {
			CERVER_STATS_ADD (cr->cerver->stats->client_receives_done, 1);
			CERVER_STATS_ADD (cr->cerver->stats->client_bytes_received, received);

			// read by the inactive clients thread
			__atomic_store_n (&cr->client->last_activity, time (NULL), __ATOMIC_RELAXED);

			#ifdef CLIENT_STATS
			cr->client->stats->n_receives_done += 1;
			cr->client->stats->total_bytes_received += received;
			#endif

			#ifdef CONNECTION_STATS
			cr->connection->stats->n_receives_done += 1;
			cr->connection->stats->total_bytes_received += received;
			#endif
		} break;

		case RECEIVE_TYPE_ON_HOLD: {
			CERVER_STATS_ADD (cr->cerver->stats->on_hold_receives_done, 1);
			CERVER_STATS_ADD (cr->cerver->stats->on_hold_bytes_received, received);

			cr->connection->stats->n_receives_done += 1;
			cr->connection->stats->total_bytes_received += received;
		} break;

		case RECEIVE_TYPE_ADMIN: {
			cr->cerver->admin->stats->total_n_receives_done += 1;
			cr->cerver->admin->stats->total_bytes_received += received;

			#ifdef CLIENT_STATS
			cr->client->stats->n_receives_done += 1;
			cr->client->stats->total_bytes_received += received;
			#endif

			#ifdef CONNECTION_STATS
			cr->connection->stats->n_receives_done += 1;
			cr->connection->stats->total_bytes_received += received;
			#endif
		} break;

		default: break;
	}

	cerver_receive_success_receive_handle (
		cr,
		received,
		packet_buffer, packet_buffer_size
	);

}

void cerver_receive_internal (
	CerverReceive *cr,
	char *packet_buffer, const size_t packet_buffer_size
) {

	ssize_t rc = recv (
		cr->socket->sock_fd,
		packet_buffer, packet_buffer_size,
		0
	);

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

				perror ("Error ");
				#endif

				cerver_receive_handle_failed (cr);
			}
		} break;

		case 0: {
			// man recv -> steam socket perfomed an orderly shutdown
			// but in dgram it might mean something?
			#ifdef HANDLER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"cerver_recieve () - rc == 0 - sock fd: %d",
				cr->socket->sock_fd
			);

			// perror ("Error ");
			#endif

			cerver_receive_handle_failed (cr);
		} break;

		default: {
			#ifdef RECEIVE_DEBUG
			cerver_log_debug (
				"recv () - %ld bytes from %d sock fd",
				rc, cr->socket->sock_fd
			);
			#endif

			cerver_receive_success (
				cr, rc,
				packet_buffer, packet_buffer_size
			);
		} break;
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
				#ifdef SOCKET_DEBUG
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
			cerver_receive_success (
				cr, (size_t) rc,
				buffer, buffer_size
			);

			retval = 0;
		} break;
	}

	return retval;

}

// the load balancer receives complete packets and routes them to its services,
// the route of the connection lives while it is received, a connection without one is not received
// a packet can end the connection, so it is never used after the receive loop
static void cerver_receive_threads_balancer (CerverReceive *cr) {

	Balancer *balancer = cr->cerver->balancer;

	const u32 route_id = balancer_connection_start (balancer, cr->socket);
	if (cr->connection) cr->connection->route_id = route_id;

	while (
		route_id
		&& (cr->socket->sock_fd > -1)
		&& cerver_is_running (cr->cerver)
		&& !balancer_receive (cr)
	);

	balancer_connection_end (balancer, route_id);

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
	(void) sock_set_timeout (sock_fd, CERVER_DEFAULT_SOCKET_RECV_TIMEOUT);

	const size_t buffer_size = cr->cerver->receive_buffer_size;
	char *buffer = (char *) calloc (buffer_size, sizeof (char));
	if (cr->cerver->type == CERVER_TYPE_BALANCER) {
		free (buffer);
		cerver_receive_threads_balancer (cr);
	}

	else if (buffer) {
		while (
			(cr->socket->sock_fd > -1)
			&& cerver_is_running (cr->cerver)
			&& !cerver_receive_threads_actual (cr, buffer, buffer_size)
		);

		free (buffer);
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_HANDLER,
			"cerver_receive_threads () - "
			"Failed to allocate packet buffer for sock fd <%d> connection!",
			cr->connection->socket->sock_fd
		);
	}

	// check if the connection has already ended
	if (cr->socket->sock_fd > -1) {
		client_remove_connection_by_sock_fd (
			cr->cerver, cr->client, cr->socket->sock_fd
		);
	}

	cerver_receive_delete (cr);

	#ifdef HANDLER_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
		"cerver_receive_threads () - loop has ended - "
		"dropping sock fd <%d> connection...",
		sock_fd
	);
	#endif

	return NULL;

}

static void cerver_receive_threads_work (void *cerver_receive_ptr) {

	(void) cerver_receive_threads (cerver_receive_ptr);

}

// a client connection that the cerver can not handle
// (the thpool is full or the cerver is being drained)
static void cerver_receive_threads_reject (void *cerver_receive_ptr) {

	CerverReceive *cr = (CerverReceive *) cerver_receive_ptr;

	CERVER_STATS_ADD (cr->cerver->stats->total_rejected_connections, 1);

	if (cr->socket->sock_fd > -1) {
		client_remove_connection_by_sock_fd (
			cr->cerver, cr->client, cr->socket->sock_fd
		);
	}

	cerver_receive_delete (cr);

}

#pragma endregion

#pragma region accept

// every connection owns its socket, they are never reused,
// so a thread that keeps an old socket can never use the fd of a new connection
static Connection *cerver_connection_create (
	Cerver *cerver,
	const i32 new_fd, const struct sockaddr_storage *client_address
) {

	Connection *connection = connection_create (
		new_fd, client_address, cerver->protocol
	);

	connection_set_send_timeout (connection, cerver->send_timeout);

	return connection;

}

// if the cerver requires auth, we put the connection on hold
static u8 cerver_register_new_connection_auth_required (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	// the on hold poll owns the connection as soon as it is registered,
	// it can drop it at any time, so it is prepared before
	connection->active = true;

	cerver_info_send_info_packet (cerver, NULL, connection);

	cerver_event_trigger (
		CERVER_EVENT_ON_HOLD_CONNECTED,
		cerver,
		NULL, connection
	);

	if (!on_hold_connection (cerver, connection)) {
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Connection is on hold on cerver %s!",
			cerver->info->name
		);
		#endif

		CERVER_STATS_ADD (cerver->stats->total_on_hold_connections, 1);

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to put connection on hold in cerver %s",
			cerver->info->name
		);
	}

	return retval;

}

#ifdef HANDLER_DEBUG
static void cerver_register_new_connection_log_queued (
	const Cerver *cerver, const Connection *connection
) {

	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
		"Cerver %s queued sock fd <%d> connection - %u working - %u queued",
		cerver->info->name, connection->socket->sock_fd,
		thpool_get_num_threads_working (cerver->thpool),
		thpool_get_queued (cerver->thpool)
	);

}
#endif


static u8 cerver_register_new_connection_normal_default_create_detachable (
	CerverReceive *cr
) {

	u8 retval = 1;

	// counted in the cerver, so its drain waits for the thread
	if (!thread_group_create (
		&cr->cerver->connection_threads,
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

// a client connection is queued in the thpool,
// when the queue is full, the connection is removed from its client
static u8 cerver_register_new_connection_normal_default_thpool (
	Cerver *cerver, CerverReceive *cr
) {

	if (!thpool_add_work_with_reject (
		cerver->thpool,
		cerver_receive_threads_work, cerver_receive_threads_reject,
		cr
	)) {
		#ifdef HANDLER_DEBUG
		cerver_register_new_connection_log_queued (cerver, cr->connection);
		#endif
	}

	else {
		cerver_log_warning (
			"Cerver %s thpool is full - rejected sock fd <%d> connection",
			cerver->info->name, cr->connection->socket->sock_fd
		);

		cerver_receive_threads_reject (cr);
	}

	// the connection was handled either way
	return 0;

}

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
		if (cerver->handle_detachable_threads || !cerver->thpool) {
			retval = cerver_register_new_connection_normal_default_create_detachable (cr);
		}

		else {
			retval = cerver_register_new_connection_normal_default_thpool (cerver, cr);
		}
	}

	return retval;

}

// a new connection that no handler could take is taken out of the cerver with its new client,
// the client is unregistered while it still has the connection, so its sock fd leaves the maps,
// and the connection is left for its caller to drop
void cerver_new_connection_release (
	Cerver *cerver, Client *client, Connection *connection
) {

	cerver_log_error (
		"Cerver %s failed to start the handler of the connection with sock fd <%d>",
		cerver->info->name, connection->socket->sock_fd
	);

	(void) client_unregister_from_cerver (cerver, client);
	(void) client_connection_remove (client, connection);
	client_delete (client);

}

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

			// the event runs before the handler owns the connection,
			// the receive thread can delete it as soon as it starts
			cerver_event_trigger (
				CERVER_EVENT_CLIENT_CONNECTED,
				cerver,
				client, connection
			);

			// a connection that no handler took is dropped by the caller, without its client
			retval = cerver_register_new_connection_normal_default_select_handler (
				cerver, client, connection
			);

			if (retval) cerver_new_connection_release (cerver, client, connection);
		}

		// the poll is full or it failed, the caller drops the connection
		else {
			(void) client_connection_remove (client, connection);
			client_delete (client);
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

	return cerver_register_new_connection_normal_default (
		cerver, connection
	);

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
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CLIENT,
			"New connection from IP address: %s -- Port: %d",
			connection->ip, connection->port
		);
		#endif

		// before the dispatch, the worker may delete the connection right after it
		#ifdef HANDLER_DEBUG
		cerver_log_with_date (
			LOG_TYPE_DEBUG, LOG_TYPE_HANDLER,
			"cerver_register_new_connection () - accepted fd %d - %s:%u",
			new_fd, connection->ip, (unsigned int) connection->port
		);
		#endif

		connection->active = true;

		if (!cerver_register_new_connection_select (cerver, connection)) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
				"New connection to cerver %s!", cerver->info->name
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
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CLIENT,
			"cerver_register_new_connection () - failed to create a new connection for fd %d!",
			new_fd
		);

		(void) close (new_fd);
	}

}

// the connections that the thpool handles
// wait for space when the overload policy is CERVER_OVERLOAD_POLICY_WAIT
static bool cerver_accept_uses_thpool (const Cerver *cerver) {

	bool retval = false;

	if (cerver->thpool) {
		retval = (cerver->handler_type == CERVER_HANDLER_TYPE_THREADS)
			&& !cerver->handle_detachable_threads
			&& !cerver->auth_required;
	}

	return retval;

}

// sleeps for the interval, returns false if a signal interrupted it
static bool cerver_accept_sleep (const long nanoseconds) {

	struct timespec interval = { 0, nanoseconds };

	return !(nanosleep (&interval, NULL) && (errno == EINTR));

}

// with CERVER_OVERLOAD_POLICY_WAIT, new connections are not accepted
// until the thpool has space for them, so they wait in the listen backlog
// the thpool wakes up the wait as soon as a job is taken,
// and a signal that interrupts it stops the cerver, like an interrupted accept ()
static void cerver_accept_wait_space (Cerver *cerver) {

	if (
		(cerver->overload_policy == CERVER_OVERLOAD_POLICY_WAIT)
		&& cerver_accept_uses_thpool (cerver)
	) {
		int result = 1;
		while (cerver_is_running (cerver) && result) {
			result = thpool_wait_space (cerver->thpool, CERVER_OVERLOAD_WAIT_TIMEOUT);

			if ((result == -1) && (errno == EINTR)) {
				cerver_log_warning (
					"Cerver %s was interrupted by a signal - stopping...",
					cerver->info->name
				);

				cerver_stop_request (cerver);
			}
		}
	}

}

static pthread_mutex_t handler_reserve_fd_mutex = PTHREAD_MUTEX_INITIALIZER;
static int handler_reserve_fd = -1;

// keeps a fd open, so a connection can still be accepted and closed
// when the process runs out of fds, instead of leaving it in the backlog
static void cerver_accept_reserve_fd_open (void) {

	(void) pthread_mutex_lock (&handler_reserve_fd_mutex);

	if (handler_reserve_fd < 0) {
		handler_reserve_fd = open (HANDLER_RESERVE_FD_PATH, O_RDONLY | O_CLOEXEC);
	}

	(void) pthread_mutex_unlock (&handler_reserve_fd_mutex);

}

// without fds, the pending connection is accepted with the reserve fd and closed,
// then the cerver waits a bit, otherwise the listening socket is always readable
// and accept () fails in a busy loop
static void cerver_accept_out_of_fds (Cerver *cerver, const int error) {

	(void) pthread_mutex_lock (&handler_reserve_fd_mutex);

	if (handler_reserve_fd > -1) {
		(void) close (handler_reserve_fd);
		handler_reserve_fd = -1;

		int fd = accept (cerver->sock, NULL, NULL);
		if (fd > -1) (void) close (fd);
	}

	(void) pthread_mutex_unlock (&handler_reserve_fd_mutex);

	CERVER_STATS_ADD (cerver->stats->total_rejected_connections, 1);

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_ERROR, LOG_TYPE_CERVER,
		"cerver_accept () - cerver %s is out of fds - errno %d (%s) - rejected a connection",
		cerver->info->name, error,
		strerror_r (error, error_description, sizeof (error_description))
	);

	(void) cerver_accept_sleep (HANDLER_ACCEPT_BACKOFF);

	cerver_accept_reserve_fd_open ();

}

static void cerver_accept_failed_unknown (Cerver *cerver, const int error) {

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_with_date (
		LOG_TYPE_ERROR, LOG_TYPE_CERVER,
		"cerver_accept () - accept () failed in cerver %s - errno %d (%s)",
		cerver->info->name, error,
		strerror_r (error, error_description, sizeof (error_description))
	);

	(void) cerver_accept_sleep (HANDLER_ACCEPT_BACKOFF);

}

static void cerver_accept_failed (Cerver *cerver, const int error) {

	switch (error) {
		// a signal stops the cerver,
		// so its handler can run when cerver_start () returns
		case EINTR: {
			cerver_log_warning (
				"Cerver %s accept () was interrupted by a signal - stopping...",
				cerver->info->name
			);

			cerver_stop_request (cerver);
		} break;

		// the listening socket is non blocking in CERVER_HANDLER_TYPE_POLL
		case EAGAIN:
		#if EAGAIN != EWOULDBLOCK
		case EWOULDBLOCK:
		#endif
			break;

		// the client went away before its connection was accepted
		case ECONNABORTED:
		case EPROTO:
			break;

		case EMFILE:
		case ENFILE:
		case ENOBUFS:
		case ENOMEM:
			cerver_accept_out_of_fds (cerver, error);
			break;

		default: {
			// the listening socket was shutdown by cerver_stop_request ()
			if (cerver_is_running (cerver)) {
				cerver_accept_failed_unknown (cerver, error);
			}
		} break;
	}

}

// every accepted connection is blocking, it has a send timeout
// and its fd is not inherited by child processes
static void cerver_accept_configure_fd (const Cerver *cerver, const i32 new_fd) {

	(void) sock_set_blocking (new_fd, true);

	(void) fcntl (new_fd, F_SETFD, FD_CLOEXEC);

	if (cerver->send_timeout) {
		(void) sock_set_send_timeout (new_fd, (time_t) cerver->send_timeout);
	}

}

// accepst a new connection to the cerver
static void cerver_accept (void *cerver_ptr) {

	Cerver *cerver = (Cerver *) cerver_ptr;

	cerver_accept_wait_space (cerver);

	if (cerver_is_running (cerver)) {
		struct sockaddr_storage client_address;
		(void) memset (&client_address, 0, sizeof (struct sockaddr_storage));
		socklen_t socklen = sizeof (struct sockaddr_storage);

		// accept the new connection
		i32 new_fd = accept (cerver->sock, (struct sockaddr *) &client_address, &socklen);
		if (new_fd > -1) {
			#ifdef HANDLER_DEBUG
			cerver_log_debug ("Accepted fd: %d", new_fd);
			#endif

			cerver_accept_configure_fd (cerver, new_fd);

			cerver_register_new_connection (cerver, new_fd, &client_address);
		}

		else {
			cerver_accept_failed (cerver, errno);
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

// the address with its zone and the port where the cerver accepts connections
static void cerver_log_ready (const Cerver *cerver) {

	char ip[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	(void) sock_ip_to_string_actual ((const struct sockaddr *) &cerver->address, ip);

	cerver_log (
		LOG_TYPE_SUCCESS, LOG_TYPE_CERVER,
		"Cerver %s is ready in %s port %d!",
		cerver->info->name, ip, cerver->port
	);

}

// reallocs main cerver poll fds
// returns 0 on success, 1 on error
u8 cerver_realloc_main_poll_fds (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		const u32 current_max = cerver->max_n_fds;
		const u32 new_max = current_max * HANDLER_POLL_FDS_GROWTH_FACTOR;

		// the current array is kept if the realloc fails
		struct pollfd *fds = (struct pollfd *) realloc (
			cerver->fds, new_max * sizeof (struct pollfd)
		);

		if (fds) {
			for (u32 i = current_max; i < new_max; i++) {
				fds[i].fd = -1;
				fds[i].events = 0;
				fds[i].revents = 0;
			}

			cerver->fds = fds;
			cerver->max_n_fds = new_max;

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
	if (idx > -1) {
		cerver->fds[idx].fd = connection->socket->sock_fd;
		cerver->fds[idx].events = POLLIN;
		cerver->fds[idx].revents = 0;
		cerver->current_n_fds++;

		CERVER_STATS_ADD (cerver->stats->current_active_client_connections, 1);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Added sock fd <%d> to cerver %s MAIN poll, idx: %i",
			connection->socket->sock_fd, cerver->info->name, idx
		);
		#endif

		#ifdef CERVER_STATS
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Cerver %s current active connections: %ld",
			cerver->info->name,
			cerver->stats->current_active_client_connections
		);
		#endif

		retval = 0;
	}

	return retval;

}

// a connection beyond the max of cerver_set_poll_fds () is rejected,
// called with the poll lock
static bool cerver_poll_reached_max (Cerver *cerver) {

	bool reached = false;

	if (
		cerver->poll_max_connections
		&& (CERVER_STATS_GET (cerver->stats->current_active_client_connections) >= cerver->poll_max_connections)
	) {
		CERVER_STATS_ADD (cerver->stats->total_rejected_connections, 1);

		cerver_log_warning (
			"Cerver %s main poll has its max of %u connections - rejected a connection",
			cerver->info->name, cerver->poll_max_connections
		);

		reached = true;
	}

	return reached;

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

		if (cerver_poll_reached_max (cerver)) {
			retval = 1;
		}

		else if (!cerver_poll_register_connection_internal (
			cerver, connection
		)) {
			retval = 0;
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"Cerver %s main poll is full -- we need to realloc...",
				cerver->info->name
			);
			#endif

			if (cerver_realloc_main_poll_fds (cerver)) {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to realloc cerver %s main poll fds!",
					cerver->info->name
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
		if (idx > -1) {
			cerver->fds[idx].fd = -1;
			cerver->fds[idx].events = 0;
			cerver->fds[idx].revents = 0;
			cerver->current_n_fds--;

			CERVER_STATS_SUBTRACT (cerver->stats->current_active_client_connections, 1);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
				"Removed sock fd <%d> from cerver %s MAIN poll, idx: %d",
				sock_fd, cerver->info->name, idx
			);
			#endif

			#ifdef CERVER_STATS
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_NONE,
				"Cerver %s current active connections: %ld",
				cerver->info->name,
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
				sock_fd, cerver->info->name
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

// the poll thread accepts the connection itself,
// so it is the only thread that adds fds to the poll array
static inline void cerver_poll_handle_actual_accept (
	Cerver *cerver, const short revents
) {

	if (revents & POLLIN) {
		cerver_accept (cerver);
	}

}

static inline void cerver_poll_handle_actual_receive (
	Cerver *cerver,
	const i32 sock_fd, const short revents,
	char *packet_buffer
) {

	CerverReceive *cr = cerver_receive_create (
		RECEIVE_TYPE_NORMAL, cerver, sock_fd
	);

	if (cr) {
		// the data that arrived is read first, even if the connection was also closed
		if (revents & POLLIN) {
			cerver_receive_internal (
				cr,
				packet_buffer, cerver->receive_buffer_size
			);
		}

		// the other end closed the connection, or an error occurred
		else if (revents & (POLLHUP | POLLERR | POLLNVAL)) {
			cerver_receive_handle_failed (cr);
		}

		cerver_receive_delete (cr);
	}

}

// the poll thread polls its own copy of the registered fds,
// because other threads register connections (and realloc the array) while it polls
// returns the copy, that grows with the registered fds, or NULL on error
static struct pollfd *cerver_poll_copy_fds (
	Cerver *cerver, struct pollfd *polled_fds, u32 *polled_size
) {

	(void) pthread_mutex_lock (cerver->poll_lock);

	if (cerver->max_n_fds > *polled_size) {
		struct pollfd *bigger = (struct pollfd *) realloc (
			polled_fds, sizeof (struct pollfd) * cerver->max_n_fds
		);

		if (bigger) {
			polled_fds = bigger;
			*polled_size = cerver->max_n_fds;
		}
	}

	const u32 count = (cerver->max_n_fds < *polled_size) ? cerver->max_n_fds : *polled_size;
	(void) memcpy (polled_fds, cerver->fds, sizeof (struct pollfd) * count);

	for (u32 idx = count; idx < *polled_size; idx++) {
		polled_fds[idx].fd = -1;
		polled_fds[idx].revents = 0;
	}

	(void) pthread_mutex_unlock (cerver->poll_lock);

	return polled_fds;

}

// checks that the polled fd is still registered in the same slot
static bool cerver_poll_is_registered (
	Cerver *cerver, const u32 idx, const i32 sock_fd
) {

	(void) pthread_mutex_lock (cerver->poll_lock);

	const bool registered = (idx < cerver->max_n_fds) && (cerver->fds[idx].fd == sock_fd);

	(void) pthread_mutex_unlock (cerver->poll_lock);

	return registered;

}

static inline void cerver_poll_handle (
	Cerver *cerver, char *packet_buffer,
	const struct pollfd *polled_fds, const u32 polled_size
) {

	// one or more fd(s) are readable, need to determine which ones they are
	for (u32 idx = 0; idx < polled_size; idx++) {
		const i32 sock_fd = polled_fds[idx].fd;
		const short revents = polled_fds[idx].revents;

		if ((sock_fd > -1) && revents && cerver_poll_is_registered (cerver, idx, sock_fd)) {
			if (sock_fd == cerver->sock) {
				cerver_poll_handle_actual_accept (cerver, revents);
			}

			else {
				cerver_poll_handle_actual_receive (
					cerver, sock_fd, revents, packet_buffer
				);
			}
		}
	}

}

static void cerver_poll_failed (Cerver *cerver, const int error) {

	// a signal stops the cerver, so its handler can run when cerver_start () returns
	if (error == EINTR) {
		cerver_log_warning (
			"Cerver %s poll () was interrupted by a signal - stopping...",
			cerver->info->name
		);
	}

	else {
		char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_CERVER,
			"Cerver %s main poll has failed - errno %d (%s)",
			cerver->info->name, error,
			strerror_r (error, error_description, sizeof (error_description))
		);
	}

	cerver_stop_request (cerver);

}

// cerver poll loop to handle events in the registered socket's fds
u8 cerver_poll (Cerver *cerver) {

	u8 retval = 1;

	if (cerver) {
		cerver_log_ready (cerver);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Waiting for connections..."
		);
		#endif

		char *packet_buffer = (char *) calloc (
			cerver->receive_buffer_size, sizeof (char)
		);

		struct pollfd *polled_fds = NULL;
		u32 polled_size = 0;

		if (packet_buffer) {
			cerver_accept_reserve_fd_open ();

			int poll_retval = 0;
			while (cerver_is_running (cerver)) {
				polled_fds = cerver_poll_copy_fds (cerver, polled_fds, &polled_size);

				poll_retval = poll (
					polled_fds,
					polled_size,
					cerver->poll_timeout
				);

				switch (poll_retval) {
					case -1: {
						cerver_poll_failed (cerver, errno);
					} break;

					case 0: {
						// #ifdef CERVER_DEBUG
						// cerver_log_debug (
						// 	"Cerver %s MAIN poll timeout",
						// 	cerver->info->name
						// );
						// #endif
					} break;

					default: {
						cerver_poll_handle (cerver, packet_buffer, polled_fds, polled_size);
					} break;
				}
			}

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_CERVER, LOG_TYPE_NONE,
				"Cerver %s main poll has stopped!",
				cerver->info->name
			);
			#endif

			free (polled_fds);
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
		cerver_log_ready (cerver);

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CERVER,
			"Waiting for connections..."
		);
		#endif

		cerver_accept_reserve_fd_open ();

		while (cerver_is_running (cerver)) {
			cerver_accept (cerver);
		}

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_CERVER, LOG_TYPE_NONE,
			"Cerver %s accept thread has stopped!",
			cerver->info->name
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
