/**
 * @file cerver/connection/send.h
 * @brief The send queue of a connection.
 */

#ifndef _CERVER_CONNECTION_SEND_H_
#define _CERVER_CONNECTION_SEND_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup connection_send Send
 * @ingroup connection
 * @brief Sending the packets of a connection with its queue.
 * @{
 */

/**
 * @brief Pushes a packet to the send queue of the connection; the send thread sends it and deletes it.
 *
 * @param[in] connection the connection, with connection_set_send_queue ()
 * @param[in] packet the packet, it always belongs to the connection: one that can not be queued (or a connection
 * without a send queue) deletes it
 */
CERVER_PUBLIC void connection_send_packet (
	Connection *connection, struct _Packet *packet
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
