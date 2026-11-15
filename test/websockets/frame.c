#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/frame.h>

#include "../test.h"
#include "websocket.h"

// the frames that a parse keeps
#define FRAME_TEST_MAX_FRAMES			8

// the largest payload that is parsed for real
#define FRAME_TEST_LARGE_PAYLOAD		70000

#define FRAME_TEST_BUFFER_SIZE			(FRAME_TEST_LARGE_PAYLOAD + (1 << 10))

// the sizes of the random masks that are compared with a naive one
#define FRAME_TEST_MASK_SIZES			(1 << 6)
#define FRAME_TEST_MASK_OFFSETS			(1 << 4)

static const u8 frame_test_mask[HTTP_WEB_SOCKET_MASK_SIZE] = { 0x37, 0xFA, 0x21, 0x3D };

// what the parser reported
typedef struct FrameTestParse {

	unsigned int n_frames;
	HttpWebSocketFrame frames[FRAME_TEST_MAX_FRAMES];

	size_t payload_sizes[FRAME_TEST_MAX_FRAMES];
	u8 *payloads[FRAME_TEST_MAX_FRAMES];

	// stops the parser at the end of each frame
	bool stop_at_end;

	// stops the parser after the header and after each piece of the payload
	bool stop_at_data;

} FrameTestParse;

static int frame_test_on_begin (HttpWebSocketParser *parser, const HttpWebSocketFrame *frame) {

	FrameTestParse *parse = (FrameTestParse *) parser->data;

	test_check (parse->n_frames < FRAME_TEST_MAX_FRAMES, "too many frames");
	parse->frames[parse->n_frames] = *frame;
	parse->payload_sizes[parse->n_frames] = 0;
	parse->payloads[parse->n_frames] = (u8 *) malloc (frame->length ? (size_t) frame->length : 1);
	test_check_ptr (parse->payloads[parse->n_frames]);

	return parse->stop_at_data ? 1 : 0;

}

static int frame_test_on_data (HttpWebSocketParser *parser, u8 *data, size_t size) {

	FrameTestParse *parse = (FrameTestParse *) parser->data;

	const unsigned int current = parse->n_frames;
	test_check (
		(parse->payload_sizes[current] + size) <= parse->frames[current].length,
		"the parser gave more data than the frame has"
	);

	(void) memcpy (parse->payloads[current] + parse->payload_sizes[current], data, size);
	parse->payload_sizes[current] += size;

	return parse->stop_at_data ? 1 : 0;

}

static int frame_test_on_end (HttpWebSocketParser *parser, const HttpWebSocketFrame *frame) {

	FrameTestParse *parse = (FrameTestParse *) parser->data;

	test_check_unsigned_eq (parse->payload_sizes[parse->n_frames], (size_t) frame->length, NULL);
	parse->n_frames += 1;

	return parse->stop_at_end ? 1 : 0;

}

static const HttpWebSocketParserSettings frame_test_settings = {
	frame_test_on_begin, frame_test_on_data, frame_test_on_end
};

static void frame_test_parse_end (FrameTestParse *parse) {

	// also the payload of a frame that started and never ended
	for (unsigned int i = 0; i < FRAME_TEST_MAX_FRAMES; i++) free (parse->payloads[i]);

	(void) memset (parse, 0, sizeof (FrameTestParse));

}

// writes a frame with its payload, masked when mask is not NULL
static size_t frame_test_build (
	u8 *buffer, const bool fin, const bool rsv1, const u8 opcode,
	const u8 *payload, const size_t size, const u8 *mask
) {

	size_t length = http_web_socket_frame_header (buffer, fin, rsv1, opcode, size, mask);

	if (size) (void) memcpy (buffer + length, payload, size);
	if (mask) http_web_socket_mask (buffer + length, size, mask, 0);

	return length + size;

}

