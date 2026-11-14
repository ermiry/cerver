#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/deflate.h>
#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/message.h>

// raw bytes to the receiver of WebSocket messages (the frames, the messages and permessage-deflate):
// the first byte selects the size of the pieces (its low bits), the role,
// the compression and the delivery of the messages in pieces
// the second one is the compression that was negotiated: the peer starts every message from zero
// (its first bit) and the window bits of both sides, from 8 to 15 (its next three bits)

#define FUZZ_FRAME_HEADER_SIZE			2

#define FUZZ_FRAME_PIECE_BITS			0x1F
#define FUZZ_FRAME_CLIENT_BIT			0x20
#define FUZZ_FRAME_DEFLATE_BIT			0x40
#define FUZZ_FRAME_STREAM_BIT			0x80

#define FUZZ_FRAME_RESET_BIT			0x01
#define FUZZ_FRAME_WINDOW_SHIFT			1
#define FUZZ_FRAME_WINDOW_BITS			0x07

// small limits, so the fuzzer reaches them
#define FUZZ_FRAME_MAX_MESSAGE			(1 << 14)

// every byte that the receiver gives is read, so the sanitizers catch a read past a payload
static int fuzz_frame_touch (const u8 *payload, const size_t size) {

	volatile u8 sum = 0;
	for (size_t i = 0; i < size; i++) sum = (u8) (sum + payload[i]);

	return 0;

}

static int fuzz_frame_on_message (void *data, HttpWebSocketOpcode type, const u8 *payload, size_t size) {

	(void) data;
	(void) type;

	return fuzz_frame_touch (payload, size);

}

static int fuzz_frame_on_fragment (
	void *data, HttpWebSocketOpcode type,
	const u8 *payload, size_t size, bool first, bool last
) {

	(void) data;
	(void) type;
	(void) first;
	(void) last;

	return fuzz_frame_touch (payload, size);

}

static int fuzz_frame_on_control (void *data, const u8 *payload, size_t size) {

	(void) data;

	return fuzz_frame_touch (payload, size);

}

static int fuzz_frame_on_close (void *data, u16 code, const char *reason, size_t length) {

	(void) data;
	(void) code;

	return fuzz_frame_touch ((const u8 *) reason, length);

}

static const HttpWebSocketReceiverHandlers fuzz_frame_handlers = {
	fuzz_frame_on_message, NULL, fuzz_frame_on_control, fuzz_frame_on_control, fuzz_frame_on_close
};

static const HttpWebSocketReceiverHandlers fuzz_frame_stream_handlers = {
	fuzz_frame_on_message, fuzz_frame_on_fragment, fuzz_frame_on_control, fuzz_frame_on_control, fuzz_frame_on_close
};

// the compression of the connection with the parameters of the second byte of the input,
// only the one of 15 bits that keeps its context was fuzzed
static HttpWebSocketDeflate *fuzz_frame_deflate (const HttpWebSocketRole role, const u8 negotiated) {

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = true;

	const u8 window_bits = (u8) (
		HTTP_WEB_SOCKET_DEFLATE_MIN_WINDOW_BITS + ((negotiated >> FUZZ_FRAME_WINDOW_SHIFT) & FUZZ_FRAME_WINDOW_BITS)
	);

	const bool reset = (negotiated & FUZZ_FRAME_RESET_BIT) != 0;

	HttpWebSocketDeflateParameters parameters;
	(void) memset (&parameters, 0, sizeof (HttpWebSocketDeflateParameters));
	parameters.server_max_window_bits = window_bits;
	parameters.client_max_window_bits = window_bits;
	parameters.server_no_context_takeover = reset;
	parameters.client_no_context_takeover = reset;

	return http_web_socket_deflate_create (role, &parameters, &options);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < FUZZ_FRAME_HEADER_SIZE) return 0;

	const u8 selector = data[0];
	const size_t piece = (size_t) (selector & FUZZ_FRAME_PIECE_BITS) + 1;
	const HttpWebSocketRole role = (selector & FUZZ_FRAME_CLIENT_BIT) ?
		HTTP_WEB_SOCKET_ROLE_CLIENT : HTTP_WEB_SOCKET_ROLE_SERVER;

	HttpWebSocketDeflate *deflate = (selector & FUZZ_FRAME_DEFLATE_BIT) ? fuzz_frame_deflate (role, data[1]) : NULL;

	HttpWebSocketReceiver receiver;
	http_web_socket_receiver_init (
		&receiver, role, FUZZ_FRAME_MAX_MESSAGE, FUZZ_FRAME_MAX_MESSAGE, deflate,
		(selector & FUZZ_FRAME_STREAM_BIT) ? &fuzz_frame_stream_handlers : &fuzz_frame_handlers, NULL
	);

	// each piece is copied to a buffer of its exact size, the receiver unmasks it in place
	size_t position = FUZZ_FRAME_HEADER_SIZE;
	HttpWebSocketReceiveResult result = HTTP_WEB_SOCKET_RECEIVE_CONTINUE;
	while ((position < size) && (result == HTTP_WEB_SOCKET_RECEIVE_CONTINUE)) {
		const size_t length = ((size - position) < piece) ? (size - position) : piece;

		u8 *buffer = (u8 *) malloc (length);
		if (!buffer) break;
		(void) memcpy (buffer, data + position, length);

		size_t parsed = 0;
		result = http_web_socket_receiver_execute (&receiver, buffer, length, &parsed);
		free (buffer);

		position += length;
	}

	http_web_socket_receiver_end (&receiver);
	http_web_socket_deflate_delete (deflate);

	return 0;

}
