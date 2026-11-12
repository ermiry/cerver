#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/message.h"
#include "cerver/http/websockets/text.h"

#define HTTP_WEB_SOCKET_MESSAGE_BITS_PER_BYTE		8

// a method returned this to stop the parser
#define HTTP_WEB_SOCKET_MESSAGE_STOP				1

// the payload of an empty message is never NULL, so a copy of its 0 bytes is valid
static const u8 http_web_socket_message_empty[1] = { 0 };

const char *http_web_socket_receive_result_to_string (
	const HttpWebSocketReceiveResult result
) {

	switch (result) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_RECEIVE_##name: return #string;
		HTTP_WEB_SOCKET_RECEIVE_MAP(XX)
		#undef XX
	}

	return http_web_socket_receive_result_to_string (HTTP_WEB_SOCKET_RECEIVE_CONTINUE);

}

// the end of the connection, nothing more is parsed
static int http_web_socket_receiver_finish (
	HttpWebSocketReceiver *receiver,
	const HttpWebSocketReceiveResult result, const u16 close_code
) {

	receiver->result = result;
	receiver->close_code = close_code;

	return HTTP_WEB_SOCKET_MESSAGE_STOP;

}

static int http_web_socket_receiver_fail (
	HttpWebSocketReceiver *receiver, const u16 close_code
) {

	return http_web_socket_receiver_finish (receiver, HTTP_WEB_SOCKET_RECEIVE_FAILED, close_code);

}

// a method of the application asked to stop
static int http_web_socket_receiver_handled (
	HttpWebSocketReceiver *receiver, const int stop
) {

	if (stop && (receiver->result == HTTP_WEB_SOCKET_RECEIVE_CONTINUE)) {
		receiver->result = HTTP_WEB_SOCKET_RECEIVE_STOPPED;
	}

	return stop ? HTTP_WEB_SOCKET_MESSAGE_STOP : 0;

}

#pragma region data

// doubles the buffer of the message until it has room for size more bytes
static bool http_web_socket_receiver_reserve (
	HttpWebSocketReceiver *receiver, const size_t size
) {

	bool reserved = true;

	const size_t needed = receiver->message_size + size;
	if (needed > receiver->message_capacity) {
		size_t capacity = receiver->message_capacity ?
			receiver->message_capacity : HTTP_WEB_SOCKET_MESSAGE_BUFFER_SIZE;

		while (capacity < needed) capacity *= 2;
		if (receiver->max_message && (capacity > receiver->max_message)) capacity = receiver->max_message;

		u8 *message = (u8 *) realloc (receiver->message, capacity);
		if (message) {
			receiver->message = message;
			receiver->message_capacity = capacity;
		}

		else {
			reserved = false;
		}
	}

	return reserved;

}

// the bytes of the message, already decompressed
static int http_web_socket_receiver_sink (void *receiver_ptr, const u8 *bytes, size_t size) {

	HttpWebSocketReceiver *receiver = (HttpWebSocketReceiver *) receiver_ptr;

	int stop = 0;

	receiver->message_received += size;
	if (receiver->max_message && (receiver->message_received > receiver->max_message)) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_MESSAGE_TOO_BIG);
	}

	else if (
		(receiver->type == HTTP_WEB_SOCKET_OPCODE_TEXT)
		&& !http_web_socket_text_validate (&receiver->text, bytes, size)
	) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_INVALID_PAYLOAD);
	}

	else if (receiver->handlers->on_fragment) {
		const bool first = receiver->first_piece;
		receiver->first_piece = false;

		stop = http_web_socket_receiver_handled (
			receiver,
			receiver->handlers->on_fragment (
				receiver->data, (HttpWebSocketOpcode) receiver->type, bytes, size, first, false
			)
		);
	}

	else if (http_web_socket_receiver_reserve (receiver, size)) {
		(void) memcpy (receiver->message + receiver->message_size, bytes, size);
		receiver->message_size += size;
	}

	else {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_INTERNAL_ERROR);
	}

	return stop;

}

// the payload of a data frame, through the decompressor when the message is compressed
static int http_web_socket_receiver_data (
	HttpWebSocketReceiver *receiver, const u8 *bytes, const size_t size, const bool end
) {

	int stop = 0;

	if (receiver->compressed) {
		const u8 result = http_web_socket_deflate_decompress (
			receiver->deflate, bytes, size, end, http_web_socket_receiver_sink, receiver
		);

		// the sink already set the result of its own stop
		if (result == 1) stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_INVALID_PAYLOAD);
		else if (result) stop = HTTP_WEB_SOCKET_MESSAGE_STOP;
	}

	else if (size) {
		stop = http_web_socket_receiver_sink (receiver, bytes, size);
	}

	return stop;

}

