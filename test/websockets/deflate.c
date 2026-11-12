#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef CERVER_ZLIB
#include <zlib.h>
#endif

#include <cerver/types/types.h>

#include <cerver/http/websockets/deflate.h>
#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/message.h>

#include "../test.h"

#include "websocket.h"

#define DEFLATE_TEST_OUTPUT_SIZE			(1 << 20)
#define DEFLATE_TEST_MESSAGES				8
#define DEFLATE_TEST_MESSAGE_SIZE			(1 << 14)

// the most bytes of a message in the test of the decompression bomb
#define DEFLATE_TEST_BOMB_LIMIT				(1 << 12)
#define DEFLATE_TEST_BOMB_SIZE				(1 << 20)

static HttpWebSocketDeflateOptions deflate_test_options (void) {

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = true;

	return options;

}

// the server negotiates the offers, and returns the value of its response, "" when it declined them
static const char *deflate_test_negotiate (
	const char *offers, const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
) {

	static char response[HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE];
	response[0] = '\0';

	if (http_web_socket_deflate_negotiate (offers, strlen (offers), options, parameters)) {
		test_check (
			http_web_socket_deflate_response (parameters, response, HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE) > 0,
			"the response did not fit"
		);
	}

	return response;

}

static void test_deflate_negotiate (void) {

	HttpWebSocketDeflateOptions options = deflate_test_options ();
	HttpWebSocketDeflateParameters parameters;

	test_check_str_eq (deflate_test_negotiate ("permessage-deflate", &options, &parameters), "permessage-deflate", NULL);

	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate; client_max_window_bits", &options, &parameters),
		"permessage-deflate", NULL
	);

	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate; server_max_window_bits=10", &options, &parameters),
		"permessage-deflate; server_max_window_bits=10", NULL
	);

	test_check_unsigned_eq (parameters.server_max_window_bits, 10, NULL);

	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate; server_max_window_bits=\"12\"", &options, &parameters),
		"permessage-deflate; server_max_window_bits=12", NULL
	);

	test_check_str_eq (
		deflate_test_negotiate (
			"x-webkit-deflate-frame, permessage-deflate; client_no_context_takeover; server_no_context_takeover",
			&options, &parameters
		),
		"permessage-deflate; server_no_context_takeover; client_no_context_takeover", NULL
	);

	test_check_true (parameters.server_no_context_takeover);
	test_check_true (parameters.client_no_context_takeover);

	// zlib can not compress with a window of 2^8, the next offer is taken
	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate; server_max_window_bits=8", &options, &parameters), "", NULL
	);

	test_check_str_eq (
		deflate_test_negotiate (
			"permessage-deflate; server_max_window_bits=8, permessage-deflate; server_max_window_bits=9",
			&options, &parameters
		),
		"permessage-deflate; server_max_window_bits=9", NULL
	);

	// the options of the server
	options.no_context_takeover = true;
	options.window_bits = 11;
	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate", &options, &parameters),
		"permessage-deflate; server_no_context_takeover; client_no_context_takeover; server_max_window_bits=11", NULL
	);

	options.enabled = false;
	test_check_str_eq (deflate_test_negotiate ("permessage-deflate", &options, &parameters), "", NULL);

}

