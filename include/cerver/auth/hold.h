#ifndef _CERVER_AUTH_HOLD_H_
#define _CERVER_AUTH_HOLD_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/auth/auth.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Connection;
struct _Packet;

#pragma region handler

// handles a packet from an on hold connection
// returns 0 if we can / need to handle more packets
// returns 1 if the connection has been ended or removed from on hold
CERVER_PRIVATE u8 on_hold_packet_handler (
	struct _Packet *packet
);

#pragma endregion

#pragma region connections

// if the cerver requires authentication, we put the connection on hold
// until it has a sucess or failed authentication
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 on_hold_connection (
	struct _Cerver *cerver, struct _Connection *connection
);

// closes the on hold connection and removes it from the cerver,
// it triggers CERVER_EVENT_ON_HOLD_DROPPED
CERVER_PRIVATE void on_hold_connection_drop (
	const struct _Cerver *cerver, struct _Connection *connection
);

// closes the on hold connection that its client closed or lost,
// it triggers CERVER_EVENT_ON_HOLD_DISCONNECTED
CERVER_PRIVATE void on_hold_connection_disconnected (
	const struct _Cerver *cerver, struct _Connection *connection
);

#pragma endregion

#pragma region poll

// removed a sock fd from the cerver's on hold poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 on_hold_poll_unregister_sock_fd (
	struct _Cerver *cerver, const i32 sock_fd
);

// handles packets from the on hold clients until they authenticate
CERVER_PRIVATE void *on_hold_poll (void *cerver_ptr);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
