#ifndef _CERVER_CONNECTION_SEND_H_
#define _CERVER_CONNECTION_SEND_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// pushes a packet to the connection's send queue,
// the send thread sends it and deletes it;
// the packet always belongs to the connection, one that can not be queued is deleted
CERVER_PUBLIC void connection_send_packet (
	Connection *connection, struct _Packet *packet
);

#ifdef __cplusplus
}
#endif

#endif