// parses the bytes in pieces of piece bytes (0 is all at once), the parser can not fail
static void frame_test_parse (
	const HttpWebSocketRole role, const u8 *bytes, const size_t size,
	const size_t piece, FrameTestParse *parse
) {

	u8 *copy = (u8 *) malloc (size ? size : 1);
	test_check_ptr (copy);
	(void) memcpy (copy, bytes, size);

	HttpWebSocketParser parser;
	http_web_socket_parser_init (&parser, role, 0, &frame_test_settings, parse);

	size_t position = 0;
	while (position < size) {
		const size_t length = (piece && ((size - position) > piece)) ? piece : (size - position);
		size_t parsed = 0;
		while (parsed < length) {
			const size_t taken = http_web_socket_parser_execute (&parser, copy + position + parsed, length - parsed);
			test_check (parser.state != HTTP_WEB_SOCKET_PARSER_STATE_FAILED, "a valid frame failed");
			test_check (taken || parse->stop_at_end, "the parser took nothing");
			parsed += taken;
		}

		position += length;
	}

	free (copy);

}

// the lengths of the three encodings and their limits
static void test_frame_lengths (void) {

	static const u64 lengths[] = {
		0, 1, 125, 126, 127, 65535, 65536, 4294967296ULL, 0x7FFFFFFFFFFFFFFFULL
	};

	static const size_t header_sizes[] = { 2, 2, 2, 4, 4, 4, 10, 10, 10 };

	u8 header[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE];
	for (size_t i = 0; i < (sizeof (lengths) / sizeof (lengths[0])); i++) {
		test_check_unsigned_eq (
			http_web_socket_frame_header (header, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, lengths[i], NULL),
			header_sizes[i], NULL
		);

		test_check_unsigned_eq (
			http_web_socket_frame_header (header, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, lengths[i], frame_test_mask),
			header_sizes[i] + HTTP_WEB_SOCKET_MASK_SIZE, NULL
		);

		// the header alone is parsed, its payload never arrives
		FrameTestParse parse;
		(void) memset (&parse, 0, sizeof (FrameTestParse));

		HttpWebSocketParser parser;
		http_web_socket_parser_init (&parser, HTTP_WEB_SOCKET_ROLE_SERVER, 0, &frame_test_settings, &parse);

		const size_t size = http_web_socket_frame_header (
			header, false, true, HTTP_WEB_SOCKET_OPCODE_TEXT, lengths[i], frame_test_mask
		);

		parser.allow_rsv1 = true;
		if (lengths[i] < FRAME_TEST_LARGE_PAYLOAD) {
			test_check_unsigned_eq (http_web_socket_parser_execute (&parser, header, size), size, NULL);
			test_check_unsigned_eq (parser.frame.length, lengths[i], NULL);
			test_check_false (parser.frame.fin);
			test_check_true (parser.frame.rsv1);
			test_check_true (parser.frame.masked);
			test_check (!memcmp (parser.frame.mask, frame_test_mask, HTTP_WEB_SOCKET_MASK_SIZE), "the mask is wrong");
		}

		frame_test_parse_end (&parse);
	}

}

// the header with the length of 64 bits, without its payload
static void test_frame_large_header (void) {

	u8 header[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE];
	const size_t size = http_web_socket_frame_header (
		header, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, 0x7FFFFFFFFFFFFFFFULL, frame_test_mask
	);

	FrameTestParse parse;
	(void) memset (&parse, 0, sizeof (FrameTestParse));

	HttpWebSocketParser parser;
	HttpWebSocketParserSettings settings = { NULL, NULL, NULL };
	http_web_socket_parser_init (&parser, HTTP_WEB_SOCKET_ROLE_SERVER, 0, &settings, &parse);

	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, header, size), size, NULL);
	test_check_unsigned_eq (parser.frame.length, 0x7FFFFFFFFFFFFFFFULL, NULL);
	test_check_int_eq (parser.state, HTTP_WEB_SOCKET_PARSER_STATE_PAYLOAD, NULL);

}

