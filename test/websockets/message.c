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

#define MESSAGE_TEST_BUFFER_SIZE		(1 << 16)
#define MESSAGE_TEST_MAX_MESSAGE		(1 << 12)
#define MESSAGE_TEST_MAX_EVENTS			16
#define MESSAGE_TEST_EVENT_SIZE			(1 << 13)

static const u8 message_test_mask[HTTP_WEB_SOCKET_MASK_SIZE] = { 0x01, 0x02, 0x03, 0x04 };

typedef enum MessageTestEventType {

	MESSAGE_TEST_EVENT_MESSAGE = 0,
	MESSAGE_TEST_EVENT_FRAGMENT = 1,
	MESSAGE_TEST_EVENT_PING = 2,
	MESSAGE_TEST_EVENT_PONG = 3,
	MESSAGE_TEST_EVENT_CLOSE = 4

} MessageTestEventType;

typedef struct MessageTestEvent {

	MessageTestEventType type;
	HttpWebSocketOpcode opcode;

	bool first;
	bool last;

	u16 code;

	size_t size;
	u8 data[MESSAGE_TEST_EVENT_SIZE];

} MessageTestEvent;

// what the receiver reported, the pieces of a streamed message are joined in the last event
typedef struct MessageTestEvents {

	unsigned int n_events;
	MessageTestEvent events[MESSAGE_TEST_MAX_EVENTS];

	// a method returns 1 once to stop the receiver
	bool stop_once;

	// the event of the message that is streamed, its pieces can have control frames between them
	unsigned int fragment_event;

} MessageTestEvents;

static MessageTestEvent *message_test_event (MessageTestEvents *events, const MessageTestEventType type) {

	test_check (events->n_events < MESSAGE_TEST_MAX_EVENTS, "too many events");

	MessageTestEvent *event = &events->events[events->n_events];
	(void) memset (event, 0, sizeof (MessageTestEvent));
	event->type = type;
	events->n_events += 1;

	return event;

}

static void message_test_event_data (MessageTestEvent *event, const u8 *data, const size_t size) {

	test_check ((event->size + size) <= MESSAGE_TEST_EVENT_SIZE, "the event is too big");
	if (size) (void) memcpy (event->data + event->size, data, size);
	event->size += size;

}

static int message_test_stop (MessageTestEvents *events) {

	const int stop = events->stop_once ? 1 : 0;
	events->stop_once = false;

	return stop;

}

static int message_test_on_message (void *data, HttpWebSocketOpcode type, const u8 *payload, size_t size) {

	MessageTestEvents *events = (MessageTestEvents *) data;
	MessageTestEvent *event = message_test_event (events, MESSAGE_TEST_EVENT_MESSAGE);
	event->opcode = type;
	message_test_event_data (event, payload, size);

	return message_test_stop (events);

}

// the pieces of a message are joined in one event
static int message_test_on_fragment (
	void *data, HttpWebSocketOpcode type,
	const u8 *payload, size_t size, bool first, bool last
) {

	MessageTestEvents *events = (MessageTestEvents *) data;

	if (first) {
		(void) message_test_event (events, MESSAGE_TEST_EVENT_FRAGMENT);
		events->fragment_event = events->n_events - 1;
	}

	MessageTestEvent *event = &events->events[events->fragment_event];

	if (first) event->first = true;
	event->last = last;
	event->opcode = type;
	message_test_event_data (event, payload, size);

	return 0;

}

static int message_test_on_ping (void *data, const u8 *payload, size_t size) {

	MessageTestEvent *event = message_test_event ((MessageTestEvents *) data, MESSAGE_TEST_EVENT_PING);
	message_test_event_data (event, payload, size);

	return 0;

}

static int message_test_on_pong (void *data, const u8 *payload, size_t size) {

	MessageTestEvent *event = message_test_event ((MessageTestEvents *) data, MESSAGE_TEST_EVENT_PONG);
	message_test_event_data (event, payload, size);

	return 0;

}

static int message_test_on_close (void *data, u16 code, const char *reason, size_t length) {

	MessageTestEvent *event = message_test_event ((MessageTestEvents *) data, MESSAGE_TEST_EVENT_CLOSE);
	event->code = code;
	message_test_event_data (event, (const u8 *) reason, length);

	return 0;

}