// the offers that are declined, and the ones after a broken one
static void test_deflate_declined (void) {

	const HttpWebSocketDeflateOptions options = deflate_test_options ();
	HttpWebSocketDeflateParameters parameters;

	static const char *declined[] = {
		"",
		"deflate",
		"permessage-deflate; unknown",
		"permessage-deflate; server_no_context_takeover; server_no_context_takeover",
		"permessage-deflate; server_no_context_takeover=1",
		"permessage-deflate; server_max_window_bits",
		"permessage-deflate; server_max_window_bits=16",
		"permessage-deflate; server_max_window_bits=7",
		"permessage-deflate; server_max_window_bits=09",
		"permessage-deflate; server_max_window_bits=abc",
		"permessage-deflate; client_max_window_bits=16",
		"permessage-deflate; client_max_window_bits=1x",
		"permessage-deflate; server_max_window_bits=\"10",
		"permessage-deflate; =10",
		"permessage-deflate;",
		"permessage-deflate x",
		"permessage-deflate; server_max_window_bits=10; server_max_window_bits=10",
		"permessage-deflate; server_max_window_bits=8"
	};

	for (size_t i = 0; i < (sizeof (declined) / sizeof (declined[0])); i++) {
		if (deflate_test_negotiate (declined[i], &options, &parameters)[0]) {
			(void) fprintf (stderr, "offer: %s\n", declined[i]);
			test_check (false, "an offer that is not valid was accepted");
		}
	}

	// a broken offer stops the list, nothing after it can be trusted
	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate; =, permessage-deflate", &options, &parameters), "", NULL
	);

	// an invalid offer is skipped, the next one is taken
	test_check_str_eq (
		deflate_test_negotiate ("permessage-deflate; unknown, permessage-deflate", &options, &parameters),
		"permessage-deflate", NULL
	);

}

static bool deflate_test_accept (const char *response, HttpWebSocketDeflateParameters *parameters) {

	const HttpWebSocketDeflateOptions options = deflate_test_options ();

	return http_web_socket_deflate_accept (response, strlen (response), &options, parameters);

}

// the client validates the response to its offer
static void test_deflate_client (void) {

	HttpWebSocketDeflateOptions options = deflate_test_options ();
	HttpWebSocketDeflateParameters parameters;

	char offer[HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE];
	test_check (http_web_socket_deflate_offer (&options, offer, sizeof (offer)) > 0, NULL);
	test_check_str_eq (offer, "permessage-deflate; client_max_window_bits", NULL);

	options.no_context_takeover = true;
	options.window_bits = 12;
	test_check (http_web_socket_deflate_offer (&options, offer, sizeof (offer)) > 0, NULL);
	test_check_str_eq (
		offer, "permessage-deflate; server_no_context_takeover; client_no_context_takeover; client_max_window_bits=12", NULL
	);

	test_check_true (deflate_test_accept ("permessage-deflate", &parameters));
	test_check_true (deflate_test_accept ("permessage-deflate; client_max_window_bits=10", &parameters));
	test_check_unsigned_eq (parameters.client_max_window_bits, 10, NULL);
	test_check_true (deflate_test_accept ("permessage-deflate; server_no_context_takeover", &parameters));
	test_check_true (parameters.server_no_context_takeover);

	test_check_false (deflate_test_accept ("", &parameters));
	test_check_false (deflate_test_accept ("permessage-deflate, permessage-deflate", &parameters));
	test_check_false (deflate_test_accept ("x-unknown", &parameters));
	test_check_false (deflate_test_accept ("permessage-deflate; client_max_window_bits", &parameters));
	test_check_false (deflate_test_accept ("permessage-deflate; client_max_window_bits=8", &parameters));
	test_check_false (deflate_test_accept ("permessage-deflate; unknown", &parameters));

}

#ifdef CERVER_ZLIB

static const u8 deflate_test_mask[HTTP_WEB_SOCKET_MASK_SIZE] = { 0x0A, 0x0B, 0x0C, 0x0D };

typedef struct DeflateTestSink {

	u8 *data;
	size_t size;

} DeflateTestSink;

static int deflate_test_sink (void *sink_ptr, const u8 *bytes, size_t size) {

	DeflateTestSink *sink = (DeflateTestSink *) sink_ptr;

	test_check ((sink->size + size) <= DEFLATE_TEST_OUTPUT_SIZE, "the output is too big");
	(void) memcpy (sink->data + sink->size, bytes, size);
	sink->size += size;

	return 0;

}

static HttpWebSocketDeflate *deflate_test_create (
	const HttpWebSocketRole role, const HttpWebSocketDeflateParameters *parameters
) {

	const HttpWebSocketDeflateOptions options = deflate_test_options ();

	HttpWebSocketDeflate *deflate = http_web_socket_deflate_create (role, parameters, &options);
	test_check_ptr (deflate);

	return deflate;

}