// a stream of frames of the client, parsed whole and split at every byte
static void test_frame_stream (void) {

	u8 *buffer = (u8 *) malloc (FRAME_TEST_BUFFER_SIZE);
	u8 *large = (u8 *) malloc (FRAME_TEST_LARGE_PAYLOAD);
	test_check_ptr (buffer);
	test_check_ptr (large);

	for (size_t i = 0; i < FRAME_TEST_LARGE_PAYLOAD; i++) large[i] = (u8) (i * 7);

	size_t size = 0;
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, (const u8 *) "Hello", 5, frame_test_mask);
	size += frame_test_build (buffer + size, false, false, HTTP_WEB_SOCKET_OPCODE_BINARY, large, 300, frame_test_mask);
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_PING, NULL, 0, frame_test_mask);
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, large, FRAME_TEST_LARGE_PAYLOAD, frame_test_mask);

	// the large frame makes every split of every byte too slow, its first bytes are split at each one
	const size_t pieces[] = { 0, 1, 2, 3, 5, 7, 13, 64, 1000, 4096 };
	for (size_t p = 0; p < (sizeof (pieces) / sizeof (pieces[0])); p++) {
		FrameTestParse parse;
		(void) memset (&parse, 0, sizeof (FrameTestParse));

		frame_test_parse (HTTP_WEB_SOCKET_ROLE_SERVER, buffer, size, pieces[p], &parse);

		test_check_unsigned_eq (parse.n_frames, 4, NULL);
		test_check (!memcmp (parse.payloads[0], "Hello", 5), "the text is wrong");
		test_check_int_eq (parse.frames[1].opcode, HTTP_WEB_SOCKET_OPCODE_BINARY, NULL);
		test_check_false (parse.frames[1].fin);
		test_check (!memcmp (parse.payloads[1], large, 300), "the binary is wrong");
		test_check_int_eq (parse.frames[2].opcode, HTTP_WEB_SOCKET_OPCODE_PING, NULL);
		test_check_unsigned_eq (parse.payload_sizes[2], 0, NULL);
		test_check (!memcmp (parse.payloads[3], large, FRAME_TEST_LARGE_PAYLOAD), "the large payload is wrong");

		frame_test_parse_end (&parse);
	}

	free (buffer);
	free (large);

}

// the first frames of the stream split at every byte
static void test_frame_every_byte (void) {

	u8 buffer[1 << 10];
	u8 payload[200];
	for (size_t i = 0; i < sizeof (payload); i++) payload[i] = (u8) i;

	size_t size = 0;
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, (const u8 *) "Hello", 5, NULL);
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, payload, sizeof (payload), NULL);
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, (const u8 *) "\x03\xe8", 2, NULL);

	for (size_t offset = 1; offset < size; offset++) {
		FrameTestParse parse;
		(void) memset (&parse, 0, sizeof (FrameTestParse));

		u8 *copy = (u8 *) malloc (size);
		test_check_ptr (copy);
		(void) memcpy (copy, buffer, size);

		HttpWebSocketParser parser;
		http_web_socket_parser_init (&parser, HTTP_WEB_SOCKET_ROLE_CLIENT, 0, &frame_test_settings, &parse);

		test_check_unsigned_eq (http_web_socket_parser_execute (&parser, copy, offset), offset, NULL);
		test_check_unsigned_eq (http_web_socket_parser_execute (&parser, copy + offset, size - offset), size - offset, NULL);

		test_check_unsigned_eq (parse.n_frames, 3, NULL);
		test_check (!memcmp (parse.payloads[1], payload, sizeof (payload)), "the payload is wrong");

		free (copy);
		frame_test_parse_end (&parse);
	}

}