static const HttpWebSocketReceiverHandlers message_test_handlers = {
	message_test_on_message, NULL, message_test_on_ping, message_test_on_pong, message_test_on_close
};

static const HttpWebSocketReceiverHandlers message_test_stream_handlers = {
	message_test_on_message, message_test_on_fragment, message_test_on_ping, message_test_on_pong, message_test_on_close
};

// appends a masked frame of the client
static size_t message_test_frame (
	u8 *buffer, const size_t position,
	const bool fin, const bool rsv1, const u8 opcode,
	const void *payload, const size_t size
) {

	u8 *start = buffer + position;
	size_t length = http_web_socket_frame_header (start, fin, rsv1, opcode, size, message_test_mask);

	if (size) (void) memcpy (start + length, payload, size);
	http_web_socket_mask (start + length, size, message_test_mask, 0);

	return position + length + size;

}

// receives the bytes split at offset (0 is whole) with a new receiver of the server
static HttpWebSocketReceiveResult message_test_receive (
	const u8 *bytes, const size_t size, const size_t offset,
	const bool streaming, HttpWebSocketDeflate *deflate,
	MessageTestEvents *events, u16 *close_code, size_t *parsed
) {

	(void) memset (events, 0, sizeof (MessageTestEvents));

	u8 *copy = (u8 *) malloc (size ? size : 1);
	test_check_ptr (copy);
	(void) memcpy (copy, bytes, size);

	HttpWebSocketReceiver receiver;
	http_web_socket_receiver_init (
		&receiver, HTTP_WEB_SOCKET_ROLE_SERVER,
		MESSAGE_TEST_MAX_MESSAGE, MESSAGE_TEST_MAX_MESSAGE, deflate,
		streaming ? &message_test_stream_handlers : &message_test_handlers, events
	);

	size_t total = 0;
	size_t taken = 0;
	HttpWebSocketReceiveResult result = HTTP_WEB_SOCKET_RECEIVE_CONTINUE;
	if (offset) {
		result = http_web_socket_receiver_execute (&receiver, copy, offset, &taken);
		total += taken;
	}

	if (result == HTTP_WEB_SOCKET_RECEIVE_CONTINUE) {
		result = http_web_socket_receiver_execute (&receiver, copy + offset, size - offset, &taken);
		total += taken;
	}

	*close_code = receiver.close_code;
	if (parsed) *parsed = total;

	http_web_socket_receiver_end (&receiver);
	free (copy);

	return result;

}

// a text in three fragments with a ping between them, a binary and a close, split at every byte
static void test_message_fragments (void) {

	u8 *buffer = (u8 *) malloc (MESSAGE_TEST_BUFFER_SIZE);
	test_check_ptr (buffer);

	// "héllo €" with the é and the € split between the fragments
	size_t size = 0;
	size = message_test_frame (buffer, size, false, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "h\xC3", 2);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_PING, "ping", 4);
	size = message_test_frame (buffer, size, false, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "\xA9llo \xE2", 6);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "\x82\xAC", 2);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, "\x00\xFF", 2);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xE8" "bye", 5);

	for (size_t offset = 0; offset < size; offset++) {
		for (int streaming = 0; streaming <= 1; streaming++) {
			MessageTestEvents events;
			u16 code = 0;
			const HttpWebSocketReceiveResult result = message_test_receive (
				buffer, size, offset, streaming, NULL, &events, &code, NULL
			);

			test_check_int_eq (result, HTTP_WEB_SOCKET_RECEIVE_CLOSED, NULL);
			test_check_unsigned_eq (code, 1000, NULL);
			test_check_unsigned_eq (events.n_events, 4, NULL);

			// a streamed message starts with its first piece, before the ping, a whole one ends after it
			const unsigned int ping = streaming ? 1 : 0;
			const unsigned int text = streaming ? 0 : 1;

			test_check_int_eq (events.events[ping].type, MESSAGE_TEST_EVENT_PING, NULL);
			test_check (!memcmp (events.events[ping].data, "ping", 4), "the ping is wrong");
			test_check_int_eq (events.events[text].opcode, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL);
			test_check_unsigned_eq (events.events[text].size, 10, NULL);
			test_check (!memcmp (events.events[text].data, "h\xC3\xA9llo \xE2\x82\xAC", 10), "the text is wrong");
			test_check_int_eq (events.events[2].opcode, HTTP_WEB_SOCKET_OPCODE_BINARY, NULL);
			test_check_int_eq (events.events[3].type, MESSAGE_TEST_EVENT_CLOSE, NULL);
			test_check (!memcmp (events.events[3].data, "bye", 3), "the reason is wrong");

			if (streaming) {
				test_check_true (events.events[text].first);
				test_check_true (events.events[text].last);
			}
		}
	}

	free (buffer);

}

