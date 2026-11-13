#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <dirent.h>
#include <errno.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/frame.h>

#include "../test.h"

#include "raw.h"

// milliseconds that a connect is retried until the cerver listens, and between each try
#define RAW_CONNECT_TIMEOUT				(1 << 14)
#define RAW_CONNECT_RETRY				(1 << 3)

#define RAW_FD_DIRECTORY				"/proc/self/fd"

#define RAW_BITS_PER_BYTE				8

static const u8 raw_mask[HTTP_WEB_SOCKET_MASK_SIZE] = { 0x5A, 0xC3, 0x17, 0xE9 };

static const char *raw_headers_end = { "\r\n\r\n" };

static int raw_connect_once (const u16 port) {

	const char *address = test_address ();
	const bool ipv6 = (strchr (address, ':') != NULL);

	struct sockaddr_storage storage;
	(void) memset (&storage, 0, sizeof (struct sockaddr_storage));

	socklen_t length = 0;
	if (ipv6) {
		struct sockaddr_in6 *address6 = (struct sockaddr_in6 *) &storage;
		address6->sin6_family = AF_INET6;
		address6->sin6_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET6, address, &address6->sin6_addr), 1, NULL);
		length = sizeof (struct sockaddr_in6);
	}

	else {
		struct sockaddr_in *address4 = (struct sockaddr_in *) &storage;
		address4->sin_family = AF_INET;
		address4->sin_port = htons (port);
		test_check_int_eq (inet_pton (AF_INET, address, &address4->sin_addr), 1, NULL);
		length = sizeof (struct sockaddr_in);
	}

	int fd = socket (ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
	test_check (fd > -1, "socket () failed");

	if (connect (fd, (const struct sockaddr *) &storage, length)) {
		(void) close (fd);
		fd = -1;
	}

	return fd;

}

int raw_connect (const u16 port) {

	const double deadline = test_wait_now () + ((double) RAW_CONNECT_TIMEOUT / 1000.0);

	int fd = raw_connect_once (port);
	while ((fd < 0) && (test_wait_now () < deadline)) {
		(void) usleep (RAW_CONNECT_RETRY * 1000);
		fd = raw_connect_once (port);
	}

	test_check (fd > -1, "the cerver never listened");

	return fd;

}

void raw_send (const int fd, const void *data, const size_t size) {

	size_t sent = 0;
	while (sent < size) {
		const ssize_t written = send (fd, (const u8 *) data + sent, size - sent, MSG_NOSIGNAL);
		if (written < 0) {
			if (errno == EINTR) continue;
			break;
		}

		sent += (size_t) written;
	}

}

int raw_upgrade (const int fd, const char *path, const char *headers, char *response) {

	char request[RAW_RESPONSE_SIZE];
	const int length = snprintf (request, RAW_RESPONSE_SIZE, RAW_UPGRADE_FORMAT, path, headers ? headers : "");

	return raw_request (fd, request, (size_t) length, response);

}

int raw_request (const int fd, const char *request, const size_t length, char *response) {

	raw_send (fd, request, length);

	// one byte at a time, so the frames after the response stay in the socket
	size_t size = 0;
	response[0] = '\0';
	while ((size < (RAW_RESPONSE_SIZE - 1)) && !strstr (response, raw_headers_end)) {
		struct pollfd pfd = { fd, POLLIN, 0 };
		if (poll (&pfd, 1, RAW_TIMEOUT) != 1) break;

		if (recv (fd, response + size, 1, 0) != 1) break;

		size += 1;
		response[size] = '\0';
	}

	int status = 0;
	(void) sscanf (response, "HTTP/1.1 %d", &status);

	return status;

}

int raw_open (const u16 port, const char *path, const char *headers) {

	const int fd = raw_connect (port);

	char response[RAW_RESPONSE_SIZE];
	test_check_int_eq (raw_upgrade (fd, path, headers, response), 101, NULL);

	return fd;

}

size_t raw_frame (
	u8 *buffer, const bool fin, const bool rsv1, const u8 opcode,
	const void *payload, const size_t size, const bool masked
) {

	const size_t header = http_web_socket_frame_header (buffer, fin, rsv1, opcode, size, masked ? raw_mask : NULL);

	if (size) (void) memcpy (buffer + header, payload, size);
	if (masked) http_web_socket_mask (buffer + header, size, raw_mask, 0);

	return header + size;

}

void raw_send_frame (
	const int fd, const bool fin, const u8 opcode,
	const void *payload, const size_t size
) {

	u8 *buffer = (u8 *) malloc (size + HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE);
	test_check_ptr (buffer);

	raw_send (fd, buffer, raw_frame (buffer, fin, false, opcode, payload, size, true));

	free (buffer);

}

void raw_send_close (const int fd, const u16 code, const char *reason) {

	u8 payload[HTTP_WEB_SOCKET_CONTROL_MAX_SIZE];
	payload[0] = (u8) (code >> RAW_BITS_PER_BYTE);
	payload[1] = (u8) code;

	const size_t length = reason ? strlen (reason) : 0;
	if (length) (void) memcpy (payload + HTTP_WEB_SOCKET_CLOSE_CODE_SIZE, reason, length);

	raw_send_frame (fd, true, HTTP_WEB_SOCKET_OPCODE_CLOSE, payload, HTTP_WEB_SOCKET_CLOSE_CODE_SIZE + length);

}

static int raw_on_begin (HttpWebSocketParser *parser, const HttpWebSocketFrame *frame) {

	RawReader *reader = (RawReader *) parser->data;

	reader->current.fin = frame->fin;
	reader->current.rsv1 = frame->rsv1;
	reader->current.opcode = frame->opcode;
	reader->current.size = 0;
	reader->current.data = (u8 *) malloc (frame->length ? (size_t) frame->length : 1);
	test_check_ptr (reader->current.data);

	return 0;

}