// a frame that breaks a rule fails the parser with its error
static void frame_test_error (
	const HttpWebSocketRole role, const bool allow_rsv1, const u64 max_frame,
	const u8 *bytes, const size_t size, const HttpWebSocketFrameError error
) {

	u8 copy[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE + HTTP_WEB_SOCKET_CONTROL_MAX_SIZE + 1];
	(void) memcpy (copy, bytes, size);

	FrameTestParse parse;
	(void) memset (&parse, 0, sizeof (FrameTestParse));

	HttpWebSocketParser parser;
	http_web_socket_parser_init (&parser, role, max_frame, &frame_test_settings, &parse);
	parser.allow_rsv1 = allow_rsv1;

	(void) http_web_socket_parser_execute (&parser, copy, size);

	if ((parser.state != HTTP_WEB_SOCKET_PARSER_STATE_FAILED) || (parser.error != error)) {
		(void) fprintf (
			stderr, "expected %s, got %s\n",
			http_web_socket_frame_error_to_string (error),
			http_web_socket_frame_error_to_string (parser.error)
		);

		test_check (false, "the frame did not fail as expected");
	}

	test_check_unsigned_eq (parse.n_frames, 0, NULL);

	frame_test_parse_end (&parse);

}

static void test_frame_errors (void) {

	const HttpWebSocketRole server = HTTP_WEB_SOCKET_ROLE_SERVER;
	const HttpWebSocketRole client = HTTP_WEB_SOCKET_ROLE_CLIENT;

	frame_test_error (server, false, 0, (const u8 *) "\xA1\x80\x00\x00\x00\x00", 6, HTTP_WEB_SOCKET_FRAME_ERROR_RESERVED_BITS);
	frame_test_error (server, false, 0, (const u8 *) "\x91\x80\x00\x00\x00\x00", 6, HTTP_WEB_SOCKET_FRAME_ERROR_RESERVED_BITS);
	frame_test_error (server, true, 0, (const u8 *) "\xA1\x80\x00\x00\x00\x00", 6, HTTP_WEB_SOCKET_FRAME_ERROR_RESERVED_BITS);
	frame_test_error (server, false, 0, (const u8 *) "\xC1\x80\x00\x00\x00\x00", 6, HTTP_WEB_SOCKET_FRAME_ERROR_RESERVED_BITS);

	for (u8 opcode = 0x3; opcode <= 0xF; opcode++) {
		if ((opcode >= HTTP_WEB_SOCKET_OPCODE_CLOSE) && (opcode <= HTTP_WEB_SOCKET_OPCODE_PONG)) continue;

		const u8 bytes[] = { (u8) (0x80 | opcode), 0x80, 0, 0, 0, 0 };
		frame_test_error (server, false, 0, bytes, sizeof (bytes), HTTP_WEB_SOCKET_FRAME_ERROR_OPCODE);
	}

	frame_test_error (server, false, 0, (const u8 *) "\x09\x80\x00\x00\x00\x00", 6, HTTP_WEB_SOCKET_FRAME_ERROR_CONTROL_FRAGMENTED);
	frame_test_error (server, false, 0, (const u8 *) "\x89\xFE\x00\x7E", 4, HTTP_WEB_SOCKET_FRAME_ERROR_CONTROL_TOO_BIG);
	frame_test_error (server, false, 0, (const u8 *) "\x88\xFF", 2, HTTP_WEB_SOCKET_FRAME_ERROR_CONTROL_TOO_BIG);
	frame_test_error (server, false, 0, (const u8 *) "\x81\x05Hello", 7, HTTP_WEB_SOCKET_FRAME_ERROR_MASK_REQUIRED);
	frame_test_error (client, false, 0, (const u8 *) "\x81\x80\x00\x00\x00\x00", 6, HTTP_WEB_SOCKET_FRAME_ERROR_MASK_FORBIDDEN);
	frame_test_error (
		client, false, 0, (const u8 *) "\x82\x7F\x80\x00\x00\x00\x00\x00\x00\x00", 10,
		HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH
	);

	frame_test_error (client, false, 4, (const u8 *) "\x82\x05Hello", 7, HTTP_WEB_SOCKET_FRAME_ERROR_TOO_BIG);

	// a length with more bytes than it needs (RFC 6455 section 5.2), they were accepted
	frame_test_error (client, false, 0, (const u8 *) "\x81\x7E\x00\x05Hello", 9, HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH);
	frame_test_error (client, false, 0, (const u8 *) "\x81\x7E\x00\x7D", 4, HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH);
	frame_test_error (client, false, 0, (const u8 *) "\x81\x7E\x00\x00", 4, HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH);
	frame_test_error (
		client, false, 0, (const u8 *) "\x82\x7F\x00\x00\x00\x00\x00\x00\x00\x05", 10,
		HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH
	);

	frame_test_error (
		client, false, 0, (const u8 *) "\x82\x7F\x00\x00\x00\x00\x00\x00\xFF\xFF", 10,
		HTTP_WEB_SOCKET_FRAME_ERROR_LENGTH
	);

	test_check_unsigned_eq (
		http_web_socket_frame_error_close_code (HTTP_WEB_SOCKET_FRAME_ERROR_TOO_BIG), HTTP_WEB_SOCKET_CLOSE_MESSAGE_TOO_BIG, NULL
	);

	test_check_unsigned_eq (
		http_web_socket_frame_error_close_code (HTTP_WEB_SOCKET_FRAME_ERROR_OPCODE), HTTP_WEB_SOCKET_CLOSE_PROTOCOL_ERROR, NULL
	);

}

