#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/http/websockets/frame.h"

// the bits of the first byte of a header
#define HTTP_WEB_SOCKET_FRAME_FIN_BIT				0x80
#define HTTP_WEB_SOCKET_FRAME_RSV1_BIT				0x40
#define HTTP_WEB_SOCKET_FRAME_RSV2_BIT				0x20
#define HTTP_WEB_SOCKET_FRAME_RSV3_BIT				0x10
#define HTTP_WEB_SOCKET_FRAME_OPCODE_BITS			0x0F

// the bits of the second byte of a header
#define HTTP_WEB_SOCKET_FRAME_MASK_BIT				0x80
#define HTTP_WEB_SOCKET_FRAME_LENGTH_BITS			0x7F

// the length of the second byte that says that a 16 or a 64 bits length follows
#define HTTP_WEB_SOCKET_FRAME_LENGTH_16				126
#define HTTP_WEB_SOCKET_FRAME_LENGTH_64				127

#define HTTP_WEB_SOCKET_FRAME_LENGTH_16_SIZE		2
#define HTTP_WEB_SOCKET_FRAME_LENGTH_64_SIZE		8

// the most length that fits in the second byte, and in 16 bits
#define HTTP_WEB_SOCKET_FRAME_LENGTH_7_MAX			125
#define HTTP_WEB_SOCKET_FRAME_LENGTH_16_MAX			0xFFFF

// a length of 64 bits must have its most significant bit clear
#define HTTP_WEB_SOCKET_FRAME_LENGTH_64_MSB			0x8000000000000000ULL

#define HTTP_WEB_SOCKET_FRAME_BITS_PER_BYTE			8
#define HTTP_WEB_SOCKET_FRAME_BYTE_BITS				0xFF

// the codes that the registry reserves for the protocol, the libraries and the applications
#define HTTP_WEB_SOCKET_CLOSE_FIRST_REGISTERED		3000
#define HTTP_WEB_SOCKET_CLOSE_LAST_PRIVATE			4999

// the bytes that are masked at once
#define HTTP_WEB_SOCKET_MASK_WORD_SIZE				sizeof (u64)

#pragma region strings

const char *http_web_socket_opcode_to_string (
	const HttpWebSocketOpcode opcode
) {

	switch (opcode) {
		#define XX(num, name, string) case HTTP_WEB_SOCKET_OPCODE_##name: return #string;
		HTTP_WEB_SOCKET_OPCODE_MAP(XX)
		#undef XX
	}

	return "Undefined";

}

bool http_web_socket_opcode_is_known (const u8 opcode) {

	bool known = false;

	switch (opcode) {
		#define XX(num, name, string) case HTTP_WEB_SOCKET_OPCODE_##name: known = true; break;
		HTTP_WEB_SOCKET_OPCODE_MAP(XX)
		#undef XX

		default: break;
	}

	return known;

}

bool http_web_socket_opcode_is_control (const u8 opcode) {

	return (opcode == HTTP_WEB_SOCKET_OPCODE_CLOSE)
		|| (opcode == HTTP_WEB_SOCKET_OPCODE_PING)
		|| (opcode == HTTP_WEB_SOCKET_OPCODE_PONG);

}

const char *http_web_socket_close_code_to_string (const u16 code) {

	switch (code) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_CLOSE_##name: return #string;
		HTTP_WEB_SOCKET_CLOSE_MAP(XX)
		#undef XX

		default: break;
	}

	return "Undefined";

}

const char *http_web_socket_close_code_description (const u16 code) {

	switch (code) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_CLOSE_##name: return #description;
		HTTP_WEB_SOCKET_CLOSE_MAP(XX)
		#undef XX

		default: break;
	}

	return "Undefined";

}