// the frames that fail the connection, each one with its close code
static void message_test_failure (
	const char *description, const u8 *bytes, const size_t size,
	HttpWebSocketDeflate *deflate, const u16 expected
) {

	MessageTestEvents events;
	u16 code = 0;
	const HttpWebSocketReceiveResult result = message_test_receive (
		bytes, size, 0, false, deflate, &events, &code, NULL
	);

	if ((result != HTTP_WEB_SOCKET_RECEIVE_FAILED) || (code != expected)) {
		(void) fprintf (
			stderr, "%s: %s with %u, expected %u\n",
			description, http_web_socket_receive_result_to_string (result), code, expected
		);

		test_check (false, "a broken message did not fail as expected");
	}

	// nothing of a failed message reaches the application
	for (unsigned int i = 0; i < events.n_events; i++) {
		test_check (events.events[i].type != MESSAGE_TEST_EVENT_MESSAGE, "a failed message was delivered");
	}

}

static void test_message_failures (void) {

	u8 buffer[1 << 14];
	size_t size = 0;

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "a", 1);
	message_test_failure ("a continuation without a message", buffer, size, NULL, 1002);

	size = message_test_frame (buffer, 0, false, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "a", 1);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "b", 1);
	message_test_failure ("a text inside another one", buffer, size, NULL, 1002);

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "a\xFF", 2);
	message_test_failure ("a text that is not UTF-8", buffer, size, NULL, 1007);

	size = message_test_frame (buffer, 0, false, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "\xF4\x90", 2);
	message_test_failure ("the first fragment fails before its message ends", buffer, size, NULL, 1007);

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "\xE2\x82", 2);
	message_test_failure ("a text that ends inside a code point", buffer, size, NULL, 1007);

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03", 1);
	message_test_failure ("a close of one byte", buffer, size, NULL, 1002);

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xED", 2);
	message_test_failure ("a close with 1005", buffer, size, NULL, 1002);

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xE7", 2);
	message_test_failure ("a close with 999", buffer, size, NULL, 1002);

	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, "\x03\xE8\xFF", 3);
	message_test_failure ("a close with a reason that is not UTF-8", buffer, size, NULL, 1007);

	u8 large[MESSAGE_TEST_MAX_MESSAGE + 1];
	(void) memset (large, 'a', sizeof (large));
	size = message_test_frame (buffer, 0, false, false, HTTP_WEB_SOCKET_OPCODE_BINARY, large, MESSAGE_TEST_MAX_MESSAGE - 1);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, large, 2);
	message_test_failure ("a message larger than the limit", buffer, size, NULL, 1009);

	size = http_web_socket_frame_header (buffer, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, MESSAGE_TEST_MAX_MESSAGE + 1, message_test_mask);
	message_test_failure ("a frame larger than the limit", buffer, size, NULL, 1009);

	size = message_test_frame (buffer, 0, true, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "a", 1);
	message_test_failure ("a compressed frame without the extension", buffer, size, NULL, 1002);

}

#ifdef CERVER_ZLIB

// the output of the decompressor that the test gives to a connection
#define MESSAGE_TEST_INFLATER_CHUNK_SIZE		(1 << 12)

// "Hello" as the payload of a compressed message (RFC 7692 section 7.2.3.1)
static const u8 message_test_compressed_hello[] = { 0xF2, 0x48, 0xCD, 0xC9, 0xC9, 0x07, 0x00 };

// zlib has no more memory: the inflater got its state, it does not get its window
static bool message_test_inflater_exhausted = false;

static voidpf message_test_inflater_alloc (voidpf opaque, uInt items, uInt size) {

	(void) opaque;

	return message_test_inflater_exhausted ? Z_NULL : calloc (items, size);

}

