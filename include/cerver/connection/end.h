#ifndef _CERVER_CONNECTION_END_H_
#define _CERVER_CONNECTION_END_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// waits until the send thread ends, the connection must not be active
CERVER_PRIVATE void connection_stop_send (Connection *connection);

// resets connection values to be able to connect it again,
// the connection must not have an update thread
CERVER_PRIVATE void connection_reset (Connection *connection);

// ends a client connection
CERVER_PRIVATE void connection_end (Connection *connection);

CERVER_PRIVATE void connection_drop (
	struct _Cerver *cerver, Connection *connection
);

// asks the cerver to drop the connection, it can be called from any packet handler:
// the socket is shut down (the packets that were already sent still arrive),
// and the receive loop ends and removes the connection when the handler returns,
// like when the client closes it, so the connection must not be used after this call
// returns 0 on success, 1 on error
CERVER_EXPORT u8 connection_drop_request (Connection *connection);

#ifdef __cplusplus
}
#endif

#endif