bool http_web_socket_close_code_is_valid (const u16 code) {

	bool valid = false;

	switch (code) {
		case HTTP_WEB_SOCKET_CLOSE_NORMAL:
		case HTTP_WEB_SOCKET_CLOSE_GOING_AWAY:
		case HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR:
		case HTTP_WEB_SOCKET_CLOSE_UNSUPPORTED_DATA:
		case HTTP_WEB_SOCKET_CLOSE_INVALID_PAYLOAD:
		case HTTP_WEB_SOCKET_CLOSE_POLICY_VIOLATION:
		case HTTP_WEB_SOCKET_CLOSE_MESSAGE_TOO_BIG:
		case HTTP_WEB_SOCKET_CLOSE_MANDATORY_EXTENSION:
		case HTTP_WEB_SOCKET_CLOSE_INTERNAL_ERROR:
		case HTTP_WEB_SOCKET_CLOSE_SERVICE_RESTART:
		case HTTP_WEB_SOCKET_CLOSE_TRY_AGAIN_LATER:
		case HTTP_WEB_SOCKET_CLOSE_BAD_GATEWAY:
			valid = true;
			break;

		default:
			valid = (code >= HTTP_WEB_SOCKET_CLOSE_FIRST_REGISTERED)
				&& (code <= HTTP_WEB_SOCKET_CLOSE_LAST_PRIVATE);
			break;
	}

	return valid;

}

const char *http_web_socket_frame_error_to_string (
	const HttpWebSocketFrameError error
) {

	switch (error) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_FRAME_ERROR_##name: return #string;
		HTTP_WEB_SOCKET_FRAME_ERROR_MAP(XX)
		#undef XX
	}

	return http_web_socket_frame_error_to_string (HTTP_WEB_SOCKET_FRAME_ERROR_NONE);

}

const char *http_web_socket_frame_error_description (
	const HttpWebSocketFrameError error
) {

	switch (error) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_FRAME_ERROR_##name: return #description;
		HTTP_WEB_SOCKET_FRAME_ERROR_MAP(XX)
		#undef XX
	}

	return http_web_socket_frame_error_description (HTTP_WEB_SOCKET_FRAME_ERROR_NONE);

}

u16 http_web_socket_frame_error_close_code (
	const HttpWebSocketFrameError error
) {

	return (error == HTTP_WEB_SOCKET_FRAME_ERROR_TOO_BIG) ?
		HTTP_WEB_SOCKET_CLOSE_MESSAGE_TOO_BIG : HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR;

}

#pragma endregion

#pragma region encoder

// writes the length in big endian with size bytes
static void http_web_socket_frame_write_length (
	u8 *destination, const u64 length, const size_t size
) {

	for (size_t i = 0; i < size; i++) {
		const size_t shift = (size - 1 - i) * HTTP_WEB_SOCKET_FRAME_BITS_PER_BYTE;
		destination[i] = (u8) ((length >> shift) & HTTP_WEB_SOCKET_FRAME_BYTE_BITS);
	}

}

size_t http_web_socket_frame_header (
	u8 *header,
	const bool fin, const bool rsv1,
	const u8 opcode, const u64 length,
	const u8 *mask
) {

	size_t size = 0;

	if (header) {
		header[0] = (u8) (opcode & HTTP_WEB_SOCKET_FRAME_OPCODE_BITS);
		if (fin) header[0] |= HTTP_WEB_SOCKET_FRAME_FIN_BIT;
		if (rsv1) header[0] |= HTTP_WEB_SOCKET_FRAME_RSV1_BIT;

		const u8 mask_bit = mask ? HTTP_WEB_SOCKET_FRAME_MASK_BIT : 0;
		size = HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE;

		if (length <= HTTP_WEB_SOCKET_FRAME_LENGTH_7_MAX) {
			header[1] = (u8) (mask_bit | (u8) length);
		}

		else if (length <= HTTP_WEB_SOCKET_FRAME_LENGTH_16_MAX) {
			header[1] = (u8) (mask_bit | HTTP_WEB_SOCKET_FRAME_LENGTH_16);
			http_web_socket_frame_write_length (header + size, length, HTTP_WEB_SOCKET_FRAME_LENGTH_16_SIZE);
			size += HTTP_WEB_SOCKET_FRAME_LENGTH_16_SIZE;
		}

		else {
			header[1] = (u8) (mask_bit | HTTP_WEB_SOCKET_FRAME_LENGTH_64);
			http_web_socket_frame_write_length (header + size, length, HTTP_WEB_SOCKET_FRAME_LENGTH_64_SIZE);
			size += HTTP_WEB_SOCKET_FRAME_LENGTH_64_SIZE;
		}

		if (mask) {
			(void) memcpy (header + size, mask, HTTP_WEB_SOCKET_MASK_SIZE);
			size += HTTP_WEB_SOCKET_MASK_SIZE;
		}
	}

	return size;

}