static void message_test_inflater_free (voidpf opaque, voidpf address) {

	(void) opaque;

	free (address);

}

// a decompressor that fails for its memory is a failure of cerver, the connection ends with 1011:
// only the bytes of the peer that are not a deflate stream are a 1007 (it was the code of both)
static void message_test_inflater_without_memory (
	const HttpWebSocketDeflateParameters *parameters, const HttpWebSocketDeflateOptions *options
) {

	HttpWebSocketDeflate *deflate = http_web_socket_deflate_create (HTTP_WEB_SOCKET_ROLE_SERVER, parameters, options);
	test_check_ptr (deflate);

	z_stream *stream = (z_stream *) calloc (1, sizeof (z_stream));
	test_check_ptr (stream);
	stream->zalloc = message_test_inflater_alloc;
	stream->zfree = message_test_inflater_free;

	message_test_inflater_exhausted = false;
	test_check_int_eq (inflateInit2 (stream, -HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS), Z_OK, NULL);
	message_test_inflater_exhausted = true;

	// the connection finds its decompressor already created
	deflate->decompressor = stream;
	deflate->chunk = (u8 *) malloc (MESSAGE_TEST_INFLATER_CHUNK_SIZE);
	test_check_ptr (deflate->chunk);
	deflate->chunk_size = MESSAGE_TEST_INFLATER_CHUNK_SIZE;

	u8 buffer[1 << 8];
	const size_t size = message_test_frame (
		buffer, 0, true, true, HTTP_WEB_SOCKET_OPCODE_TEXT,
		message_test_compressed_hello, sizeof (message_test_compressed_hello)
	);

	message_test_failure ("a decompressor without memory", buffer, size, deflate, 1011);

	http_web_socket_deflate_delete (deflate);

	message_test_inflater_exhausted = false;

}

#endif

// the receiver of a connection with permessage-deflate rejects the reserved bit in the wrong frames
static void test_message_compressed_failures (void) {

	if (!http_web_socket_deflate_available ()) return;

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = true;

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	parameters.client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	HttpWebSocketDeflate *deflate = http_web_socket_deflate_create (HTTP_WEB_SOCKET_ROLE_SERVER, &parameters, &options);
	test_check_ptr (deflate);

	u8 buffer[1 << 8];
	size_t size = message_test_frame (buffer, 0, true, true, HTTP_WEB_SOCKET_OPCODE_PING, "a", 1);
	message_test_failure ("a compressed ping", buffer, size, deflate, 1002);

	size = message_test_frame (buffer, 0, false, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "a", 1);
	size = message_test_frame (buffer, size, true, true, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, "b", 1);
	message_test_failure ("a compressed continuation", buffer, size, deflate, 1002);

	size = message_test_frame (buffer, 0, true, true, HTTP_WEB_SOCKET_OPCODE_TEXT, "\xFF\xFF\xFF\xFF", 4);
	message_test_failure ("a stream that is not deflate", buffer, size, deflate, 1007);

	http_web_socket_deflate_delete (deflate);

	#ifdef CERVER_ZLIB
	message_test_inflater_without_memory (&parameters, &options);
	#endif

}

// a close without a code reports 1005, and the bytes after a close are ignored
static void test_message_close (void) {

	u8 buffer[1 << 8];
	size_t size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_CLOSE, NULL, 0);
	const size_t close_size = size;
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "ignored", 7);

	MessageTestEvents events;
	u16 code = 0;
	size_t parsed = 0;
	test_check_int_eq (
		message_test_receive (buffer, size, 0, false, NULL, &events, &code, &parsed),
		HTTP_WEB_SOCKET_RECEIVE_CLOSED, NULL
	);

	test_check_unsigned_eq (code, 1005, NULL);
	test_check_unsigned_eq (parsed, close_size, NULL);
	test_check_unsigned_eq (events.n_events, 1, NULL);
	test_check_unsigned_eq (events.events[0].code, 1005, NULL);

}

