#ifndef _CERVER_TESTS_WEBSOCKET_H_
#define _CERVER_TESTS_WEBSOCKET_H_

/*** units ***/

extern void web_socket_tests_text (void);

extern void web_socket_tests_frame (void);

extern void web_socket_tests_message (void);

extern void web_socket_tests_handshake (void);

extern void web_socket_tests_deflate (void);

/*** integration ***/

extern void web_socket_tests_server (void);

extern void web_socket_tests_client (void);

#endif
