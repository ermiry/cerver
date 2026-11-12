#ifndef _CERVER_HTTP_WEBSOCKETS_MESSAGE_H_
#define _CERVER_HTTP_WEBSOCKETS_MESSAGE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/text.h"

// the first size of the buffer of a message, that doubles until the most size of a message
#define HTTP_WEB_SOCKET_MESSAGE_BUFFER_SIZE			(1 << 12)

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_WEB_SOCKET_RECEIVE_MAP(XX)																	\
	XX(0,	CONTINUE,	Continue,	Every byte was parsed and the connection goes on)					\
	XX(1,	CLOSED,		Closed,		A close frame was received and the bytes after it were ignored)	\
	XX(2,	FAILED,		Failed,		The peer broke the protocol and the connection must be closed)		\
	XX(3,	STOPPED,	Stopped,	A method asked to stop)

typedef enum HttpWebSocketReceiveResult {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_RECEIVE_##name = num,
	HTTP_WEB_SOCKET_RECEIVE_MAP (XX)
	#undef XX

} HttpWebSocketReceiveResult;

CERVER_PUBLIC const char *http_web_socket_receive_result_to_string (
	const HttpWebSocketReceiveResult result
);

// the methods return 0 to continue, any other value stops the receiver
typedef struct HttpWebSocketReceiverHandlers {

	// a whole message, its data is valid only during the call
	int (*on_message)(void *data, HttpWebSocketOpcode type, const u8 *payload, size_t size);

	// a piece of a message, instead of on_message when it is set,
	// the last piece is always a call with last as true and it can be empty
	int (*on_fragment)(
		void *data, HttpWebSocketOpcode type,
		const u8 *payload, size_t size,
		bool first, bool last
	);

	int (*on_ping)(void *data, const u8 *payload, size_t size);

	int (*on_pong)(void *data, const u8 *payload, size_t size);

	// the close frame of the peer, the code is 1005 when it had none
	int (*on_close)(void *data, u16 code, const char *reason, size_t length);

} HttpWebSocketReceiverHandlers;

// takes the frames of the parser and checks the rules of the messages:
// the fragments, the UTF-8 of the texts, the close frames, the size and the compression
typedef struct HttpWebSocketReceiver {

	HttpWebSocketParser parser;

	const HttpWebSocketReceiverHandlers *handlers;
	void *data;

	// the most bytes of a message, after it was decompressed
	size_t max_message;

	// the compression of the connection, NULL when it was not negotiated
	HttpWebSocketDeflate *deflate;

	// the message in progress
	bool in_message;
	u8 type;
	bool compressed;
	bool first_piece;
	size_t message_received;
	HttpWebSocketText text;

	u8 *message;
	size_t message_size;
	size_t message_capacity;

	// the payload of the control frame in progress
	u8 control[HTTP_WEB_SOCKET_CONTROL_MAX_SIZE];
	size_t control_size;

	// the code of the close frame that answers a failure
	u16 close_code;

	HttpWebSocketReceiveResult result;

} HttpWebSocketReceiver;

// prepares the receiver for the frames of the other side of the role
// the deflate is not owned by the receiver
CERVER_PUBLIC void http_web_socket_receiver_init (
	HttpWebSocketReceiver *receiver,
	const HttpWebSocketRole role,
	const u64 max_frame, const size_t max_message,
	HttpWebSocketDeflate *deflate,
	const HttpWebSocketReceiverHandlers *handlers, void *data
);

// releases the buffer of the messages
CERVER_PUBLIC void http_web_socket_receiver_end (HttpWebSocketReceiver *receiver);

// parses the bytes, that are unmasked in place,
// parsed (optional) is set to the bytes that were taken
// it continues after a STOPPED result with the bytes that were not parsed,
// CLOSED and FAILED are final, and close_code has the code of the answer of a failure
CERVER_PUBLIC HttpWebSocketReceiveResult http_web_socket_receiver_execute (
	HttpWebSocketReceiver *receiver,
	u8 *data, const size_t size, size_t *parsed
);

#ifdef __cplusplus
}
#endif

#endif
