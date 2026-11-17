/**
 * @file cerver/http/websockets/frame.h
 * @brief The frames of RFC 6455: the opcodes, the close codes, the headers, the masks and the incremental parser.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_FRAME_H_
#define _CERVER_HTTP_WEBSOCKETS_FRAME_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup http_websockets_frame Frames
 * @ingroup http_websockets
 * @brief The frames of the WebSocket protocol and how they are parsed.
 * @{
 */

/** @brief The least bytes of the header of a frame (RFC 6455 section 5.2). */
#define HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE		2

/** @brief The most bytes of the header of a frame. */
#define HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE		14

/** @brief The bytes of the mask of a frame. */
#define HTTP_WEB_SOCKET_MASK_SIZE					4

/** @brief The most bytes of the payload of a control frame. */
#define HTTP_WEB_SOCKET_CONTROL_MAX_SIZE			125

/**
 * @brief The bytes of the code of a close frame.
 *
 * @since 2.1b-21 (it was HTTP_WEB_SOCKET_CLOSE_CODE_SIZE, the prefix of the codes)
 */
#define HTTP_WEB_SOCKET_CLOSE_FRAME_CODE_SIZE		2

/** @brief The most bytes of the reason of a close frame. */
#define HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE		(HTTP_WEB_SOCKET_CONTROL_MAX_SIZE - HTTP_WEB_SOCKET_CLOSE_FRAME_CODE_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The X-macro table of the opcodes of RFC 6455: the number, the name and the string of each one.
 */
#define HTTP_WEB_SOCKET_OPCODE_MAP(XX)						\
	XX(0x0,	CONTINUATION,	Continuation)					\
	XX(0x1,	TEXT,			Text)							\
	XX(0x2,	BINARY,			Binary)							\
	XX(0x8,	CLOSE,			Close)							\
	XX(0x9,	PING,			Ping)							\
	XX(0xA,	PONG,			Pong)

/**
 * @brief An opcode, one `HTTP_WEB_SOCKET_OPCODE_<name>` for each row of #HTTP_WEB_SOCKET_OPCODE_MAP.
 */
typedef enum HttpWebSocketOpcode {

	#define XX(num, name, string) HTTP_WEB_SOCKET_OPCODE_##name = num,
	HTTP_WEB_SOCKET_OPCODE_MAP (XX)
	#undef XX

} HttpWebSocketOpcode;

/**
 * @brief Returns the string of an opcode.
 *
 * @param[in] opcode the opcode
 * @return the string of its row in #HTTP_WEB_SOCKET_OPCODE_MAP, or "Undefined" for a reserved opcode; a static
 * string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_opcode_to_string (
	const HttpWebSocketOpcode opcode
);

/**
 * @brief Returns whether an opcode is one of RFC 6455; the others are reserved.
 *
 * @param[in] opcode the opcode
 * @return true for a known opcode
 */
CERVER_PUBLIC bool http_web_socket_opcode_is_known (const u8 opcode);

/**
 * @brief Returns whether an opcode is of a control frame: close, ping and pong.
 *
 * @param[in] opcode the opcode
 * @return true for a control opcode
 */
CERVER_PUBLIC bool http_web_socket_opcode_is_control (const u8 opcode);

/**
 * @brief The X-macro table of the status codes of a close frame (RFC 6455 section 7.4.1 and the IANA registry):
 * the number, the name, the string and the description of each one.
 *
 * @since 2.1b-21 (it was HTTP_WEB_SOCKET_CLOSE_MAP)
 */
