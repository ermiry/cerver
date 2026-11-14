#ifndef _CERVER_CONNECTION_UPDATE_H_
#define _CERVER_CONNECTION_UPDATE_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// starts listening and receiving data in the connection sock
// the connection's updating value must be set before creating the thread
CERVER_PRIVATE void *connection_update (void *ptr);

// returns true if the current thread is the update thread of the connection,
// like a packet handler with direct handle that runs in it
CERVER_PRIVATE bool connection_is_update_thread (
	const Connection *connection
);

// wakes up the update thread of the connection that is blocked in recv (),
// the socket is shut down but it is only closed by connection_end (),
// so its fd can not be reused while the update thread uses it
// the connection's mutex must be held
CERVER_PRIVATE void connection_wake_update (Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
