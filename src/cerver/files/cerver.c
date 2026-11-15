#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/files/cerver.h"
#include "cerver/files/files.h"
#include "cerver/files/internal.h"
#include "cerver/files/receive.h"
#include "cerver/files/send.h"
#include "cerver/handler/errors.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region cerver

static FileCerverStats *file_cerver_stats_new (void) {

	FileCerverStats *stats = (FileCerverStats *) malloc (sizeof (FileCerverStats));
	if (stats) {
		stats->n_files_requests = 0;
		stats->n_success_files_requests = 0;
		stats->n_bad_files_requests = 0;
		stats->n_files_sent = 0;
		stats->n_bad_files_sent = 0;
		stats->n_bytes_sent = 0;

		stats->n_files_upload_requests = 0;
		stats->n_success_files_uploaded = 0;
		stats->n_bad_files_upload_requests = 0;
		stats->n_bad_files_received = 0;
		stats->n_bytes_received = 0;
	}

	return stats;

}

static void file_cerver_stats_delete (FileCerverStats *stats) {

	if (stats) free (stats);

}

FileCerver *file_cerver_new (void) {

	FileCerver *file_cerver = (FileCerver *) malloc (sizeof (FileCerver));
	if (file_cerver) {
		file_cerver->cerver = NULL;

		file_cerver->n_paths = 0;
		for (unsigned int i = 0; i < FILE_CERVER_MAX_PATHS; i++)
			file_cerver->paths[i] = NULL;

		file_cerver->uploads_path = NULL;

		file_cerver->file_upload_handler = file_cerver_receive;

		file_cerver->file_upload_cb = NULL;

		file_cerver->stats = NULL;
	}

	return file_cerver;

}

void file_cerver_delete (void *file_cerver_ptr) {

	if (file_cerver_ptr) {
		FileCerver *file_cerver = (FileCerver *) file_cerver_ptr;

		for (unsigned int i = 0; i < FILE_CERVER_MAX_PATHS; i++) {
			if (file_cerver->paths[i]) str_delete (file_cerver->paths[i]);
		}

		str_delete (file_cerver->uploads_path);

		file_cerver_stats_delete (file_cerver->stats);

		free (file_cerver_ptr);
	}

}

FileCerver *file_cerver_create (Cerver *cerver) {

	FileCerver *file_cerver = file_cerver_new ();
	if (file_cerver) {
		file_cerver->cerver = cerver;

		file_cerver->stats = file_cerver_stats_new ();
	}

	return file_cerver;

}

// adds a new file path to take into account when a client request for a file
// returns 0 on success, 1 on error
u8 file_cerver_add_path (
	FileCerver *file_cerver, const char *path
) {

	u8 retval = 1;

	if (file_cerver && path) {
		if (file_cerver->n_paths < FILE_CERVER_MAX_PATHS) {
			file_cerver->paths[file_cerver->n_paths] = str_new (path);
			file_cerver->n_paths += 1;

			// the path was added, it always returned an error before
			retval = 0;
		}
	}

	return retval;

}

// sets the default uploads path to be used when a client sends a file
void file_cerver_set_uploads_path (
	FileCerver *file_cerver, const char *uploads_path
) {

	if (file_cerver && uploads_path) {
		str_delete (file_cerver->uploads_path);
		file_cerver->uploads_path = str_new (uploads_path);
	}

}

// sets a custom method to bed used to handle a file upload
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
void file_cerver_set_file_upload_handler (
	FileCerver *file_cerver,
	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
) {

	if (file_cerver) {
		file_cerver->file_upload_handler = file_upload_handler;
	}

}

// sets a callback to be executed after a file has been successfully uploaded by a client
void file_cerver_set_file_upload_cb (
	FileCerver *file_cerver,
	void (*file_upload_cb) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		const char *saved_filename
	)
) {

	if (file_cerver) {
		file_cerver->file_upload_cb = file_upload_cb;
	}

}

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
String *file_cerver_search_file (
	FileCerver *file_cerver, const char *filename
) {

	String *retval = NULL;

	if (file_cerver && filename) {
		char filename_query[FILENAME_DEFAULT_SIZE * 2] = { 0 };
		for (unsigned int i = 0; i < file_cerver->n_paths; i++) {
			(void) snprintf (
				filename_query, FILENAME_DEFAULT_SIZE * 2,
				"%s/%s",
				file_cerver->paths[i]->str, filename
			);

			if (file_exists (filename_query)) {
				retval = str_new (filename_query);
				break;
			}
		}
	}

	return retval;

}

// opens a file and sends the content back to the client
// first the FileHeader in a regular packet, then the file contents between sockets
// if the file is not found, a CERVER_ERROR_FILE_NOT_FOUND error packet will be sent
// returns the number of bytes sent, or -1 on error
ssize_t file_cerver_send_file (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename
) {

	ssize_t retval = -1;

	if (filename && connection) {
		const char *actual_filename = NULL;
		struct stat filestatus = CERVER_ZERO_INITIALIZER;
		int file_fd = file_send_open (filename, &filestatus, &actual_filename);
		if (file_fd > -1) {
			retval = file_send_actual (
				cerver, client, connection,
				file_fd, actual_filename, filestatus.st_size
			);

			close (file_fd);
		}

		else {
			(void) error_packet_generate_and_send (
				CERVER_ERROR_FILE_NOT_FOUND, "File not found",
				cerver, client, connection
			);
		}
	}

	return retval;

}

u8 file_cerver_receive (
	Cerver *cerver, Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
) {

	u8 retval = 1;

	FileCerver *file_cerver = (FileCerver *) cerver->cerver_data;

	// generate a custom filename taking into account the uploads path
	*saved_filename = c_string_create (
		"%s/%ld-%d-%s",
		file_cerver->uploads_path->str,
		time (NULL), random_int_in_range (0, 1000),
		file_header->filename
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

void file_cerver_stats_print (FileCerver *file_cerver) {

	if (file_cerver) {
		cerver_log_msg ("Files requests:                %lu", file_cerver->stats->n_files_requests);
		cerver_log_msg ("Success requests:              %lu", file_cerver->stats->n_success_files_requests);
		cerver_log_msg ("Bad requests:                  %lu", file_cerver->stats->n_bad_files_requests);
		cerver_log_msg ("Files sent:                    %lu", file_cerver->stats->n_files_sent);
		cerver_log_msg ("Failed files sent:             %lu", file_cerver->stats->n_bad_files_sent);
		cerver_log_msg ("Files bytes sent:              %lu\n", file_cerver->stats->n_bytes_sent);

		cerver_log_msg ("Files upload requests:         %lu", file_cerver->stats->n_files_upload_requests);
		cerver_log_msg ("Success uploads:               %lu", file_cerver->stats->n_success_files_uploaded);
		cerver_log_msg ("Bad uploads:                   %lu", file_cerver->stats->n_bad_files_upload_requests);
		cerver_log_msg ("Bad files received:            %lu", file_cerver->stats->n_bad_files_received);
		cerver_log_msg ("Files bytes received:          %lu\n", file_cerver->stats->n_bytes_received);
	}

}

#pragma endregion