#define HTTP_WEB_SOCKET_CLOSE_CODE_MAP(XX)																		\
	XX(1000,	NORMAL,					Normal,					The purpose of the connection was fulfilled)	\
	XX(1001,	GOING_AWAY,				Going Away,				The endpoint is going away)					\
	XX(1002,	PROTOCOL_ERROR,			Protocol Error,			The endpoint received a protocol error)		\
	XX(1003,	UNSUPPORTED_DATA,		Unsupported Data,		The endpoint can not accept the type of data)	\
	XX(1005,	NO_STATUS,				No Status,				The close frame had no status code)			\
	XX(1006,	ABNORMAL,				Abnormal,				The connection was closed without a close frame)	\
	XX(1007,	INVALID_PAYLOAD,		Invalid Payload,		The data was not consistent with its type)		\
	XX(1008,	POLICY_VIOLATION,		Policy Violation,		The message violates a policy)					\
	XX(1009,	MESSAGE_TOO_BIG,		Message Too Big,		The message is too big to process)				\
	XX(1010,	MANDATORY_EXTENSION,	Mandatory Extension,	The server did not negotiate an extension)		\
	XX(1011,	INTERNAL_ERROR,			Internal Error,			The server found an unexpected condition)		\
	XX(1012,	SERVICE_RESTART,		Service Restart,		The service is restarted)						\
	XX(1013,	TRY_AGAIN_LATER,		Try Again Later,		The service is overloaded)						\
	XX(1014,	BAD_GATEWAY,			Bad Gateway,			The gateway received an invalid response)		\
	XX(1015,	TLS_HANDSHAKE,			TLS Handshake,			The TLS handshake failed)

/**
 * @brief A status code of a close frame, one `HTTP_WEB_SOCKET_CLOSE_CODE_<name>` for each row of
 * #HTTP_WEB_SOCKET_CLOSE_CODE_MAP.
 *
 * @since 2.1b-21 (the enumerators were `HTTP_WEB_SOCKET_CLOSE_<name>`)
 */
typedef enum HttpWebSocketCloseCode {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_CLOSE_CODE_##name = num,
	HTTP_WEB_SOCKET_CLOSE_CODE_MAP (XX)
	#undef XX

} HttpWebSocketCloseCode;

/**
 * @brief Returns the string of a close code.
 *
 * @param[in] code the code
 * @return the string of its row in #HTTP_WEB_SOCKET_CLOSE_CODE_MAP, or "Undefined" for another code; a static string
 * that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_close_code_to_string (const u16 code);

/**
 * @brief Returns the description of a close code.
 *
 * @param[in] code the code
 * @return the description of its row in #HTTP_WEB_SOCKET_CLOSE_CODE_MAP, or "Undefined" for another code; a static
 * string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_close_code_description (const u16 code);

/**
 * @brief Returns whether a code can be sent in a close frame: 1000 to 1003, 1007 to 1014, and 3000 to 4999
 * (registered and private).
 *
 * 1005, 1006 and 1015 only report a close, they are never sent.
 *
 * @param[in] code the code
 * @return true for a code that can be sent
 */
CERVER_PUBLIC bool http_web_socket_close_code_is_valid (const u16 code);

/**
 * @brief The X-macro table of the errors of a frame: the number, the name, the string and the description of each
 * one.
 */
#define HTTP_WEB_SOCKET_FRAME_ERROR_MAP(XX)																	\
	XX(0,	NONE,				None,				No error)												\
	XX(1,	RESERVED_BITS,		Reserved Bits,		A reserved bit was set without an extension)			\
	XX(2,	OPCODE,				Opcode,				The opcode is reserved)									\
	XX(3,	CONTROL_FRAGMENTED,	Control Fragmented,	A control frame was fragmented)						\
	XX(4,	CONTROL_TOO_BIG,	Control Too Big,	A control frame had more than 125 bytes)				\
	XX(5,	MASK_REQUIRED,		Mask Required,		A frame of the client was not masked)					\
	XX(6,	MASK_FORBIDDEN,		Mask Forbidden,		A frame of the server was masked)						\
	XX(7,	LENGTH,				Length,				The most significant bit of the length was set)		\
	XX(8,	TOO_BIG,			Too Big,			The frame is larger than the limit)

/**
 * @brief The error of a frame, one `HTTP_WEB_SOCKET_FRAME_ERROR_<name>` for each row of
 * #HTTP_WEB_SOCKET_FRAME_ERROR_MAP.
 */
typedef enum HttpWebSocketFrameError {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_FRAME_ERROR_##name = num,
	HTTP_WEB_SOCKET_FRAME_ERROR_MAP (XX)
	#undef XX

} HttpWebSocketFrameError;