// the examples of RFC 7692 section 7.2.3: "Hello" alone, and two of them with a shared window
static void test_deflate_examples (void) {

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	HttpWebSocketDeflate *client = deflate_test_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &parameters);

	u8 output[1 << 6];
	DeflateTestSink sink = { output, 0 };

	static const u8 hello[] = { 0xf2, 0x48, 0xcd, 0xc9, 0xc9, 0x07, 0x00 };
	test_check_unsigned_eq (http_web_socket_deflate_decompress (client, hello, sizeof (hello), true, deflate_test_sink, &sink), 0, NULL);
	test_check_unsigned_eq (sink.size, 5, NULL);
	test_check (!memcmp (output, "Hello", 5), "the first Hello is wrong");

	// the second message uses the window of the first
	static const u8 second[] = { 0xf2, 0x00, 0x11, 0x00, 0x00 };
	sink.size = 0;
	test_check_unsigned_eq (http_web_socket_deflate_decompress (client, second, sizeof (second), true, deflate_test_sink, &sink), 0, NULL);
	test_check (!memcmp (output, "Hello", 5), "the second Hello is wrong");

	// an empty message is a single 0x00
	HttpWebSocketDeflate *server = deflate_test_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters);
	const u8 *compressed = NULL;
	size_t compressed_size = 0;
	test_check_unsigned_eq (http_web_socket_deflate_compress (server, NULL, 0, true, &compressed, &compressed_size), 0, NULL);
	test_check_unsigned_eq (compressed_size, 1, NULL);
	test_check_unsigned_eq (compressed[0], 0, NULL);

	// "Hello" alone is the same as the example
	parameters.server_no_context_takeover = true;
	HttpWebSocketDeflate *alone = deflate_test_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters);
	test_check_unsigned_eq (http_web_socket_deflate_compress (alone, "Hello", 5, true, &compressed, &compressed_size), 0, NULL);
	test_check_unsigned_eq (compressed_size, sizeof (hello), NULL);
	test_check (!memcmp (compressed, hello, sizeof (hello)), "the compressed Hello is not the one of the example");

	http_web_socket_deflate_delete (client);
	http_web_socket_deflate_delete (server);
	http_web_socket_deflate_delete (alone);

}

// fills a message with text that compresses, different in each message
static void deflate_test_message (u8 *message, const size_t size, const unsigned int n) {

	for (size_t i = 0; i < size; i++) message[i] = (u8) ('a' + ((i / (n + 1)) % 26));

}

// the messages of the server go through the decompressor of the client,
// with every combination of the context takeover and the windows
static void test_deflate_round_trip (void) {

	u8 *message = (u8 *) malloc (DEFLATE_TEST_MESSAGE_SIZE);
	u8 *output = (u8 *) malloc (DEFLATE_TEST_OUTPUT_SIZE);
	test_check_ptr (message);
	test_check_ptr (output);

	for (int takeover = 0; takeover <= 1; takeover++) {
		for (u8 bits = HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS; bits <= HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS; bits++) {
			HttpWebSocketDeflateParameters parameters;
			(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
			parameters.server_no_context_takeover = !takeover;
			parameters.server_max_window_bits = bits;
			parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

			HttpWebSocketDeflate *server = deflate_test_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters);
			HttpWebSocketDeflate *client = deflate_test_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &parameters);

			for (unsigned int n = 0; n < DEFLATE_TEST_MESSAGES; n++) {
				const size_t size = (n * DEFLATE_TEST_MESSAGE_SIZE) / DEFLATE_TEST_MESSAGES;
				deflate_test_message (message, size, n);

				const u8 *compressed = NULL;
				size_t compressed_size = 0;
				test_check_unsigned_eq (
					http_web_socket_deflate_compress (server, message, size, true, &compressed, &compressed_size), 0, NULL
				);

				// the compressed message is decompressed in pieces of 3 bytes
				DeflateTestSink sink = { output, 0 };
				for (size_t position = 0; position < compressed_size; position += 3) {
					const size_t piece = ((compressed_size - position) > 3) ? 3 : (compressed_size - position);
					test_check_unsigned_eq (
						http_web_socket_deflate_decompress (client, compressed + position, piece, false, deflate_test_sink, &sink),
						0, NULL
					);
				}

				test_check_unsigned_eq (http_web_socket_deflate_decompress (client, NULL, 0, true, deflate_test_sink, &sink), 0, NULL);
				test_check_unsigned_eq (sink.size, size, NULL);
				test_check (!memcmp (output, message, size), "the message changed in its round trip");
			}

			http_web_socket_deflate_delete (server);
			http_web_socket_deflate_delete (client);
		}
	}

	free (message);
	free (output);

}