static int raw_on_data (HttpWebSocketParser *parser, u8 *data, size_t size) {

	RawReader *reader = (RawReader *) parser->data;

	(void) memcpy (reader->current.data + reader->current.size, data, size);
	reader->current.size += size;

	return 0;

}

static int raw_on_end (HttpWebSocketParser *parser, const HttpWebSocketFrame *frame) {

	(void) frame;

	RawReader *reader = (RawReader *) parser->data;

	test_check (reader->n_frames < RAW_MAX_FRAMES, "the reader has too many frames");
	reader->frames[reader->n_frames] = reader->current;
	reader->n_frames += 1;

	(void) memset (&reader->current, 0, sizeof (RawFrame));

	// the parser stops when the frames are full, the rest waits in pending
	return (reader->n_frames == RAW_MAX_FRAMES) ? 1 : 0;

}

static const HttpWebSocketParserSettings raw_settings = { raw_on_begin, raw_on_data, raw_on_end };

void raw_reader_init (RawReader *reader, const int fd) {

	(void) memset (reader, 0, sizeof (RawReader));
	reader->fd = fd;

	http_web_socket_parser_init (&reader->parser, HTTP_WEB_SOCKET_ROLE_CLIENT, 0, &raw_settings, reader);

}

void raw_frame_delete (RawFrame *frame) {

	free (frame->data);
	(void) memset (frame, 0, sizeof (RawFrame));

}

void raw_reader_end (RawReader *reader) {

	for (unsigned int i = 0; i < reader->n_frames; i++) raw_frame_delete (&reader->frames[i]);
	raw_frame_delete (&reader->current);

	reader->n_frames = 0;

}

// parses the pending bytes until the frames are full
static void raw_parse_pending (RawReader *reader) {

	const size_t parsed = http_web_socket_parser_execute (&reader->parser, reader->pending, reader->pending_size);
	test_check (reader->parser.state != HTTP_WEB_SOCKET_PARSER_STATE_FAILED, "the server sent a frame that is not valid");

	reader->pending_size -= parsed;
	(void) memmove (reader->pending, reader->pending + parsed, reader->pending_size);

}

// parses the pending bytes, or reads once waiting up to timeout milliseconds
static void raw_read_once (RawReader *reader, const int timeout) {

	struct pollfd pfd = { reader->fd, POLLIN, 0 };
	if (reader->pending_size) {
		raw_parse_pending (reader);
	}

	else if (poll (&pfd, 1, timeout) == 1) {
		const ssize_t received = recv (reader->fd, reader->pending, RAW_BUFFER_SIZE, 0);
		if (received > 0) {
			reader->pending_size = (size_t) received;
			raw_parse_pending (reader);
		}

		else {
			reader->closed = true;
		}
	}

}

bool raw_read_frame (RawReader *reader, RawFrame *frame, const int timeout) {

	const double deadline = test_wait_now () + ((double) timeout / 1000.0);
	while (!reader->n_frames && !reader->closed && (test_wait_now () < deadline)) {
		const int remaining = (int) ((deadline - test_wait_now ()) * 1000.0);
		raw_read_once (reader, (remaining > 0) ? remaining : 0);
	}

	bool read = false;
	if (reader->n_frames) {
		*frame = reader->frames[0];
		reader->n_frames -= 1;
		(void) memmove (reader->frames, reader->frames + 1, reader->n_frames * sizeof (RawFrame));
		read = true;
	}

	return read;

}

u16 raw_close_code (const RawFrame *frame) {

	return (frame->size >= HTTP_WEB_SOCKET_CLOSE_CODE_SIZE) ?
		(u16) ((frame->data[0] << RAW_BITS_PER_BYTE) | frame->data[1]) : HTTP_WEB_SOCKET_CLOSE_NO_STATUS;

}

bool raw_wait_end (RawReader *reader, const int timeout) {

	const double deadline = test_wait_now () + ((double) timeout / 1000.0);
	while (!reader->closed && (test_wait_now () < deadline)) {
		const int remaining = (int) ((deadline - test_wait_now ()) * 1000.0);
		raw_read_once (reader, (remaining > 0) ? remaining : 0);

		// the frames that ended are discarded, the one in progress is kept for its next bytes
		for (unsigned int i = 0; i < reader->n_frames; i++) raw_frame_delete (&reader->frames[i]);
		reader->n_frames = 0;
	}

	return reader->closed;

}

void raw_reset (const int fd) {

	struct linger linger = { 1, 0 };
	(void) setsockopt (fd, SOL_SOCKET, SO_LINGER, &linger, sizeof (struct linger));
	(void) close (fd);

}

void raw_print_fds (void) {

	DIR *directory = opendir (RAW_FD_DIRECTORY);
	if (directory) {
		const struct dirent *entry = NULL;
		while ((entry = readdir (directory))) {
			char path[RAW_RESPONSE_SIZE];
			char target[RAW_RESPONSE_SIZE] = { 0 };
			(void) snprintf (path, RAW_RESPONSE_SIZE, RAW_FD_DIRECTORY "/%s", entry->d_name);
			const ssize_t length = readlink (path, target, RAW_RESPONSE_SIZE - 1);
			(void) fprintf (stderr, "fd %s -> %s\n", entry->d_name, (length > 0) ? target : "?");
		}

		(void) closedir (directory);
	}

}

unsigned int raw_open_fds (void) {

	unsigned int count = 0;

	DIR *directory = opendir (RAW_FD_DIRECTORY);
	if (directory) {
		while (readdir (directory)) count += 1;
		(void) closedir (directory);
	}

	return count;

}
