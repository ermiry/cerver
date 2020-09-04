#ifndef _CERVER_HTTP_WEB_SOCKET_H_
#define _CERVER_HTTP_WEB_SOCKET_H_

enum _HttpWebSocketError {

    HTTP_WEB_SOCKET_ERROR_NONE                      = 0,

    HTTP_WEB_SOCKET_ERROR_READ_HANDSHAKE            = 1,
    HTTP_WEB_SOCKET_ERROR_WRITE_HANDSHAKE           = 2,

};

typedef enum _HttpWebSocketError HttpWebSocketError;

#endif