void http_web_socket_mask (
	u8 *data, const size_t size,
	const u8 *mask, const u64 offset
) {

	if (data && mask) {
		size_t position = 0;

		// the bytes until the mask starts again at the first byte of a word
		while ((position < size) && ((offset + position) % HTTP_WEB_SOCKET_MASK_SIZE)) {
			data[position] ^= mask[(offset + position) % HTTP_WEB_SOCKET_MASK_SIZE];
			position += 1;
		}

		// the mask repeated in a whole word
		u8 repeated[HTTP_WEB_SOCKET_MASK_WORD_SIZE];
		for (size_t i = 0; i < HTTP_WEB_SOCKET_MASK_WORD_SIZE; i++) {
			repeated[i] = mask[i % HTTP_WEB_SOCKET_MASK_SIZE];
		}

		u64 word_mask = 0;
		(void) memcpy (&word_mask, repeated, HTTP_WEB_SOCKET_MASK_WORD_SIZE);

		u64 word = 0;
		while ((size - position) >= HTTP_WEB_SOCKET_MASK_WORD_SIZE) {
			(void) memcpy (&word, data + position, HTTP_WEB_SOCKET_MASK_WORD_SIZE);
			word ^= word_mask;
			(void) memcpy (data + position, &word, HTTP_WEB_SOCKET_MASK_WORD_SIZE);
			position += HTTP_WEB_SOCKET_MASK_WORD_SIZE;
		}

		while (position < size) {
			data[position] ^= mask[(offset + position) % HTTP_WEB_SOCKET_MASK_SIZE];
			position += 1;
		}
	}

}

#pragma endregion

#pragma region parser

void http_web_socket_parser_init (
	HttpWebSocketParser *parser,
	const HttpWebSocketRole role, const u64 max_frame,
	const HttpWebSocketParserSettings *settings, void *data
) {

	if (parser) {
		(void) memset (parser, 0, sizeof (HttpWebSocketParser));

		parser->role = role;
		parser->allow_rsv1 = false;
		parser->max_frame = max_frame;

		parser->state = HTTP_WEB_SOCKET_PARSER_STATE_HEADER;
		parser->header_size = 0;
		parser->header_needed = HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE;

		parser->received = 0;
		parser->error = HTTP_WEB_SOCKET_FRAME_ERROR_NONE;

		parser->settings = settings;
		parser->data = data;
	}

}

static void http_web_socket_parser_fail (
	HttpWebSocketParser *parser, const HttpWebSocketFrameError error
) {

	parser->state = HTTP_WEB_SOCKET_PARSER_STATE_FAILED;
	parser->error = error;

}