// a message compressed in pieces, as http_web_socket_send_write () does
static void test_deflate_stream (void) {

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	HttpWebSocketDeflate *server = deflate_test_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters);
	HttpWebSocketDeflate *client = deflate_test_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &parameters);

	u8 *message = (u8 *) malloc (DEFLATE_TEST_MESSAGE_SIZE);
	u8 *output = (u8 *) malloc (DEFLATE_TEST_OUTPUT_SIZE);
	test_check_ptr (message);
	test_check_ptr (output);
	deflate_test_message (message, DEFLATE_TEST_MESSAGE_SIZE, 3);

	DeflateTestSink sink = { output, 0 };

	const size_t pieces = 7;
	const size_t piece = DEFLATE_TEST_MESSAGE_SIZE / pieces;
	for (size_t i = 0; i <= pieces; i++) {
		const bool end = (i == pieces);
		const size_t size = end ? (DEFLATE_TEST_MESSAGE_SIZE - (piece * pieces)) : piece;

		const u8 *compressed = NULL;
		size_t compressed_size = 0;
		test_check_unsigned_eq (
			http_web_socket_deflate_compress (server, message + (i * piece), size, end, &compressed, &compressed_size), 0, NULL
		);

		test_check_unsigned_eq (
			http_web_socket_deflate_decompress (client, compressed, compressed_size, end, deflate_test_sink, &sink), 0, NULL
		);
	}

	test_check_unsigned_eq (sink.size, DEFLATE_TEST_MESSAGE_SIZE, NULL);
	test_check (!memcmp (output, message, DEFLATE_TEST_MESSAGE_SIZE), "the streamed message changed");

	http_web_socket_deflate_delete (server);
	http_web_socket_deflate_delete (client);
	free (message);
	free (output);

}

static int deflate_test_count (void *data, HttpWebSocketOpcode type, const u8 *payload, size_t size) {

	(void) type;
	(void) payload;
	(void) size;

	*((unsigned int *) data) += 1;

	return 0;

}

static const HttpWebSocketReceiverHandlers deflate_test_handlers = {
	deflate_test_count, NULL, NULL, NULL, NULL
};

