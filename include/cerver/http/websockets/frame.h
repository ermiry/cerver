#ifndef _CERVER_HTTP_WEBSOCKETS_FRAME_H_
#define _CERVER_HTTP_WEBSOCKETS_FRAME_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

// the least and most bytes of the header of a frame (RFC 6455 section 5.2)
#define HTTP_WEB_SOCKET_FRAME_HEADER_MIN_SIZE		2
#define HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE		14

#define HTTP_WEB_SOCKET_MASK_SIZE					4

// the most bytes of the payload of a control frame
#define HTTP_WEB_SOCKET_CONTROL_MAX_SIZE			125

// the bytes of the code of a close frame
#define HTTP_WEB_SOCKET_CLOSE_CODE_SIZE				2

// the most bytes of the reason of a close frame
#define HTTP_WEB_SOCKET_CLOSE_REASON_MAX_SIZE		(HTTP_WEB_SOCKET_CONTROL_MAX_SIZE - HTTP_WEB_SOCKET_CLOSE_CODE_SIZE)

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_WEB_SOCKET_OPCODE_MAP(XX)						\
	XX(0x0,	CONTINUATION,	Continuation)					\
	XX(0x1,	TEXT,			Text)							\
	XX(0x2,	BINARY,			Binary)							\
	XX(0x8,	CLOSE,			Close)							\
	XX(0x9,	PING,			Ping)							\
	XX(0xA,	PONG,			Pong)

typedef enum HttpWebSocketOpcode {

	#define XX(num, name, string) HTTP_WEB_SOCKET_OPCODE_##name = num,
	HTTP_WEB_SOCKET_OPCODE_MAP (XX)
	#undef XX

} HttpWebSocketOpcode;

CERVER_PUBLIC const char *http_web_socket_opcode_to_string (
	const HttpWebSocketOpcode opcode
);

// returns true for the opcodes of RFC 6455, the others are reserved
CERVER_PUBLIC bool http_web_socket_opcode_is_known (const u8 opcode);

// returns true for close, ping and pong
CERVER_PUBLIC bool http_web_socket_opcode_is_control (const u8 opcode);

// the status codes of a close frame (RFC 6455 section 7.4.1 and the IANA registry)
#define HTTP_WEB_SOCKET_CLOSE_MAP(XX)																			\
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

typedef enum HttpWebSocketCloseCode {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_CLOSE_##name = num,
	HTTP_WEB_SOCKET_CLOSE_MAP (XX)
	#undef XX

} HttpWebSocketCloseCode;

CERVER_PUBLIC const char *http_web_socket_close_code_to_string (const u16 code);

CERVER_PUBLIC const char *http_web_socket_close_code_description (const u16 code);

// returns true for a code that can be sent in a close frame:
// 1000 to 1003, 1007 to 1014, and 3000 to 4999 (registered and private)
// 1005, 1006 and 1015 only report a close, they are never sent
CERVER_PUBLIC bool http_web_socket_close_code_is_valid (const u16 code);

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

typedef enum HttpWebSocketFrameError {

	#define XX(num, name, string, description) HTTP_WEB_SOCKET_FRAME_ERROR_##name = num,
	HTTP_WEB_SOCKET_FRAME_ERROR_MAP (XX)
	#undef XX

} HttpWebSocketFrameError;

CERVER_PUBLIC const char *http_web_socket_frame_error_to_string (
	const HttpWebSocketFrameError error
);

CERVER_PUBLIC const char *http_web_socket_frame_error_description (
	const HttpWebSocketFrameError error
);

// the code of the close frame that answers the error, 1009 for a frame too big, 1002 for the rest
CERVER_PUBLIC u16 http_web_socket_frame_error_close_code (
	const HttpWebSocketFrameError error
);

// the side of the connection, a client masks its frames and a server never does
typedef enum HttpWebSocketRole {

	HTTP_WEB_SOCKET_ROLE_SERVER = 0,
	HTTP_WEB_SOCKET_ROLE_CLIENT = 1

} HttpWebSocketRole;

typedef struct HttpWebSocketFrame {

	bool fin;

	bool rsv1;
	bool rsv2;
	bool rsv3;

	u8 opcode;

	bool masked;
	u8 mask[HTTP_WEB_SOCKET_MASK_SIZE];

	u64 length;

} HttpWebSocketFrame;

// writes the header of a frame in header, that has HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE bytes,
// always with the least bytes for the length, and with the mask when it is not NULL
// returns the size of the header
CERVER_PUBLIC size_t http_web_socket_frame_header (
	u8 *header,
	const bool fin, const bool rsv1,
	const u8 opcode, const u64 length,
	const u8 *mask
);

// applies the mask to the bytes, that are at offset from the start of the payload,
// the same call masks and unmasks
CERVER_PUBLIC void http_web_socket_mask (
	u8 *data, const size_t size,
	const u8 *mask, const u64 offset
);

typedef enum HttpWebSocketParserState {

	HTTP_WEB_SOCKET_PARSER_STATE_HEADER = 0,
	HTTP_WEB_SOCKET_PARSER_STATE_PAYLOAD = 1,
	HTTP_WEB_SOCKET_PARSER_STATE_FAILED = 2

} HttpWebSocketParserState;

struct _HttpWebSocketParser;

// the methods of the parser return 0 to continue, any other value stops it
typedef struct HttpWebSocketParserSettings {

	// a header was parsed, before its payload
	int (*on_frame_begin)(struct _HttpWebSocketParser *parser, const HttpWebSocketFrame *frame);

	// a piece of the payload, already unmasked, it can arrive in many pieces
	int (*on_frame_data)(struct _HttpWebSocketParser *parser, u8 *data, size_t size);

	// the whole payload was received
	int (*on_frame_end)(struct _HttpWebSocketParser *parser, const HttpWebSocketFrame *frame);

} HttpWebSocketParserSettings;

// an incremental parser of frames: the bytes of a frame can arrive in many calls,
// and a call can have many frames
typedef struct _HttpWebSocketParser {

	HttpWebSocketRole role;

	// RSV1 is valid only when an extension that uses it was negotiated (permessage-deflate)
	bool allow_rsv1;

	// the most bytes of the payload of a frame
	u64 max_frame;

	HttpWebSocketParserState state;

	u8 header[HTTP_WEB_SOCKET_FRAME_HEADER_MAX_SIZE];
	u8 header_size;
	u8 header_needed;

	HttpWebSocketFrame frame;

	// the bytes of the payload of the current frame that were parsed
	u64 received;

	HttpWebSocketFrameError error;

	const HttpWebSocketParserSettings *settings;

	void *data;

} HttpWebSocketParser;

// prepares the parser for the frames of the other side of the role
CERVER_PUBLIC void http_web_socket_parser_init (
	HttpWebSocketParser *parser,
	const HttpWebSocketRole role, const u64 max_frame,
	const HttpWebSocketParserSettings *settings, void *data
);

// parses the bytes, the payloads are unmasked in place
// returns the bytes that were parsed, fewer than size when a method stopped it
// or when a frame was invalid (the state is HTTP_WEB_SOCKET_PARSER_STATE_FAILED and error is set)
CERVER_PUBLIC size_t http_web_socket_parser_execute (
	HttpWebSocketParser *parser, u8 *data, const size_t size
);

#ifdef __cplusplus
}
#endif

#endif