// the rules that the first two bytes can break
static HttpWebSocketFrameError http_web_socket_parser_check_start (
	const HttpWebSocketParser *parser, const HttpWebSocketFrame *frame, const u8 length
) {

	HttpWebSocketFrameError error = HTTP_WEB_SOCKET_FRAME_ERROR_NONE;

	if (frame->rsv2 || frame->rsv3 || (frame->rsv1 && !parser->allow_rsv1)) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_RESERVED_BITS;
	}

	else if (!http_web_socket_opcode_is_known (frame->opcode)) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_OPCODE;
	}

	else if (http_web_socket_opcode_is_control (frame->opcode) && !frame->fin) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_CONTROL_FRAGMENTED;
	}

	else if (http_web_socket_opcode_is_control (frame->opcode) && (length > HTTP_WEB_SOCKET_CONTROL_MAX_SIZE)) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_CONTROL_TOO_BIG;
	}

	else if ((parser->role == HTTP_WEB_SOCKET_ROLE_SERVER) && !frame->masked) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_MASK_REQUIRED;
	}

	else if ((parser->role == HTTP_WEB_SOCKET_ROLE_CLIENT) && frame->masked) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_MASK_FORBIDDEN;
	}

	return error;

}

// the first two bytes, that say how many bytes the header has
static HttpWebSocketFrameError http_web_socket_parser_header_start (
	HttpWebSocketParser *parser
) {

	HttpWebSocketFrame *frame = &parser->frame;
	const u8 first = parser->header[0];
	const u8 second = parser->header[1];

	frame->fin = (first & HTTP_WEB_SOCKET_FRAME_FIN_BIT) != 0;
	frame->rsv1 = (first & HTTP_WEB_SOCKET_FRAME_RSV1_BIT) != 0;
	frame->rsv2 = (first & HTTP_WEB_SOCKET_FRAME_RSV2_BIT) != 0;
	frame->rsv3 = (first & HTTP_WEB_SOCKET_FRAME_RSV3_BIT) != 0;
	frame->opcode = (u8) (first & HTTP_WEB_SOCKET_FRAME_OPCODE_BITS);
	frame->masked = (second & HTTP_WEB_SOCKET_FRAME_MASK_BIT) != 0;

	const u8 length = (u8) (second & HTTP_WEB_SOCKET_FRAME_LENGTH_BITS);
	frame->length = length;

	u8 needed = HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE;
	if (length == HTTP_WEB_SOCKET_FRAME_LENGTH_16) needed += HTTP_WEB_SOCKET_FRAME_LENGTH_16_SIZE;
	else if (length == HTTP_WEB_SOCKET_FRAME_LENGTH_64) needed += HTTP_WEB_SOCKET_FRAME_LENGTH_64_SIZE;

	if (frame->masked) needed += HTTP_WEB_SOCKET_MASK_SIZE;

	parser->header_needed = needed;

	return http_web_socket_parser_check_start (parser, frame, length);

}

// reads a big endian length of size bytes
static u64 http_web_socket_parser_read_length (const u8 *source, const size_t size) {

	u64 length = 0;
	for (size_t i = 0; i < size; i++) {
		length = (length << HTTP_WEB_SOCKET_FRAME_BITS_PER_BYTE) | source[i];
	}

	return length;

}

// the extended length and the mask, when the whole header was received
static HttpWebSocketFrameError http_web_socket_parser_header_end (
	HttpWebSocketParser *parser
) {

	HttpWebSocketFrameError error = HTTP_WEB_SOCKET_FRAME_ERROR_NONE;

	HttpWebSocketFrame *frame = &parser->frame;
	size_t position = HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE;

	if (frame->length == HTTP_WEB_SOCKET_FRAME_LENGTH_16) {
		frame->length = http_web_socket_parser_read_length (
			parser->header + position, HTTP_WEB_SOCKET_FRAME_LENGTH_16_SIZE
		);

		position += HTTP_WEB_SOCKET_FRAME_LENGTH_16_SIZE;
	}

	else if (frame->length == HTTP_WEB_SOCKET_FRAME_LENGTH_64) {
		frame->length = http_web_socket_parser_read_length (
			parser->header + position, HTTP_WEB_SOCKET_FRAME_LENGTH_64_SIZE
		);

		position += HTTP_WEB_SOCKET_FRAME_LENGTH_64_SIZE;

		if (frame->length & HTTP_WEB_SOCKET_FRAME_LENGTH_64_MSB) error = HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH;
	}

	if (frame->masked) {
		(void) memcpy (frame->mask, parser->header + position, HTTP_WEB_SOCKET_MASK_SIZE);
	}

	if (!error && parser->max_frame && (frame->length > parser->max_frame)) {
		error = HTTP_WEB_SOCKET_FRAME_ERROR_TOO_BIG;
	}

	return error;

}

