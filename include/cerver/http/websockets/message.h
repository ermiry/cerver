/**
 * @file cerver/http/websockets/message.h
 * @brief The receiver of the messages of a WebSocket connection, on top of the parser of the frames.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_MESSAGE_H_
#define _CERVER_HTTP_WEBSOCKETS_MESSAGE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/text.h"

/**
 * @defgroup http_websockets_message Messages
 * @ingroup http_websockets
 * @brief The rules of the messages: the fragments, the UTF-8 of the texts, the close frames, the size and the
 * compression.
 * @{
 */

/** @brief The first size of the buffer of a message, that doubles until the most size of a message. */
#define HTTP_WEB_SOCKET_MESSAGE_BUFFER_SIZE			(1 << 12)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The X-macro table of the results of a receive: the number, the name, the string and the description of
 * each one.
 *
 * @since 2.1b-21 (it was HTTP_WEB_SOCKET_RECEIVE_MAP)
 */
#define HTTP_WEB_SOCKET_RECEIVE_RESULT_MAP(XX)															\
	XX(0,	CONTINUE,	Continue,	Every byte was parsed and the connection goes on)					\
	XX(1,	CLOSED,		Closed,		A close frame was received and the bytes after it were ignored)	\
	XX(2,	FAILED,		Failed,		The peer broke the protocol and the connection must be closed)		\
	XX(3,	STOPPED,	Stopped,	A method asked to stop)

/**
 * @brief The result of a receive, one `HTTP_WEB_SOCKET_RECEIVE_RESULT_<name>` for each row of
 * #HTTP_WEB_SOCKET_RECEIVE_RESULT_MAP.
 *
 * @since 2.1b-21 (the enumerators were `HTTP_WEB_SOCKET_RECEIVE_<name>`)
 */
typedef enum HttpWebSocketReceiveResult {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_RECEIVE_RESULT_##name = num,
	HTTP_WEB_SOCKET_RECEIVE_RESULT_MAP (XX)
	#undef XX

} HttpWebSocketReceiveResult;

/**
 * @brief Returns the string of a result.
 *
 * @param[in] result the result
 * @return the string of its row in #HTTP_WEB_SOCKET_RECEIVE_RESULT_MAP, or "Undefined" for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_receive_result_to_string (
	const HttpWebSocketReceiveResult result
);

/**
 * @brief The methods of a receiver; they return 0 to continue, any other value stops it.
 */
typedef struct HttpWebSocketReceiverHandlers {

	/** @brief A whole message; its data is valid only during the call. */
	int (*on_message)(void *data, HttpWebSocketOpcode type, const u8 *payload, size_t size);

	/**
	 * @brief A piece of a message, instead of on_message when it is set; the last piece is always a call with last
	 * as true and it can be empty.
	 */
	int (*on_fragment)(
		void *data, HttpWebSocketOpcode type,
		const u8 *payload, size_t size,
		bool first, bool last
	);

	/** @brief A ping. */
	int (*on_ping)(void *data, const u8 *payload, size_t size);

	/** @brief A pong. */
	int (*on_pong)(void *data, const u8 *payload, size_t size);

	/** @brief The close frame of the peer; the code is 1005 when it had none. */
	int (*on_close)(void *data, u16 code, const char *reason, size_t length);

} HttpWebSocketReceiverHandlers;

/**
 * @brief Takes the frames of the parser and checks the rules of the messages: the fragments, the UTF-8 of the
 * texts, the close frames, the size and the compression.
 */
typedef struct HttpWebSocketReceiver {

	/** @brief The parser of the frames. */
	HttpWebSocketParser parser;

	/** @brief The methods of the receiver. */
	const HttpWebSocketReceiverHandlers *handlers;
	/** @brief The data of the methods. */
	void *data;

	/** @brief The most bytes of a message, after it was decompressed. */
	size_t max_message;

	/** @brief The compression of the connection, NULL when it was not negotiated; the receiver does not own it. */
	HttpWebSocketDeflate *deflate;

	/** @brief A message is in progress. */
	bool in_message;
	/** @brief The opcode of the message in progress. */
	u8 type;
	/** @brief The message in progress is compressed. */
	bool compressed;
	/** @brief The next piece is the first one of the message. */
	bool first_piece;
	/** @brief The bytes of the message in progress that were received. */
	size_t message_received;
	/**
	 * @brief The end of the message in progress was stopped in its decompression, the next call goes on with it
	 * before it parses (W4).
	 *
	 * @since 2.1b-21
	 */
	bool ending;
	/** @brief The UTF-8 validation of a text message in progress. */
	HttpWebSocketText text;

	/** @brief The buffer of the message in progress. */
	u8 *message;
	/** @brief The bytes in the buffer. */
	size_t message_size;
	/** @brief The size of the buffer. */
	size_t message_capacity;

	/** @brief The payload of the control frame in progress. */
	u8 control[HTTP_WEB_SOCKET_CONTROL_MAX_SIZE];
	/** @brief The bytes of the control payload. */
	size_t control_size;

	/** @brief The code of the close frame that answers a failure. */
	u16 close_code;

	/** @brief The result of the last call. */
	HttpWebSocketReceiveResult result;

} HttpWebSocketReceiver;

/**
 * @brief Prepares a receiver for the frames of the other side of a role.
 *
 * @param[out] receiver the receiver, it can be NULL
 * @param[in] role the role of this side
 * @param[in] max_frame the most bytes of the payload of a frame
 * @param[in] max_message the most bytes of a message, after it was decompressed
 * @param[in] deflate the compression of the connection, NULL without one; the receiver does not own it, and RSV1 is
 * only allowed with it
 * @param[in] handlers the methods, they must live as long as the receiver
 * @param[in] data the data of the methods
 */
CERVER_PUBLIC void http_web_socket_receiver_init (
	HttpWebSocketReceiver *receiver,
	const HttpWebSocketRole role,
	const u64 max_frame, const size_t max_message,
	HttpWebSocketDeflate *deflate,
	const HttpWebSocketReceiverHandlers *handlers, void *data
);

/**
 * @brief Releases the buffer of the messages of a receiver.
 *
 * @param[in,out] receiver the receiver, it can be NULL
 */
CERVER_PUBLIC void http_web_socket_receiver_end (HttpWebSocketReceiver *receiver);

/**
 * @brief Parses some bytes, that are unmasked in place, and calls the methods with the messages.
 *
 * It continues after a STOPPED result with the bytes that were not parsed; CLOSED and FAILED are final, and
 * close_code has the code of the answer of a failure.
 *
 * @param[in,out] receiver the receiver
 * @param[in,out] data the bytes
 * @param[in] size the number of bytes
 * @param[out] parsed the bytes that were taken, it can be NULL
 * @return the result; HTTP_WEB_SOCKET_RECEIVE_RESULT_FAILED for a NULL receiver or one without methods
 */
CERVER_PUBLIC HttpWebSocketReceiveResult http_web_socket_receiver_execute (
	HttpWebSocketReceiver *receiver,
	u8 *data, const size_t size, size_t *parsed
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