/**
 * @brief Returns the string of a frame error.
 *
 * @param[in] error the error
 * @return the string of its row in #HTTP_WEB_SOCKET_FRAME_ERROR_MAP, or "Undefined" for a value out of the table; a
 * static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_frame_error_to_string (
	const HttpWebSocketFrameError error
);

/**
 * @brief Returns the description of a frame error.
 *
 * @param[in] error the error
 * @return the description of its row in #HTTP_WEB_SOCKET_FRAME_ERROR_MAP, or "Undefined" for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *http_web_socket_frame_error_description (
	const HttpWebSocketFrameError error
);

/**
 * @brief Returns the code of the close frame that answers a frame error.
 *
 * @param[in] error the error
 * @return 1009 for a frame that is too big, 1002 for the rest
 */
CERVER_PUBLIC u16 http_web_socket_frame_error_close_code (
	const HttpWebSocketFrameError error
);

/**
 * @brief The X-macro table of the sides of a connection: the number, the name and the string of each one; the
 * frames of a server are not masked, the ones of a client are.
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_ROLE_MAP(XX)							\
	XX(0,	SERVER,		Server)									\
	XX(1,	CLIENT,		Client)

/**
 * @brief The side of a connection, one `HTTP_WEB_SOCKET_ROLE_<name>` for each row of #HTTP_WEB_SOCKET_ROLE_MAP: a
 * client masks its frames and a server never does.
 */
typedef enum HttpWebSocketRole {

	#define XX(num, name, string) HTTP_WEB_SOCKET_ROLE_##name = num,
	HTTP_WEB_SOCKET_ROLE_MAP (XX)
	#undef XX

} HttpWebSocketRole;

/**
 * @brief Returns the string of a role.
 *
 * @param[in] role the role
 * @return the string of its row in #HTTP_WEB_SOCKET_ROLE_MAP, or "Undefined" for a value out of the table; a static
 * string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_role_to_string (const HttpWebSocketRole role);

/**
 * @brief The header of a frame.
 */
typedef struct HttpWebSocketFrame {

	/** @brief It is the last frame of its message. */
	bool fin;

	/** @brief The RSV1 bit, the compressed messages of permessage-deflate. */
	bool rsv1;
	/** @brief The RSV2 bit, reserved. */
	bool rsv2;
	/** @brief The RSV3 bit, reserved. */
	bool rsv3;

	/** @brief The opcode. */
	u8 opcode;

	/** @brief The payload is masked. */
	bool masked;
	/** @brief The mask of the payload. */
	u8 mask[HTTP_WEB_SOCKET_MASK_SIZE];

	/** @brief The bytes of the payload. */
	u64 length;

} HttpWebSocketFrame;

/**
 * @brief Writes the header of a frame, always with the least bytes for the length.
 *
 * @param[out] header where the header is written, #HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE bytes
 * @param[in] fin true for the last frame of a message
 * @param[in] rsv1 true for a compressed message
 * @param[in] opcode the opcode
 * @param[in] length the bytes of the payload
 * @param[in] mask the mask, NULL for a frame without one (of a server)
 * @return the size of the header, 0 for a NULL header
 */
CERVER_PUBLIC size_t http_web_socket_frame_header (
	u8 *header,
	const bool fin, const bool rsv1,
	const u8 opcode, const u64 length,
	const u8 *mask
);

/**
 * @brief Applies a mask to some bytes of a payload; the same call masks and unmasks.
 *
 * @param[in,out] data the bytes
 * @param[in] size the number of bytes
 * @param[in] mask the mask, #HTTP_WEB_SOCKET_MASK_SIZE bytes
 * @param[in] offset where the bytes are from the start of the payload
 */
CERVER_PUBLIC void http_web_socket_mask (
	u8 *data, const size_t size,
	const u8 *mask, const u64 offset
);

/**
 * @brief The X-macro table of the states of the parser: the number, the name and the string of each one. It waits
 * for the bytes of a header, it takes the bytes of a payload, or a frame was invalid (the error of the parser says
 * why, and it is final).
 *
 * @since 2.1b-21
 */
#define HTTP_WEB_SOCKET_PARSER_STATE_MAP(XX)					\
	XX(0,	HEADER,		Header)									\
	XX(1,	PAYLOAD,	Payload)								\
	XX(2,	FAILED,		Failed)