// the frame and its payload were parsed, the next byte starts a new header
static int http_web_socket_parser_frame_end (HttpWebSocketParser *parser) {

	const int stop = parser->settings->on_frame_end ?
		parser->settings->on_frame_end (parser, &parser->frame) : 0;

	parser->state = HTTP_WEB_SOCKET_PARSER_STATE_HEADER;
	parser->header_size = 0;
	parser->header_needed = HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE;
	parser->received = 0;

	return stop;

}

// the header is complete, the payload follows (or the frame ends when it is empty)
static int http_web_socket_parser_frame_begin (HttpWebSocketParser *parser) {

	int stop = parser->settings->on_frame_begin ?
		parser->settings->on_frame_begin (parser, &parser->frame) : 0;

	parser->state = HTTP_WEB_SOCKET_PARSER_STATE_PAYLOAD;
	parser->received = 0;

	if (!stop && !parser->frame.length) stop = http_web_socket_parser_frame_end (parser);

	return stop;

}

// takes the bytes of the header that are available
// returns the bytes that were taken, and sets stop when the parser must stop
static size_t http_web_socket_parser_header (
	HttpWebSocketParser *parser, const u8 *data, const size_t size, int *stop
) {

	size_t taken = 0;
	while ((taken < size) && (parser->header_size < parser->header_needed)) {
		parser->header[parser->header_size] = data[taken];
		parser->header_size += 1;
		taken += 1;

		HttpWebSocketFrameError error = HTTP_WEB_SOCKET_FRAME_ERROR_NONE;
		if (parser->header_size == HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE) {
			error = http_web_socket_parser_header_start (parser);
		}

		if (!error && (parser->header_size == parser->header_needed)) {
			error = http_web_socket_parser_header_end (parser);
			if (!error) *stop = http_web_socket_parser_frame_begin (parser);
		}

		if (error) {
			http_web_socket_parser_fail (parser, error);
			*stop = 1;
		}

		if (*stop || (parser->state != HTTP_WEB_SOCKET_PARSER_STATE_HEADER)) break;
	}

	return taken;

}

// takes the bytes of the payload that are available, and unmasks them in place
static size_t http_web_socket_parser_payload (
	HttpWebSocketParser *parser, u8 *data, const size_t size, int *stop
) {

	const u64 remaining = parser->frame.length - parser->received;
	const size_t taken = (remaining < (u64) size) ? (size_t) remaining : size;

	if (taken) {
		if (parser->frame.masked) {
			http_web_socket_mask (data, taken, parser->frame.mask, parser->received);
		}

		parser->received += taken;

		if (parser->settings->on_frame_data) {
			*stop = parser->settings->on_frame_data (parser, data, taken);
		}
	}

	if (!*stop && (parser->received == parser->frame.length)) {
		*stop = http_web_socket_parser_frame_end (parser);
	}

	return taken;

}

size_t http_web_socket_parser_execute (
	HttpWebSocketParser *parser, u8 *data, const size_t size
) {

	size_t parsed = 0;

	if (parser && parser->settings && (data || !size)) {
		int stop = 0;
		while (!stop && (parsed < size) && (parser->state != HTTP_WEB_SOCKET_PARSER_STATE_FAILED)) {
			if (parser->state == HTTP_WEB_SOCKET_PARSER_STATE_HEADER) {
				parsed += http_web_socket_parser_header (parser, data + parsed, size - parsed, &stop);
			}

			else {
				parsed += http_web_socket_parser_payload (parser, data + parsed, size - parsed, &stop);
			}
		}
	}

	return parsed;

}

#pragma endregion
