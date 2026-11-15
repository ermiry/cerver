#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/client/events.h"
#include "cerver/client/internal.h"
#include "cerver/client/requests.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/files/files.h"
#include "cerver/files/internal.h"
#include "cerver/files/receive.h"
#include "cerver/files/send.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region requests

// when a client is already connected to the cerver, a request can be made to the cerver
// and the result will be returned
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
unsigned int client_request_to_cerver (
	Client *client, Connection *connection, Packet *request
) {

	unsigned int retval = 1;

	if (client && connection && request) {
		// send the request to the cerver
		packet_set_network_values (request, NULL, client, connection, NULL);

		size_t sent = 0;
		if (!packet_send (request, 0, &sent, false)) {
			// printf ("Request to cerver: %ld\n", sent);

			// receive the data directly
			client_connection_get_next_packet (client, connection);

			retval = 0;
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log_error (
				"client_request_to_cerver () - "
				"failed to send request packet!"
			);
			#endif
		}
	}

	return retval;

}

static void *client_request_to_cerver_thread (void *cc_ptr) {

	if (cc_ptr) {
		ClientConnection *cc = (ClientConnection *) cc_ptr;
		Client *client = cc->client;

		(void) client_connection_get_next_packet (
			cc->client, cc->connection
		);

		// the response was handled, the app can end the connection
		client_event_trigger (CLIENT_EVENT_CONNECTION_DATA, cc->client, cc->connection);

		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// when a client is already connected to the cerver, a request can be made to the cerver
// the result will be placed inside the connection
// this method will NOT block and the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// returns 0 on success request, 1 on error
unsigned int client_request_to_cerver_async (
	Client *client, Connection *connection, Packet *request
) {

	unsigned int retval = 1;

	if (client && connection && request) {
		// send the request to the cerver
		packet_set_network_values (request, NULL, client, connection, NULL);
		if (!packet_send (request, 0, NULL, false)) {
			ClientConnection *cc = client_connection_aux_new (client, connection);
			if (cc) {
				// create a new thread to receive & handle the response
				retval = client_async_thread_create (cc, client_request_to_cerver_thread);

				#ifdef CLIENT_DEBUG
				if (retval) cerver_log_error ("Failed to create client_request_to_cerver_thread () detachable thread!");
				#endif
			}
		}

		else {
			#ifdef CLIENT_DEBUG
			cerver_log_error (
				"client_request_to_cerver_async () - "
				"failed to send request packet!"
			);
			#endif
		}
	}

	return retval;

}

#pragma endregion

#pragma region files

u8 client_file_receive (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
) {

	u8 retval = 1;

	// generate a custom filename taking into account the uploads path
	*saved_filename = c_string_create (
		"%s/%ld-%s",
		client->uploads_path->str,
		time (NULL), file_header->filename
	);

	if (*saved_filename) {
		retval = file_receive_actual (
			client, connection,
			file_header,
			file_data, file_data_len,
			saved_filename
		);
	}

	return retval;

}

// adds a new file path to take into account when getting a request for a file
// returns 0 on success, 1 on error
u8 client_files_add_path (Client *client, const char *path) {

	u8 retval = 1;

	if (client && path) {
		if (client->n_paths < CLIENT_FILES_MAX_PATHS) {
			client->paths[client->n_paths] = str_new (path);
			client->n_paths += 1;

			// the path was added, it always returned an error before
			retval = 0;
		}
	}

	return retval;

}

// sets the default uploads path to be used when receiving a file
void client_files_set_uploads_path (
	Client *client, const char *uploads_path
) {

	if (client && uploads_path) {
		str_delete (client->uploads_path);
		client->uploads_path = str_new (uploads_path);
	}

}

// sets a custom method to be used to handle a file upload (receive)
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
void client_files_set_file_upload_handler (
	Client *client,
	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
) {

	if (client) {
		client->file_upload_handler = file_upload_handler;
	}

}

// sets a callback to be executed after a file has been successfully received
void client_files_set_file_upload_cb (
	Client *client,
	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	)
) {

	if (client) {
		client->file_upload_cb = file_upload_cb;
	}

}

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
String *client_files_search_file (
	Client *client, const char *filename
) {

	String *retval = NULL;

	if (client && filename) {
		char filename_query[FILENAME_DEFAULT_SIZE * 2] = { 0 };
		for (unsigned int i = 0; i < client->n_paths; i++) {
			(void) snprintf (
				filename_query, FILENAME_DEFAULT_SIZE * 2,
				"%s/%s",
				client->paths[i]->str, filename
			);

			if (file_exists (filename_query)) {
				retval = str_new (filename_query);
				break;
			}
		}
	}

	return retval;

}

// requests a file from the cerver
// the client's uploads_path should have been configured before calling this method
// returns 0 on success sending request, 1 on failed to send request
u8 client_file_get (
	Client *client, Connection *connection,
	const char *filename
) {

	u8 retval = 1;

	if (client && connection && filename) {
		if (client->uploads_path) {
			Packet *packet = file_header_packet_create (
				REQUEST_PACKET_TYPE_GET_FILE, filename, 0
			);

			if (packet) {
				packet_set_network_values (packet, NULL, client, connection, NULL);

				retval = packet_send (packet, 0, NULL, false);

				packet_delete (packet);
			}
		}
	}

	return retval;

}

// sends a file to the cerver
// returns 0 on success sending request, 1 on failed to send request
u8 client_file_send (
	Client *client, Connection *connection,
	const char *filename
) {

	u8 retval = 1;

	if (client && connection && filename) {
		char *last = strrchr ((char *) filename, '/');
		const char *actual_filename = last ? last + 1 : NULL;
		if (actual_filename) {
			// try to open the file
			struct stat filestatus = CERVER_ZERO_INITIALIZER;
			int file_fd = file_open_as_fd (filename, &filestatus, O_RDONLY);
			if (file_fd >= 0) {
				size_t sent = file_send_with_fd (
					NULL, client, connection,
					file_fd, actual_filename, filestatus.st_size
				);

				client->file_stats->n_files_sent += 1;
				client->file_stats->n_bytes_sent += sent;

				if (sent == (size_t) filestatus.st_size) retval = 0;

				close (file_fd);
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_FILE,
					"client_file_send () - Failed to open file %s", filename
				);
			}
		}

		else {
			cerver_log_error ("client_file_send () - failed to get actual filename");
		}
	}

	return retval;

}

#pragma endregion