/**
 * @brief The state of the parser, one `HTTP_WEB_SOCKET_PARSER_STATE_<name>` for each row of
 * #HTTP_WEB_SOCKET_PARSER_STATE_MAP.
 */
typedef enum HttpWebSocketParserState {

	#define XX(num, name, string) HTTP_WEB_SOCKET_PARSER_STATE_##name = num,
	HTTP_WEB_SOCKET_PARSER_STATE_MAP (XX)
	#undef XX

} HttpWebSocketParserState;

/**
 * @brief Returns the string of a state of the parser.
 *
 * @param[in] state the state
 * @return the string of its row in #HTTP_WEB_SOCKET_PARSER_STATE_MAP, or "Undefined" for a value out of the table; a
 * static string that must not be freed
 * @since 2.1b-21
 */
CERVER_PUBLIC const char *http_web_socket_parser_state_to_string (const HttpWebSocketParserState state);

struct _HttpWebSocketParser;

/**
 * @brief The methods of the parser; they return 0 to continue, any other value stops it.
 */
typedef struct HttpWebSocketParserSettings {

	/** @brief A header was parsed, before its payload. */
	int (*on_frame_begin)(struct _HttpWebSocketParser *parser, const HttpWebSocketFrame *frame);

	/** @brief A piece of the payload, already unmasked; a payload can arrive in many pieces. */
	int (*on_frame_data)(struct _HttpWebSocketParser *parser, u8 *data, size_t size);

	/** @brief The whole payload was received. */
	int (*on_frame_end)(struct _HttpWebSocketParser *parser, const HttpWebSocketFrame *frame);

} HttpWebSocketParserSettings;

/**
 * @brief An incremental parser of frames: the bytes of a frame can arrive in many calls, and a call can have many
 * frames.
 */
typedef struct _HttpWebSocketParser {

	/** @brief The role of this side: the frames that are parsed are the ones of the other side. */
	HttpWebSocketRole role;

	/** @brief RSV1 is valid only when an extension that uses it was negotiated (permessage-deflate). */
	bool allow_rsv1;

	/** @brief The most bytes of the payload of a frame. */
	u64 max_frame;

	/** @brief The state of the parser. */
	HttpWebSocketParserState state;

	/** @brief The bytes of the header that were received. */
	u8 header[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE];
	/** @brief The number of bytes of the header that were received. */
	u8 header_size;
	/** @brief The bytes that the header needs, known after its first two. */
	u8 header_needed;

	/** @brief The frame in progress. */
	HttpWebSocketFrame frame;

	/** @brief The bytes of the payload of the current frame that were parsed. */
	u64 received;

	/** @brief The error of a failed frame. */
	HttpWebSocketFrameError error;

	/** @brief The methods of the parser. */
	const HttpWebSocketParserSettings *settings;

	/** @brief The data of the methods. */
	void *data;

} HttpWebSocketParser;

/**
 * @brief Prepares a parser for the frames of the other side of a role.
 *
 * @param[out] parser the parser, it can be NULL
 * @param[in] role the role of this side
 * @param[in] max_frame the most bytes of the payload of a frame
 * @param[in] settings the methods, they must live as long as the parser
 * @param[in] data the data of the methods
 */
CERVER_PUBLIC void http_web_socket_parser_init (
	HttpWebSocketParser *parser,
	const HttpWebSocketRole role, const u64 max_frame,
	const HttpWebSocketParserSettings *settings, void *data
);

/**
 * @brief Parses some bytes; the payloads are unmasked in place.
 *
 * A frame whose last bytes stopped the parser ends in the next call, that can have no bytes; a length that is not
 * the shortest encoding of its value fails the frame (RFC 6455 section 5.2).
 *
 * @param[in,out] parser the parser
 * @param[in,out] data the bytes, NULL only when size is 0
 * @param[in] size the number of bytes
 * @return the bytes that were parsed: fewer than size when a method stopped it or when a frame was invalid (then
 * the state is HTTP_WEB_SOCKET_PARSER_STATE_FAILED and error is set); 0 for a NULL parser or one without settings
 */
CERVER_PUBLIC size_t http_web_socket_parser_execute (
	HttpWebSocketParser *parser, u8 *data, const size_t size
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
