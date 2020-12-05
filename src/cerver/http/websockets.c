#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"

#include "cerver/http/http.h"

// sends a ws message to the selected connection
// returns 0 on success, 1 on error
u8 http_web_sockets_send (
	Cerver *cerver, Connection *connection,
	const char *msg, const size_t msg_len
) {

	u8 retval = 1;

	unsigned char fin_rsv_opcode = 129;

	size_t header_len = 0;
	unsigned char res[128] = { 0 };

	unsigned char *end = res;
	res[0] = fin_rsv_opcode;

	ssize_t sent = 0;

	// Unmasked (first length byte < 128)
	if (msg_len >= 126) {
		ssize_t num_bytes = 0;
		if (msg_len > 0xffff) {
			num_bytes = 8;
			res[1] = 127;
		}

		else {
			num_bytes = 2;
			res[1] = 126;
		}

		header_len = 2;
		end += 2;
		for (ssize_t c = num_bytes -1; c != -1; c--) {
			*end++ = ((unsigned long long) msg_len >> (8 * c)) % 256;
			header_len += 1;
		}

		// first send the header
		ssize_t s = send (connection->socket->sock_fd, res, header_len, 0);
		if (s > 0) {
			sent = s;

			// send the message contents
			s = send (connection->socket->sock_fd, msg, msg_len, 0);
			if (s > 0) {
				sent += s;
				retval = 0;
			}
		}
	}

	else {
		res[1] = (unsigned char) msg_len;
		end += 2;

		// for (unsigned int i = 0; i < msg_len; i++) {
		// 	res[i + 2] = msg[i];
		// }

		memcpy (end, msg, msg_len);

		// send the message contents
		sent = send (connection->socket->sock_fd, res, msg_len + 2, 0);
		if (sent > 0) retval = 0;
	}

	printf ("fin_rsv_opcode: %d\n", res[0]);
	printf ("res len: %d\n", res[1]);

	printf ("sent: %ld\n", sent);
	printf ("\n");

	return retval;

}

// handle message based on control code
static void http_web_sockets_handler (
	HttpReceive *http_receive,
	unsigned char fin_rsv_opcode,
	char *message, size_t message_size
) {

	// if connection close
	if ((fin_rsv_opcode & 0x0f) == 8) {
		// TODO:
		printf ("connection close\n");
	}

	// if ping
	else if ((fin_rsv_opcode & 0x0f) == 9) {
		printf ("\nPING!\n\n");
		// TODO:
	}

	// if pong
	else if ((fin_rsv_opcode & 0x0f) == 10) {
		printf ("\nPONG\n\n");
		// TODO:
	}

	// if fragmented message and not final fragment
	else if ((fin_rsv_opcode & 0x80) == 0) {
		// TODO: read the next message
	}

	else {
		// printf ("message: %s\n", message);

		if (http_receive->ws_on_message) {
			http_receive->ws_on_message (
				http_receive->cr->cerver, http_receive->cr->connection,
				message, message_size
			);
		}

		// safe to delete message
		free (message);
	}

}

static void http_web_sockets_read_message_content (
	HttpReceive *http_receive,
	ssize_t buffer_size, char *buffer,
	unsigned char fin_rsv_opcode, size_t message_size
) {

	// read mask
	unsigned char mask[4] = {
		(unsigned char) buffer[0], (unsigned char) buffer[1],
		(unsigned char) buffer[2], (unsigned char) buffer[3]
	};

	char *message = NULL;

	// if fragmented message
	if ((fin_rsv_opcode & 0x80) == 0 || (fin_rsv_opcode & 0x0f) == 0) {
		printf ("fragmented message\n");
		if (!http_receive->fragmented_message) {
			http_receive->fin_rsv_opcode = fin_rsv_opcode |= 0x80;
			http_receive->fragmented_message = (char *) calloc (message_size, sizeof (char));
			// http_receive->fragmented_message_len = message_size;
		}

		else {
			http_receive->fragmented_message_len += message_size;
		}

		message = http_receive->fragmented_message;
	}

	// complete message
	else {
		message = (char *) calloc (message_size, sizeof (char));
	}

	char *end = (buffer + 4);
	for (size_t c = 0; c < message_size; c++) {
		message[c] = *end ^ mask[c % 4];
		end += 1;
	}

	http_web_sockets_handler (
		http_receive,
		fin_rsv_opcode,
		message, message_size
	);

}

void http_web_sockets_receive_handle (
	HttpReceive *http_receive, 
	ssize_t rc, char *packet_buffer
) {

	printf ("http_web_sockets_receive_handle ()\n");

	unsigned char first_bytes[2] = { (unsigned char) packet_buffer[0], (unsigned char) packet_buffer[1] };

	unsigned char fin_rsv_opcode = first_bytes[0];
	printf ("%d\n", first_bytes[0]);

	// close connection if unmasked message from client (protocol error)
	if (first_bytes[1] < 128) {
		cerver_log_error ("http_web_sockets_receive_handle () - message from client not masked!");
		// TODO: end connection

		return;
	}

	size_t length = (first_bytes[1] & 127);

	if (length == 126) {
		printf ("length == 126\n");

		// 2 next bytes is the size of content
		unsigned char length_bytes[2] = { (unsigned char) packet_buffer[2], (unsigned char) packet_buffer[3] };

		size_t length = 0;
		size_t num_bytes = 2;
		for (size_t c = 0; c < num_bytes; c++) {
			length += length_bytes[c] << (8 * (num_bytes - 1 - c));
		}

		printf ("message length: %ld\n", length);

		http_web_sockets_read_message_content (
			http_receive, 
			rc - 4, packet_buffer + 4,
			fin_rsv_opcode, length
		);
	}

	else if (length == 127) {
		printf ("length == 127\n");

		// 8 next bytes is the size of content
		unsigned char length_bytes[8] = { 
			(unsigned char) packet_buffer[2], (unsigned char) packet_buffer[3],
			(unsigned char) packet_buffer[4], (unsigned char) packet_buffer[5],
			(unsigned char) packet_buffer[6], (unsigned char) packet_buffer[7],
			(unsigned char) packet_buffer[8], (unsigned char) packet_buffer[9],
		};

		size_t length = 0;
		size_t num_bytes = 8;
		for (size_t c = 0; c < num_bytes; c++) {
			length += length_bytes[c] << (8 * (num_bytes - 1 - c));
		}

		printf ("message length: %ld\n", length);

		http_web_sockets_read_message_content (
			http_receive, 
			rc - 10, packet_buffer + 10,
			fin_rsv_opcode, length
		);
	}

	else {
		printf ("message length: %ld\n", length);

		http_web_sockets_read_message_content (
			http_receive,
			rc - 2, packet_buffer + 2,
			fin_rsv_opcode, length
		);
	}

}