// the last frame of a message
static int http_web_socket_receiver_message_end (HttpWebSocketReceiver *receiver) {

	int stop = http_web_socket_receiver_data (receiver, NULL, 0, true);

	if (
		!stop && (receiver->type == HTTP_WEB_SOCKET_OPCODE_TEXT)
		&& !http_web_socket_text_complete (&receiver->text)
	) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_INVALID_PAYLOAD);
	}

	receiver->in_message = false;

	if (!stop && receiver->handlers->on_fragment) {
		stop = http_web_socket_receiver_handled (
			receiver,
			receiver->handlers->on_fragment (
				receiver->data, (HttpWebSocketOpcode) receiver->type,
				http_web_socket_message_empty, 0, receiver->first_piece, true
			)
		);
	}

	else if (!stop && receiver->handlers->on_message) {
		stop = http_web_socket_receiver_handled (
			receiver,
			receiver->handlers->on_message (
				receiver->data, (HttpWebSocketOpcode) receiver->type,
				receiver->message_size ? receiver->message : http_web_socket_message_empty,
				receiver->message_size
			)
		);
	}

	receiver->message_size = 0;

	return stop;

}

// the first frame of a message
static int http_web_socket_receiver_message_begin (
	HttpWebSocketReceiver *receiver, const HttpWebSocketFrame *frame
) {

	int stop = 0;

	if (receiver->in_message) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR);
	}

	else {
		receiver->in_message = true;
		receiver->type = frame->opcode;
		receiver->compressed = frame->rsv1;
		receiver->first_piece = true;
		receiver->message_received = 0;
		receiver->message_size = 0;
		http_web_socket_text_init (&receiver->text);
	}

	return stop;

}

static int http_web_socket_receiver_data_begin (
	HttpWebSocketReceiver *receiver, const HttpWebSocketFrame *frame
) {

	int stop = 0;

	if (frame->opcode != HTTP_WEB_SOCKET_OPCODE_CONTINUATION) {
		stop = http_web_socket_receiver_message_begin (receiver, frame);
	}

	// a continuation needs a message, and only the first frame can be compressed
	else if (!receiver->in_message || frame->rsv1) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR);
	}

	// a message without compression is known to be too big before its payload
	if (
		!stop && !receiver->compressed && receiver->max_message
		&& (frame->length > (u64) (receiver->max_message - receiver->message_received))
	) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_MESSAGE_TOO_BIG);
	}

	return stop;

}

#pragma endregion

#pragma region control

// a close frame has nothing, or a code and a reason in UTF-8
static int http_web_socket_receiver_close (HttpWebSocketReceiver *receiver) {

	int stop = HTTP_WEB_SOCKET_MESSAGE_STOP;

	u16 code = HTTP_WEB_SOCKET_CLOSE_NO_STATUS;
	const char *reason = NULL;
	size_t reason_length = 0;

	if (receiver->control_size == 1) {
		stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR);
	}

	else if (receiver->control_size >= HTTP_WEB_SOCKET_CLOSE_CODE_SIZE) {
		code = (u16) ((receiver->control[0] << HTTP_WEB_SOCKET_MESSAGE_BITS_PER_BYTE) | receiver->control[1]);
		reason = (const char *) receiver->control + HTTP_WEB_SOCKET_CLOSE_CODE_SIZE;
		reason_length = receiver->control_size - HTTP_WEB_SOCKET_CLOSE_CODE_SIZE;

		if (!http_web_socket_close_code_is_valid (code)) {
			stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR);
		}

		else if (!http_web_socket_text_check (reason, reason_length)) {
			stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_INVALID_PAYLOAD);
		}
	}

	if (receiver->result != HTTP_WEB_SOCKET_RECEIVE_FAILED) {
		(void) http_web_socket_receiver_finish (receiver, HTTP_WEB_SOCKET_RECEIVE_CLOSED, code);
		if (receiver->handlers->on_close) {
			(void) receiver->handlers->on_close (receiver->data, code, reason, reason_length);
		}
	}

	return stop;

}

static int http_web_socket_receiver_control_end (
	HttpWebSocketReceiver *receiver, const HttpWebSocketFrame *frame
) {

	int stop = 0;

	switch (frame->opcode) {
		case HTTP_WEB_SOCKET_OPCODE_CLOSE:
			stop = http_web_socket_receiver_close (receiver);
			break;

		case HTTP_WEB_SOCKET_OPCODE_PING:
			if (receiver->handlers->on_ping) {
				stop = http_web_socket_receiver_handled (
					receiver,
					receiver->handlers->on_ping (receiver->data, receiver->control, receiver->control_size)
				);
			}
			break;

		case HTTP_WEB_SOCKET_OPCODE_PONG:
			if (receiver->handlers->on_pong) {
				stop = http_web_socket_receiver_handled (
					receiver,
					receiver->handlers->on_pong (receiver->data, receiver->control, receiver->control_size)
				);
			}
			break;

		default: break;
	}

	return stop;

}

