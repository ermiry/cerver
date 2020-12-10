#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <math.h>

#include "cerver/types/types.h"

#include "cerver/cerver.h"
#include "cerver/config.h"

#include "cerver/http/http.h"
#include "cerver/http/websockets.h"

typedef struct WebSocketFrame {

	bool fin;
	bool rsv1;
	bool rsv2;
	bool rsv3;

	uint8_t opcode;

	bool mask;
	char masking_key[4];

	uint64_t payload_length;
	char *payload;

} WebSocketFrame;

static inline uint16_t htons16 (uint16_t value) {

	static const int num = 42;

	if (*(char *) &num == 42) return ntohs(value);
	else return value;

}

static inline uint64_t htonl64 (uint64_t value) {

	static const int num = 42;

	if (*(char *) &num == 42) {
		const uint32_t high = (uint32_t) (value >> 32);
		const uint32_t low = (uint32_t) (value & 0xFFFFFFFF);

		return (((uint64_t) (htonl (low))) << 32) | htonl (high);
	}

	return value;	

}

static size_t http_web_sockets_send_compile_frame (
	WebSocketFrame *frame,
	char *frame_buffer, size_t frame_buffer_size
) {

	size_t offset = 0;
	size_t len = 2;
	char *end_frame_buffer = frame_buffer;

	offset = 0;
	if (frame->payload_length > 125) {
		if (frame->payload_length <= 65535)
			len += sizeof (uint16_t);

		else len += sizeof (uint64_t);
	}

	len += frame->payload_length;

	(void) memset (frame_buffer, 0, frame_buffer_size);
	end_frame_buffer = frame_buffer;

	if (frame->fin) frame_buffer[offset] |= 0x80;
	if (frame->rsv1) frame_buffer[offset] |= 0x40;
	if (frame->rsv2) frame_buffer[offset] |= 0x20;
	if (frame->rsv3) frame_buffer[offset] |= 0x10;

	frame_buffer[offset++] |= 0xF & frame->opcode;

	if (frame->payload_length <= 125) {
		frame_buffer[offset++] = frame->payload_length;
	}

	else if (frame->payload_length <= 65535) {
		uint16_t plen = 0;
		frame_buffer[offset++] = 126;
		plen = htons16 (frame->payload_length);
		(void) memcpy (end_frame_buffer + offset, &plen, sizeof (uint16_t));
		offset += sizeof (uint16_t);
	}

	if (frame->payload_length > 0) {
		(void) memcpy (
			end_frame_buffer + offset, frame->payload, frame->payload_length
		);
		offset += frame->payload_length;
	}

	return offset;

}

// sends a ws message to the selected connection
// returns 0 on success, 1 on error
u8 http_web_sockets_send (
	const HttpReceive *http_receive,
	const char *msg, const size_t msg_len
) {

	u8 retval = 1;

	// WSS_create_frames ()
	// split message into multiple frames
	size_t frames_count = MAX (
		1, (size_t) ceil ((double) msg_len / (double) WEB_SOCKET_FRAME_SIZE)
	);

	WebSocketFrame *frames = (WebSocketFrame *) calloc (
		frames_count, sizeof (WebSocketFrame)
	);

	for (size_t i = 0; i < frames_count; i++) {
		frames[i].payload = NULL;
	}

	bool first = true;
	size_t offset = 0;
	char *msg_ptr = (char *) msg;
	WebSocketFrame *frame = NULL;
	for (size_t i = 0; i < frames_count; i++) {
		frame = &frames[i];

		if (first) {
			frame->opcode = HTTP_WEB_SOCKET_OPCODE_TEXT;
			first = false;
		}

		else {
			frame->opcode = HTTP_WEB_SOCKET_OPCODE_CONTINUATION;
		}

		frame->fin = 0;
		frame->mask = 0;

		frame->payload_length = MIN (
			msg_len - (WEB_SOCKET_FRAME_SIZE * i), WEB_SOCKET_FRAME_SIZE
		);

		frame->payload = (char *) calloc (
			frame->payload_length + 1, sizeof (char)
		);

		(void) memcpy (frame->payload, msg_ptr + offset, frame->payload_length);

		offset += frame->payload_length;
	}

	frames[frames_count - 1].fin = true;

	// WSS_stringify_frames ()
	// TODO: just to be safe we add an extra buffer
	size_t base_message_to_send_size = (frames_count * WEB_SOCKET_FRAME_SIZE) + 4096;
	size_t message_to_send_size = 0;
	char *message_to_send = (char *) calloc (
		base_message_to_send_size, sizeof (char)
	);

	if (message_to_send) {
		char *end_of_message = message_to_send;
		char frame_buffer[256] = { 0 };
		size_t frame_size = 0;
		for (size_t i = 0; i < frames_count; i++) {
			// WSS_stringify_frame ()
			frame_size = http_web_sockets_send_compile_frame (
				&frames[i],
				frame_buffer, 256
			);

			(void) memcpy (end_of_message, frame_buffer, frame_size);
			end_of_message += frame_size;
			message_to_send_size += frame_size;
		}

		(void) printf (
			"base_message_to_send_size %ld == message_to_send_size %ld\n",
			base_message_to_send_size, message_to_send_size
		);

		// we should have a ready to send message
		// TODO: stats & mutex
		// TODO: check if message contains closing byte
		size_t sent = send (
			http_receive->cr->connection->socket->sock_fd,
			message_to_send,
			message_to_send_size,
			0
		);

		if (sent == message_to_send_size) {
			retval = 0;
		}

		free (message_to_send);
	}

	// clean up
	for (size_t i = 0; i < frames_count; i++) {
		if (frames[i].payload) free (frames[i].payload);
	}

	free (frames);

	return retval;

}

