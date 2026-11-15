#ifndef _CERVER_CONNECTION_LIFECYCLE_H_
#define _CERVER_CONNECTION_LIFECYCLE_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// sets up the new connection values
CERVER_PRIVATE u8 connection_init (Connection *connection);

// starts a connection -> connects to the specified ip and port
// sets connection's state based on result
// returns 0 on success, 1 on error
CERVER_PRIVATE int connection_connect (Connection *connection);

// asks the update thread of a connection with attempt reconnect
// to close its socket and to connect again
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_reconnect (Connection *connection);

// starts the threads of a connection that is connected,
// like the send queue thread
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int connection_start (Connection *connection);
// wakes up the connection that waits in the backoff of connection_connect (),
// it stops waiting if its client was torn down
CERVER_PRIVATE void connection_wake_connect (Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