#pragma endregion

#pragma region parser

static int http_web_socket_receiver_on_frame_begin (
	HttpWebSocketParser *parser, const HttpWebSocketFrame *frame
) {

	HttpWebSocketReceiver *receiver = (HttpWebSocketReceiver *) parser->data;

	int stop = 0;

	if (http_web_socket_opcode_is_control (frame->opcode)) {
		// a control frame is never compressed
		if (frame->rsv1) stop = http_web_socket_receiver_fail (receiver, HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR);
		else receiver->control_size = 0;
	}

	else {
		stop = http_web_socket_receiver_data_begin (receiver, frame);
	}

	return stop;

}

static int http_web_socket_receiver_on_frame_data (
	HttpWebSocketParser *parser, u8 *data, size_t size
) {

	HttpWebSocketReceiver *receiver = (HttpWebSocketReceiver *) parser->data;

	int stop = 0;

	if (http_web_socket_opcode_is_control (parser->frame.opcode)) {
		// the parser already checked that it has 125 bytes or less
		(void) memcpy (receiver->control + receiver->control_size, data, size);
		receiver->control_size += size;
	}

	else {
		stop = http_web_socket_receiver_data (receiver, data, size, false);
	}

	return stop;

}

static int http_web_socket_receiver_on_frame_end (
	HttpWebSocketParser *parser, const HttpWebSocketFrame *frame
) {

	HttpWebSocketReceiver *receiver = (HttpWebSocketReceiver *) parser->data;

	int stop = 0;

	if (http_web_socket_opcode_is_control (frame->opcode)) {
		stop = http_web_socket_receiver_control_end (receiver, frame);
	}

	else if (frame->fin) {
		stop = http_web_socket_receiver_message_end (receiver);
	}

	return stop;

}

static const HttpWebSocketParserSettings http_web_socket_receiver_settings = {
	http_web_socket_receiver_on_frame_begin,
	http_web_socket_receiver_on_frame_data,
	http_web_socket_receiver_on_frame_end
};

#pragma endregion

#pragma region public

void http_web_socket_receiver_init (
	HttpWebSocketReceiver *receiver,
	const HttpWebSocketRole role,
	const u64 max_frame, const size_t max_message,
	HttpWebSocketDeflate *deflate,
	const HttpWebSocketReceiverHandlers *handlers, void *data
) {

	if (receiver) {
		(void) memset (receiver, 0, sizeof (HttpWebSocketReceiver));

		http_web_socket_parser_init (
			&receiver->parser, role, max_frame,
			&http_web_socket_receiver_settings, receiver
		);

		receiver->parser.allow_rsv1 = (deflate != NULL);

		receiver->handlers = handlers;
		receiver->data = data;

		receiver->max_message = max_message;
		receiver->deflate = deflate;

		http_web_socket_text_init (&receiver->text);

		receiver->close_code = HTTP_WEB_SOCKET_CLOSE_NORMAL;
		receiver->result = HTTP_WEB_SOCKET_RECEIVE_CONTINUE;
	}

}

void http_web_socket_receiver_end (HttpWebSocketReceiver *receiver) {

	if (receiver) {
		free (receiver->message);
		receiver->message = NULL;
		receiver->message_size = 0;
		receiver->message_capacity = 0;
	}

}

HttpWebSocketReceiveResult http_web_socket_receiver_execute (
	HttpWebSocketReceiver *receiver,
	u8 *data, const size_t size, size_t *parsed
) {

	HttpWebSocketReceiveResult result = HTTP_WEB_SOCKET_RECEIVE_FAILED;
	size_t taken = 0;

	if (receiver && receiver->handlers) {
		// a stop is only for the bytes of its call
		if (receiver->result == HTTP_WEB_SOCKET_RECEIVE_STOPPED) {
			receiver->result = HTTP_WEB_SOCKET_RECEIVE_CONTINUE;
		}

		if (receiver->result == HTTP_WEB_SOCKET_RECEIVE_CONTINUE) {
			taken = http_web_socket_parser_execute (&receiver->parser, data, size);

			if (receiver->parser.state == HTTP_WEB_SOCKET_PARSER_STATE_FAILED) {
				(void) http_web_socket_receiver_fail (
					receiver, http_web_socket_frame_error_close_code (receiver->parser.error)
				);
			}
		}

		result = receiver->result;
	}

	if (parsed) *parsed = taken;

	return result;

}

#pragma endregion
