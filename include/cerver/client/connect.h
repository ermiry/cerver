#ifndef _CERVER_CLIENT_CONNECT_H_
#define _CERVER_CLIENT_CONNECT_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region connect

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
CERVER_EXPORT unsigned int client_connect (
	Client *client, struct _Connection *connection
);

// an UDP cerver only knows a connection when it sends a datagram:
// the connection asks for the cerver info and sends its auth packet (if it has auth data)
CERVER_PRIVATE void client_connect_datagram (
	Client *client, struct _Connection *connection
);

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
CERVER_EXPORT unsigned int client_connect_to_cerver (
	Client *client, struct _Connection *connection
);

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
CERVER_EXPORT unsigned int client_connect_async (
	Client *client, struct _Connection *connection
);

// works like client_connect_to_cerver ()
// performs a first read to get the cerver info packet
// this is NOT a blocking method, a new thread will be created,
// client_requests_wait () and client_teardown () wait for it
// returns 0 on success creating connection thread, 1 on error
CERVER_EXPORT unsigned int client_connect_to_cerver_async (
	Client *client, struct _Connection *connection
);

/*** start ***/

// after a client connection successfully connects to a server,
// it will start the connection's update thread to enable the connection to
// receive & handle packets in a dedicated thread
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connection_start (
	Client *client, struct _Connection *connection
);

// connects a client connection to a server
// and after a success connection, it will start the connection (create update thread for receiving messages)
// this is a blocking method, returns only after a success or failed connection
// returns 0 on success, 1 on error
CERVER_EXPORT int client_connect_and_start (
	Client *client, struct _Connection *connection
);

// connects a client connection to a server in a new thread to avoid blocking the calling thread,
// and after a success connection, it will start the connection (create update thread for receiving messages)
// returns 0 on success creating connection thread, 1 on error
CERVER_EXPORT u8 client_connect_and_start_async (
	Client *client, struct _Connection *connection
);

/*** end ***/

// closes the connection's socket & set it to be inactive
// does not send a close connection packet to the cerver
// returns 0 on success, 1 on error
CERVER_PUBLIC int client_connection_stop (
	Client *client, struct _Connection *connection
);

// terminates the connection & closes the socket
// but does NOT destroy the current connection
// returns 0 on success, 1 on error
CERVER_PUBLIC int client_connection_close (
	Client *client, struct _Connection *connection
);

// terminates and destroys a connection registered to a client
// that is connected to a cerver
// returns 0 on success, 1 on error
CERVER_PUBLIC int client_connection_end (
	Client *client, struct _Connection *connection
);

// stop any on going connection and process and destroys the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_teardown (Client *client);

// calls client_teardown () in a new thread as handlers might need time to stop
// that will cause the calling thread to wait at least a second
// returns 0 on success creating thread, 1 on error
CERVER_EXPORT u8 client_teardown_async (Client *client);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
