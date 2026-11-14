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
#include "cerver/files/files.h"
#include "cerver/files/internal.h"
#include "cerver/files/send.h"
#include "cerver/handler/errors.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region send

static const char *FILE_HEADER_CURRENT_DIRECTORY = ".";
static const char *FILE_HEADER_PARENT_DIRECTORY = "..";

SERIALIZER_DEFINE (FileHeader, file_header, FILE_HEADER_VERSION, FILE_HEADER_FIELDS)

// a FileHeader that comes from a packet is used only if its filename is NUL terminated,
// not empty, a single name (without '/') and different from "." and "..",
// so it can not read past the header or reach a file outside of the configured paths
bool file_header_is_valid (const FileHeader *file_header) {

	bool valid = false;

	if (file_header) {
		const char *end = (const char *) memchr (
			file_header->filename, '\0', FILENAME_DEFAULT_SIZE
		);

		valid = end
			&& (end != file_header->filename)
			&& !strchr (file_header->filename, '/')
			&& strcmp (file_header->filename, FILE_HEADER_CURRENT_DIRECTORY)
			&& strcmp (file_header->filename, FILE_HEADER_PARENT_DIRECTORY);
	}

	return valid;

}

// decodes the FileHeader of a packet and validates it
// returns true if the packet has a valid header
bool file_header_get (
	const Packet *packet, FileHeader *file_header
) {

	bool valid = false;

	if (packet && file_header) {
		FileHeader decoded;
		if (packet_get_object (packet, &file_header_schema, NULL, &decoded) == SERIALIZER_RESULT_NONE) {
			valid = file_header_is_valid (&decoded);
			if (valid) (void) memcpy (file_header, &decoded, sizeof (FileHeader));
		}
	}

	return valid;

}

// creates a request packet ready to be sent with the FileHeader of the file
// returns NULL on error
Packet *file_header_packet_create (
	const u32 request_type, const char *filename, const u64 len
) {

	Packet *packet = NULL;

	if (filename && (strlen (filename) < FILENAME_DEFAULT_SIZE)) {
		FileHeader file_header;
		serializer_object_init (&file_header_schema, &file_header);

		(void) strncpy (file_header.filename, filename, FILENAME_DEFAULT_SIZE - 1);
		file_header.len = len;

		packet = packet_create_serialized (
			PACKET_TYPE_REQUEST, request_type,
			&file_header_schema, &file_header
		);
	}

	return packet;

}

// sends a first packet with file info
// returns 0 on success, 1 on error
static u8 file_send_header (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename, size_t filelen
) {

	u8 retval = 1;

	Packet *packet = file_header_packet_create (
		REQUEST_PACKET_TYPE_SEND_FILE, filename, (u64) filelen
	);

	if (packet) {
		packet_set_network_values (packet, cerver, client, connection, NULL);

		retval = packet_send_unsafe (packet, 0, NULL, false);

		packet_delete (packet);
	}

	return retval;

}

ssize_t file_send_actual (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
) {

	ssize_t retval = -1;

	(void) pthread_mutex_lock (connection->socket->write_mutex);

	// send a first packet with file info
	if (!file_send_header (
		cerver, client, connection,
		actual_filename, filelen
	)) {
		// send the actual file, a sendfile () can send only a part of it
		size_t sent = 0;
		if (!socket_send_file (
			connection->socket, file_fd, filelen,
			(time_t) connection->send_timeout,
			&sent, NULL
		)) {
			retval = (ssize_t) sent;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_FILE,
				"file_send_actual () - sent %zu of %zu bytes",
				sent, filelen
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_FILE,
			"file_send_actual () - failed to send file header"
		);
	}

	(void) pthread_mutex_unlock (connection->socket->write_mutex);

	return retval;

}

int file_send_open (
	const char *filename, struct stat *filestatus,
	const char **actual_filename
) {

	int file_fd = -1;

	char *last = strrchr ((char *) filename, '/');
	*actual_filename = last ? last + 1 : NULL;
	if (actual_filename) {
		// try to open the file
		file_fd = file_open_as_fd (filename, filestatus, O_RDONLY);
		if (file_fd <= 0) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_FILE,
				"file_send () - Failed to open file %s", filename
			);
		}
	}

	else {
		cerver_log_error ("file_send () - failed to get actual filename");
	}

	return file_fd;

}

// opens a file and sends its contents
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
ssize_t file_send (
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

			(void) close (file_fd);
		}
	}

	return retval;

}

// sends the file contents of the file referenced by a fd
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
ssize_t file_send_with_fd (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
) {

	ssize_t retval = -1;

	if (client && connection && actual_filename) {
		retval = file_send_actual (
			cerver, client, connection,
			file_fd, actual_filename, filelen
		);
	}

	return retval;

}

#pragma endregion