// the first lengths of the 16 and of the 64 bits are the shortest encodings of their values
static void test_frame_minimal (void) {

	FrameTestParse parse;
	(void) memset (&parse, 0, sizeof (FrameTestParse));

	u8 *bytes = (u8 *) calloc (1, FRAME_TEST_BUFFER_SIZE);
	test_check_ptr (bytes);

	bytes[0] = 0x82;
	bytes[1] = 0x7E;
	bytes[2] = 0x00;
	bytes[3] = 0x7E;
	frame_test_parse (HTTP_WEB_SOCKET_ROLE_CLIENT, bytes, 4 + 126, 0, &parse);
	test_check_unsigned_eq (parse.n_frames, 1, NULL);
	test_check_unsigned_eq (parse.payload_sizes[0], 126, NULL);
	frame_test_parse_end (&parse);

	(void) memset (bytes, 0, FRAME_TEST_BUFFER_SIZE);
	bytes[0] = 0x82;
	bytes[1] = 0x7F;
	bytes[7] = 0x01;
	frame_test_parse (HTTP_WEB_SOCKET_ROLE_CLIENT, bytes, 10 + 65536, 0, &parse);
	test_check_unsigned_eq (parse.n_frames, 1, NULL);
	test_check_unsigned_eq (parse.payload_sizes[0], 65536, NULL);
	frame_test_parse_end (&parse);

	free (bytes);

}

// the mask applied a piece at a time matches a naive one for every offset
static void test_frame_mask (void) {

	u8 data[FRAME_TEST_MASK_SIZES + FRAME_TEST_MASK_OFFSETS];
	u8 expected[FRAME_TEST_MASK_SIZES + FRAME_TEST_MASK_OFFSETS];

	for (size_t size = 0; size < FRAME_TEST_MASK_SIZES; size++) {
		for (size_t offset = 0; offset < FRAME_TEST_MASK_OFFSETS; offset++) {
			for (size_t i = 0; i < size; i++) {
				data[i] = (u8) (i * 31 + offset);
				expected[i] = (u8) (data[i] ^ frame_test_mask[(offset + i) % HTTP_WEB_SOCKET_MASK_SIZE]);
			}

			http_web_socket_mask (data, size, frame_test_mask, offset);
			test_check (!memcmp (data, expected, size), "the mask does not match the naive one");
		}
	}

}