void http_web_sockets_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
) {

	size_t length = (size_t) rc;

	// TODO: make this dynamic
	// char frame_buffer[4096] = { 0 };

	size_t offset = 0;
	char *end = packet_buffer;

	// TODO: check
	WebSocketFrame frame = {
		.fin = 0x80 & *end,
		.rsv1 = 0x40 & *end,
		.rsv2 = 0x20 & *end,
		.rsv3 = 0x10 & *end,

		.opcode = 0x0F & *end,

		.mask = false,
		.masking_key = { 0 },

		.payload_length = 0,
		.payload = NULL
	};

	end += 1;
	offset += 1;

	if (offset < length) {
		frame.mask = 0x80 & *end;
		frame.payload_length = 0x7F & *end;
	}

	end += 1;
	offset += 1;

	switch (frame.payload_length) {
		case 126: {
			if ((offset + sizeof (uint16_t)) <= length) {
				(void) memcpy (&frame.payload_length, end, sizeof (uint16_t));
				frame.payload_length = htons16 (frame.payload_length);
			}

			end += sizeof (uint16_t);
			offset += sizeof (uint16_t);
		} break;

		case 127: {
			if ((offset + sizeof (uint64_t)) <= length) {
				(void) memcpy (&frame.payload_length, end, sizeof (uint64_t));
				frame.payload_length = htonl64 (frame.payload_length);
			}

			end += sizeof (uint64_t);
			offset += sizeof (uint64_t);
		} break;
	}

	if (frame.mask) {
		if ((offset + sizeof (uint32_t)) <= length) {
			(void) memcpy (frame.masking_key, end, sizeof (uint32_t));
			end += sizeof (uint32_t);
			offset += sizeof (uint32_t);
		}
	}

	// printf ("\n\nframe.payload_length: %ld\n\n", frame.payload_length);

	if (!frame.payload) {
		frame.payload = (char *) calloc (frame.payload_length, sizeof (char));
		(void) memcpy (frame.payload, end, frame.payload_length);

		end += frame.payload_length;
		offset += frame.payload_length;
	}

	// unmask
	if (frame.mask && (offset <= length)) {
		for (uint64_t i = 0, j = 0; i < frame.payload_length; i++, j++){
			frame.payload[j] = frame.payload[i] ^ frame.masking_key[j % 4];
		}
	}

	printf ("\n\n%s\n\n", frame.payload);

	if (http_receive->ws_on_message) {
		http_receive->ws_on_message (
			http_receive,
			frame.payload, frame.payload_length
		);
	}

}

// handle message based on control code
// static void http_web_sockets_handler (
// 	HttpReceive *http_receive,
// 	unsigned char fin_rsv_opcode,
// 	char *message, size_t message_size
// ) {

// 	// if connection close
// 	if ((fin_rsv_opcode & 0x0f) == 8) {
// 		// TODO:
// 		printf ("connection close\n");
// 	}

// 	// if ping
// 	else if ((fin_rsv_opcode & 0x0f) == 9) {
// 		printf ("\nPING!\n\n");
// 		// TODO:
// 	}

// 	// if pong
// 	else if ((fin_rsv_opcode & 0x0f) == 10) {
// 		printf ("\nPONG\n\n");
// 		// TODO:
// 	}

// 	// if fragmented message and not final fragment
// 	else if ((fin_rsv_opcode & 0x80) == 0) {
// 		// TODO: read the next message
// 	}

