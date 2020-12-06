#ifndef _CERVER_HTTP_WEBSOCKETS_H_
#define _CERVER_HTTP_WEBSOCKETS_H_

#include <stdlib.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#define WEB_SOCKET_FRAME_SIZE					128

// the default tmeout for a websocket sonnection
#define DEFAULT_WEB_SOCKET_RECV_TIMEOUT         5

struct _Cerver;
struct _Connection;

struct _HttpReceive;

typedef enum HttpWebSocketOpcode {

	HTTP_WEB_SOCKET_OPCODE_CONTINUATION		= 0x0,
	HTTP_WEB_SOCKET_OPCODE_TEXT				= 0x1,
	HTTP_WEB_SOCKET_OPCODE_BINRAY			= 0x2,
	HTTP_WEB_SOCKET_OPCODE_CLOSE			= 0x8,
	HTTP_WEB_SOCKET_OPCODE_PING				= 0x9,
	HTTP_WEB_SOCKET_OPCODE_PONG				= 0xA,

} HttpWebSocketOpcode;

enum _HttpWebSocketError {

	HTTP_WEB_SOCKET_ERROR_NONE                      = 0,

	HTTP_WEB_SOCKET_ERROR_READ_HANDSHAKE            = 1,
	HTTP_WEB_SOCKET_ERROR_WRITE_HANDSHAKE           = 2,

};

typedef enum _HttpWebSocketError HttpWebSocketError;

CERVER_PRIVATE void http_web_sockets_receive_handle (
	struct _HttpReceive *http_receive, 
	ssize_t rc, char *packet_buffer
);

// sends a ws message to the selected connection
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_web_sockets_send (
	struct _Cerver *cerver, struct _Connection *connection,
	const char *msg, const size_t msg_len
);

#endif