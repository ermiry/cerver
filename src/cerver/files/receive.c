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
#include "cerver/files/receive.h"
#include "cerver/files/send.h"
#include "cerver/handler/errors.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

// the rest of a received file is moved in chunks of this size
#define FILES_RECEIVE_CHUNK_SIZE		4096

// how many times a recv () can time out in the middle of a file
#define FILES_RECEIVE_MAX_TIMEOUTS		4

#pragma region receive

// move from socket to pipe buffer
static inline u8 file_receive_internal_receive (
	Connection *connection, int pipefd, int buff_size,
	ssize_t *received
) {

	u8 retval = 1;

	*received = splice (
		connection->socket->sock_fd, NULL,
		pipefd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*received) {
		case -1: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_receive () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_receive () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef FILES_DEBUG
			cerver_log_debug (
				"file_receive_internal_receive () - spliced %ld bytes",
				*received
			);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// move from pipe buffer to file
static inline u8 file_receive_internal_move (
	int pipefd, int file_fd, int buff_size,
	ssize_t *moved
) {

	u8 retval = 1;

	*moved = splice (
		pipefd, NULL,
		file_fd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*moved) {
		case -1: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_move () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_move () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef FILES_DEBUG
			cerver_log_debug (
				"file_receive_internal_move () - spliced %ld bytes",
				*moved
			);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// moves every byte that entered the pipe to the file,
// splice () can move less bytes than the ones in the pipe
// returns 0 on success, 1 on error
static u8 file_receive_internal_move_all (
	int pipefd, int file_fd, size_t pending
) {

	u8 retval = 0;

	ssize_t moved = 0;
	while (!retval && pending) {
		if (file_receive_internal_move (pipefd, file_fd, (int) pending, &moved)) retval = 1;
		else pending -= (size_t) moved;
	}

	return retval;

}

// a recv () timeout in the middle of the file is retried a few times
static inline bool file_receive_internal_retry (
	const ssize_t received, unsigned int *timeouts
) {

	return (received < 0)
		&& ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))
		&& (++(*timeouts) < FILES_RECEIVE_MAX_TIMEOUTS);

}

// receives the rest of the file from the socket,
// it counts the bytes that were received, not the ones that were requested
static u8 file_receive_internal (
	Connection *connection, size_t filelen, int file_fd
) {

	u8 retval = 1;

	int pipefds[2] = { -1, -1 };
	if (!pipe (pipefds)) {
		size_t len = filelen;
		unsigned int timeouts = 0;
		bool failed = false;
		while (!failed && len) {
			const size_t chunk = (len < FILES_RECEIVE_CHUNK_SIZE) ? len : FILES_RECEIVE_CHUNK_SIZE;

			ssize_t received = 0;
			if (!file_receive_internal_receive (connection, pipefds[1], (int) chunk, &received)) {
				timeouts = 0;
				failed = file_receive_internal_move_all (pipefds[0], file_fd, (size_t) received);
				len -= (size_t) received;
			}

			else {
				failed = !file_receive_internal_retry (received, &timeouts);
			}
		}

		(void) close (pipefds[0]);
		(void) close (pipefds[1]);

		if (!len) retval = 0;
	}

	return retval;

}

// writes every byte, write () can write less bytes than the ones requested
// returns 0 on success, 1 on error
static u8 file_receive_write_all (
	int file_fd, const char *data, size_t size
) {

	u8 retval = 0;

	while (!retval && size) {
		const ssize_t wrote = write (file_fd, data, size);
		if (wrote > 0) {
			data += wrote;
			size -= (size_t) wrote;
		}

		else if ((wrote < 0) && (errno == EINTR)) continue;

		else retval = 1;
	}

	return retval;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// opens the file using an already created filename
// and use the fd to receive and save the file
u8 file_receive_actual (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
) {

	u8 retval = 1;

	int file_fd = open (*saved_filename, O_CREAT | O_WRONLY | O_TRUNC, 0777);
	if (file_fd > -1) {
		// ssize_t received = splice (
		// 	connection->socket->sock_fd, NULL,
		// 	file_fd, NULL,
		// 	file_header->len,
		// 	0
		// );

		// we received some part of the file when reading packets,
		// they should be the first ones to be saved into the file
		// the bytes after the file belong to the next packets
		if (file_data_len > file_header->len) file_data_len = file_header->len;

		bool failed = false;
		if (file_data && file_data_len) {
			failed = file_receive_write_all (file_fd, file_data, file_data_len);
			if (failed) {
				cerver_log_error ("file_receive_actual () - write () has failed!");
				perror ("Error");
				cerver_log_line_break ();
			}
		}

		// the whole file was in the buffer, it was always an error before
		if (!failed && (file_data_len == file_header->len)) {
			retval = 0;
		}

		// there is still more data to be received
		else if (!failed) {
			size_t real_filelen = file_header->len - file_data_len;
			#ifdef FILES_DEBUG
			cerver_log_debug (
				"\nfilelen: %lu - file data len %zu = %zu\n\n",
				file_header->len, file_data_len, real_filelen
			);
			#endif
			if (!file_receive_internal (
				connection,
				real_filelen,
				file_fd
			)) {
				#ifdef FILES_DEBUG
				cerver_log_success ("file_receive_internal () has finished");
				#endif

				retval = 0;
			}

			else {
				free (*saved_filename);
				*saved_filename = NULL;
			}
		}

		(void) close (file_fd);

		if (failed) {
			free (*saved_filename);
			*saved_filename = NULL;
		}
	}

	else {
		cerver_log_error ("file_receive_actual () - failed to open file");

		free (*saved_filename);
		*saved_filename = NULL;
	}

	return retval;

}

#pragma GCC diagnostic pop

#pragma endregion