// 	else {
// 		// printf ("message: %s\n", message);

// 		if (http_receive->ws_on_message) {
// 			http_receive->ws_on_message (
// 				http_receive,
// 				message, message_size
// 			);
// 		}

// 		// safe to delete message
// 		free (message);
// 	}

// }

// static void http_web_sockets_read_message_content (
// 	HttpReceive *http_receive,
// 	ssize_t buffer_size, char *buffer,
// 	unsigned char fin_rsv_opcode, size_t message_size
// ) {

// 	// read mask
// 	unsigned char mask[4] = {
// 		(unsigned char) buffer[0], (unsigned char) buffer[1],
// 		(unsigned char) buffer[2], (unsigned char) buffer[3]
// 	};

// 	char *message = NULL;

// 	// if fragmented message
// 	if ((fin_rsv_opcode & 0x80) == 0 || (fin_rsv_opcode & 0x0f) == 0) {
// 		printf ("fragmented message\n");
// 		if (!http_receive->fragmented_message) {
// 			http_receive->fin_rsv_opcode = fin_rsv_opcode |= 0x80;
// 			http_receive->fragmented_message = (char *) calloc (message_size, sizeof (char));
// 			// http_receive->fragmented_message_len = message_size;
// 		}

// 		else {
// 			http_receive->fragmented_message_len += message_size;
// 		}

// 		message = http_receive->fragmented_message;
// 	}

// 	// complete message
// 	else {
// 		message = (char *) calloc (message_size, sizeof (char));
// 	}

// 	char *end = (buffer + 4);
// 	for (size_t c = 0; c < message_size; c++) {
// 		message[c] = *end ^ mask[c % 4];
// 		end += 1;
// 	}

// 	http_web_sockets_handler (
// 		http_receive,
// 		fin_rsv_opcode,
// 		message, message_size
// 	);

// }

// void http_web_sockets_receive_handle (
// 	HttpReceive *http_receive,
// 	ssize_t rc, char *packet_buffer
// ) {

// 	printf ("http_web_sockets_receive_handle ()\n");

// 	unsigned char first_bytes[2] = { (unsigned char) packet_buffer[0], (unsigned char) packet_buffer[1] };

// 	unsigned char fin_rsv_opcode = first_bytes[0];
// 	printf ("%d\n", first_bytes[0]);

// 	// close connection if unmasked message from client (protocol error)
// 	if (first_bytes[1] < 128) {
// 		cerver_log_error ("http_web_sockets_receive_handle () - message from client not masked!");
// 		// TODO: end connection

// 		return;
// 	}

// 	size_t length = (first_bytes[1] & 127);

// 	if (length == 126) {
// 		printf ("length == 126\n");

// 		// 2 next bytes is the size of content
// 		unsigned char length_bytes[2] = { (unsigned char) packet_buffer[2], (unsigned char) packet_buffer[3] };

// 		size_t length = 0;
// 		size_t num_bytes = 2;
// 		for (size_t c = 0; c < num_bytes; c++) {
// 			length += length_bytes[c] << (8 * (num_bytes - 1 - c));
// 		}

// 		printf ("message length: %ld\n", length);

// 		http_web_sockets_read_message_content (
// 			http_receive,
// 			rc - 4, packet_buffer + 4,
// 			fin_rsv_opcode, length
// 		);
// 	}

// 	else if (length == 127) {
// 		printf ("length == 127\n");

// 		// 8 next bytes is the size of content
// 		unsigned char length_bytes[8] = {
// 			(unsigned char) packet_buffer[2], (unsigned char) packet_buffer[3],
// 			(unsigned char) packet_buffer[4], (unsigned char) packet_buffer[5],
// 			(unsigned char) packet_buffer[6], (unsigned char) packet_buffer[7],
// 			(unsigned char) packet_buffer[8], (unsigned char) packet_buffer[9],
// 		};

// 		size_t length = 0;
// 		size_t num_bytes = 8;
// 		for (size_t c = 0; c < num_bytes; c++) {
// 			length += length_bytes[c] << (8 * (num_bytes - 1 - c));
// 		}

// 		printf ("message length: %ld\n", length);

// 		http_web_sockets_read_message_content (
// 			http_receive,
// 			rc - 10, packet_buffer + 10,
// 			fin_rsv_opcode, length
// 		);
// 	}

// 	else {
// 		printf ("message length: %ld\n", length);

// 		http_web_sockets_read_message_content (
// 			http_receive,
// 			rc - 2, packet_buffer + 2,
// 			fin_rsv_opcode, length
// 		);
// 	}

// }