// a small frame that decompresses to more than the limit fails with 1009
// without decompressing it all
static void test_deflate_bomb (void) {

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	HttpWebSocketDeflate *client = deflate_test_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &parameters);
	HttpWebSocketDeflate *server = deflate_test_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters);

	u8 *zeros = (u8 *) calloc (1, DEFLATE_TEST_BOMB_SIZE);
	test_check_ptr (zeros);

	const u8 *compressed = NULL;
	size_t compressed_size = 0;
	test_check_unsigned_eq (
		http_web_socket_deflate_compress (client, zeros, DEFLATE_TEST_BOMB_SIZE, true, &compressed, &compressed_size), 0, NULL
	);

	test_check (compressed_size < DEFLATE_TEST_BOMB_LIMIT, "the bomb is not small");

	u8 *frame = (u8 *) malloc (compressed_size + HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE);
	test_check_ptr (frame);
	size_t size = http_web_socket_frame_header (frame, true, true, HTTP_WEB_SOCKET_OPCODE_BINARY, compressed_size, deflate_test_mask);
	(void) memcpy (frame + size, compressed, compressed_size);
	http_web_socket_mask (frame + size, compressed_size, deflate_test_mask, 0);
	size += compressed_size;

	unsigned int n_messages = 0;
	HttpWebSocketReceiver receiver;
	http_web_socket_receiver_init (
		&receiver, HTTP_WEB_SOCKET_ROLE_SERVER, DEFLATE_TEST_BOMB_LIMIT, DEFLATE_TEST_BOMB_LIMIT,
		server, &deflate_test_handlers, &n_messages
	);

	test_check_int_eq (http_web_socket_receiver_execute (&receiver, frame, size, NULL), HTTP_WEB_SOCKET_RECEIVE_FAILED, NULL);
	test_check_unsigned_eq (receiver.close_code, HTTP_WEB_SOCKET_CLOSE_MESSAGE_TOO_BIG, NULL);
	test_check_unsigned_eq (n_messages, 0, NULL);
	test_check (receiver.message_capacity <= DEFLATE_TEST_BOMB_LIMIT, "the bomb was kept in memory");

	http_web_socket_receiver_end (&receiver);
	http_web_socket_deflate_delete (client);
	http_web_socket_deflate_delete (server);
	free (zeros);
	free (frame);

}

// a message whose last block (BFINAL) comes in its first fragment, and an empty last fragment
static void test_deflate_final_block (void) {

	u8 compressed[1 << 6];
	z_stream stream;
	(void) memset (&stream, 0, sizeof (z_stream));
	test_check_int_eq (deflateInit2 (&stream, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY), Z_OK, NULL);
	stream.next_in = (Bytef *) "Final";
	stream.avail_in = 5;
	stream.next_out = compressed;
	stream.avail_out = sizeof (compressed);
	test_check_int_eq (deflate (&stream, Z_FINISH), Z_STREAM_END, NULL);
	const size_t compressed_size = sizeof (compressed) - stream.avail_out;
	(void) deflateEnd (&stream);

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	HttpWebSocketDeflate *client = deflate_test_create (HTTP_WEB_SOCKET_ROLE_CLIENT, &parameters);

	u8 output[1 << 6];
	for (int message = 0; message < 2; message++) {
		DeflateTestSink sink = { output, 0 };
		test_check_unsigned_eq (http_web_socket_deflate_decompress (client, compressed, compressed_size, false, deflate_test_sink, &sink), 0, NULL);
		test_check_unsigned_eq (http_web_socket_deflate_decompress (client, NULL, 0, true, deflate_test_sink, &sink), 0, NULL);
		test_check_unsigned_eq (sink.size, 5, NULL);
		test_check (!memcmp (output, "Final", 5), "the final block is wrong");
	}

	// nothing can follow the last block of a message
	DeflateTestSink sink = { output, 0 };
	test_check_unsigned_eq (http_web_socket_deflate_decompress (client, compressed, compressed_size, false, deflate_test_sink, &sink), 0, NULL);
	test_check_unsigned_eq (http_web_socket_deflate_decompress (client, compressed, compressed_size, true, deflate_test_sink, &sink), 1, NULL);

	http_web_socket_deflate_delete (client);

}

#endif

void web_socket_tests_deflate (void) {

	(void) printf ("Testing WebSocket permessage-deflate...\n");

	if (http_web_socket_deflate_available ()) {
		test_run (test_deflate_negotiate);
		test_run (test_deflate_declined);
		test_run (test_deflate_client);

		#ifdef CERVER_ZLIB
		test_run (test_deflate_examples);
		test_run (test_deflate_round_trip);
		test_run (test_deflate_stream);
		test_run (test_deflate_bomb);
		test_run (test_deflate_final_block);
		#endif
	}

	else {
		HttpWebSocketDeflateOptions options = deflate_test_options ();
		HttpWebSocketDeflateParameters parameters;
		test_check_false (http_web_socket_deflate_negotiate ("permessage-deflate", 18, &options, &parameters));
		test_check_null_ptr (http_web_socket_deflate_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters, &options));
	}

	(void) printf ("Done!\n");

}