// a method stops the receiver, that continues with the bytes it did not parse
static void test_message_stop (void) {

	u8 buffer[1 << 8];
	size_t size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "one", 3);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, "two", 3);

	MessageTestEvents events;
	(void) memset (&events, 0, sizeof (MessageTestEvents));
	events.stop_once = true;

	HttpWebSocketReceiver receiver;
	http_web_socket_receiver_init (
		&receiver, HTTP_WEB_SOCKET_ROLE_SERVER, 0, 0, NULL, &message_test_handlers, &events
	);

	size_t parsed = 0;
	test_check_int_eq (http_web_socket_receiver_execute (&receiver, buffer, size, &parsed), HTTP_WEB_SOCKET_RECEIVE_STOPPED, NULL);
	test_check_unsigned_eq (events.n_events, 1, NULL);

	size_t rest = 0;
	test_check_int_eq (
		http_web_socket_receiver_execute (&receiver, buffer + parsed, size - parsed, &rest),
		HTTP_WEB_SOCKET_RECEIVE_CONTINUE, NULL
	);

	test_check_unsigned_eq (parsed + rest, size, NULL);
	test_check_unsigned_eq (events.n_events, 2, NULL);
	test_check (!memcmp (events.events[1].data, "two", 3), "the second message is wrong");

	http_web_socket_receiver_end (&receiver);

}

// the buffer of the messages keeps its size, the next messages do not allocate
static void test_message_buffer (void) {

	u8 *buffer = (u8 *) malloc (MESSAGE_TEST_BUFFER_SIZE);
	u8 payload[MESSAGE_TEST_MAX_MESSAGE];
	test_check_ptr (buffer);
	(void) memset (payload, 'x', sizeof (payload));

	MessageTestEvents events;
	(void) memset (&events, 0, sizeof (MessageTestEvents));

	HttpWebSocketReceiver receiver;
	http_web_socket_receiver_init (
		&receiver, HTTP_WEB_SOCKET_ROLE_SERVER, 0, MESSAGE_TEST_MAX_MESSAGE, NULL, &message_test_handlers, &events
	);

	size_t size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, payload, sizeof (payload));
	test_check_int_eq (http_web_socket_receiver_execute (&receiver, buffer, size, NULL), HTTP_WEB_SOCKET_RECEIVE_CONTINUE, NULL);
	test_check_unsigned_eq (receiver.message_capacity, MESSAGE_TEST_MAX_MESSAGE, NULL);

	const u8 *message = receiver.message;
	events.n_events = 0;
	size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_BINARY, payload, 10);
	test_check_int_eq (http_web_socket_receiver_execute (&receiver, buffer, size, NULL), HTTP_WEB_SOCKET_RECEIVE_CONTINUE, NULL);
	test_check_ptr_eq (receiver.message, message);
	test_check_unsigned_eq (events.events[0].size, 10, NULL);

	http_web_socket_receiver_end (&receiver);
	free (buffer);

}

// an empty text and an empty binary are messages too
static void test_message_empty (void) {

	u8 buffer[1 << 6];
	size_t size = message_test_frame (buffer, 0, true, false, HTTP_WEB_SOCKET_OPCODE_TEXT, NULL, 0);
	size = message_test_frame (buffer, size, false, false, HTTP_WEB_SOCKET_OPCODE_BINARY, NULL, 0);
	size = message_test_frame (buffer, size, true, false, HTTP_WEB_SOCKET_OPCODE_CONTINUATION, NULL, 0);

	for (int streaming = 0; streaming <= 1; streaming++) {
		MessageTestEvents events;
		u16 code = 0;
		test_check_int_eq (
			message_test_receive (buffer, size, 0, streaming, NULL, &events, &code, NULL),
			HTTP_WEB_SOCKET_RECEIVE_CONTINUE, NULL
		);

		test_check_unsigned_eq (events.n_events, 2, NULL);
		test_check_unsigned_eq (events.events[0].size, 0, NULL);
		test_check_int_eq (events.events[1].opcode, HTTP_WEB_SOCKET_OPCODE_BINARY, NULL);
	}

}

void web_socket_tests_message (void) {

	(void) printf ("Testing WebSocket messages...\n");

	test_run (test_message_fragments);
	test_run (test_message_failures);
	test_run (test_message_compressed_failures);
	test_run (test_message_close);
	test_run (test_message_stop);
	test_run (test_message_buffer);
	test_run (test_message_empty);

	(void) printf ("Done!\n");

}