// a method that stops the parser at the end of each frame, it continues with the rest
static void test_frame_stop (void) {

	u8 buffer[1 << 6];
	size_t size = 0;
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, (const u8 *) "one", 3, NULL);
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, (const u8 *) "two", 3, NULL);

	FrameTestParse parse;
	(void) memset (&parse, 0, sizeof (FrameTestParse));
	parse.stop_at_end = true;

	HttpWebSocketParser parser;
	http_web_socket_parser_init (&parser, HTTP_WEB_SOCKET_ROLE_CLIENT, 0, &frame_test_settings, &parse);

	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer, size), 5, NULL);
	test_check_unsigned_eq (parse.n_frames, 1, NULL);
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer + 5, size - 5), 5, NULL);
	test_check_unsigned_eq (parse.n_frames, 2, NULL);

	frame_test_parse_end (&parse);

}

// a method that stops the parser with the last bytes of a payload: the frame ends in the next call,
// that has no bytes to give, it waited for the bytes of the next frame
static void test_frame_stop_at_data (void) {

	u8 buffer[1 << 6];
	size_t size = frame_test_build (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, (const u8 *) "one", 3, NULL);
	const size_t empty = size;
	size += frame_test_build (buffer + size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL, 0, NULL);

	FrameTestParse parse;
	(void) memset (&parse, 0, sizeof (FrameTestParse));
	parse.stop_at_data = true;

	HttpWebSocketParser parser;
	http_web_socket_parser_init (&parser, HTTP_WEB_SOCKET_ROLE_CLIENT, 0, &frame_test_settings, &parse);

	// the header, the payload and the end
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer, empty), 2, NULL);
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer + 2, empty - 2), 3, NULL);
	test_check_unsigned_eq (parse.payload_sizes[0], 3, NULL);
	test_check_unsigned_eq (parse.n_frames, 0, NULL);
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer + empty, 0), 0, NULL);
	test_check_unsigned_eq (parse.n_frames, 1, NULL);

	// a frame without payload whose begin stopped the parser ends the same way
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer + empty, size - empty), 2, NULL);
	test_check_unsigned_eq (parse.n_frames, 1, NULL);
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer + size, 0), 0, NULL);
	test_check_unsigned_eq (parse.n_frames, 2, NULL);

	// and nothing ends twice
	test_check_unsigned_eq (http_web_socket_parser_execute (&parser, buffer + size, 0), 0, NULL);
	test_check_unsigned_eq (parse.n_frames, 2, NULL);

	frame_test_parse_end (&parse);

}

static void test_frame_close_codes (void) {

	static const u16 valid[] = { 1000, 1001, 1002, 1003, 1007, 1008, 1009, 1010, 1011, 1012, 1013, 1014, 3000, 3999, 4000, 4999 };
	static const u16 invalid[] = { 0, 999, 1004, 1005, 1006, 1015, 1016, 1100, 2000, 2999, 5000, 65535 };

	for (size_t i = 0; i < (sizeof (valid) / sizeof (valid[0])); i++) {
		test_check_true (http_web_socket_close_code_is_valid (valid[i]));
	}

	for (size_t i = 0; i < (sizeof (invalid) / sizeof (invalid[0])); i++) {
		test_check_false (http_web_socket_close_code_is_valid (invalid[i]));
	}

	test_check_str_eq (http_web_socket_close_code_to_string (1009), "Message Too Big", NULL);
	test_check_str_eq (http_web_socket_opcode_to_string (HTTP_WEB_SOCKET_OPCODE_PONG), "Pong", NULL);

}

void web_socket_tests_frame (void) {

	(void) printf ("Testing WebSocket frames...\n");

	test_run (test_frame_lengths);
	test_run (test_frame_large_header);
	test_run (test_frame_stream);
	test_run (test_frame_every_byte);
	test_run (test_frame_errors);
	test_run (test_frame_minimal);
	test_run (test_frame_mask);
	test_run (test_frame_stop);
	test_run (test_frame_stop_at_data);
	test_run (test_frame_close_codes);

	(void) printf ("Done!\n");

}
