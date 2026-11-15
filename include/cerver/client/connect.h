/**
 * @file cerver/client/connect.h
 * @brief How a client connects to a cerver, starts its connections, ends them and is torn down.
 */

#ifndef _CERVER_CLIENT_CONNECT_H_
#define _CERVER_CLIENT_CONNECT_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup client_connect Connect
 * @ingroup client
 * @brief Connecting the connections of a client, starting them and ending them.
 * @{
 */

/**
 * @brief Connects the connection of a client to the host of its values, that can be a cerver or not.
 *
 * It blocks until the connection is established or it gives up (see connection_set_max_sleep ()); it triggers
 * CLIENT_EVENT_CONNECTED or CLIENT_EVENT_CONNECTION_FAILED. The application handles how it receives the packets and
 * sends its requests; an UDP connection asks for the cerver info and sends its auth packet.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the connection was established
 * @retval 1 a NULL argument or it failed to connect
 */
CERVER_EXPORT unsigned int client_connect (
	Client *client, struct _Connection *connection
);

/**
 * @brief An UDP cerver only knows a connection when it sends a datagram: the connection asks for the cerver info
 * and sends its auth packet (if it has auth data).
 *
 * @param[in] client the client
 * @param[in,out] connection the connection, one that is not UDP is ignored
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_connect_datagram (
	Client *client, struct _Connection *connection
);

/**
 * @brief Connects like client_connect () and then reads a first packet, the info of the cerver.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the connection was established
 * @retval 1 a NULL argument or it failed to connect
 */
CERVER_EXPORT unsigned int client_connect_to_cerver (
	Client *client, struct _Connection *connection
);

/**
 * @brief Connects like client_connect () in a new detachable thread, without blocking.
 *
 * client_requests_wait () and client_teardown () wait for the thread.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the thread was created
 * @retval 1 a NULL argument or the thread could not be created
 */
CERVER_EXPORT unsigned int client_connect_async (
	Client *client, struct _Connection *connection
);

/**
 * @brief Connects like client_connect_to_cerver () in a new detachable thread, without blocking.
 *
 * client_requests_wait () and client_teardown () wait for the thread.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the thread was created
 * @retval 1 a NULL argument or the thread could not be created
 */
CERVER_EXPORT unsigned int client_connect_to_cerver_async (
	Client *client, struct _Connection *connection
);

/**
 * @brief Starts a connection that is connected: it starts the client (its handlers) and the connection (its send
 * queue), and creates its update thread, that receives and handles its packets.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection, it must be active
 * @retval 0 the connection was started
 * @retval 1 a NULL argument, an inactive connection, or the client, the connection or the thread failed to start
 */
CERVER_EXPORT int client_connection_start (
	Client *client, struct _Connection *connection
);

/**
 * @brief Connects the connection like client_connect () and then starts it like client_connection_start ().
 *
 * It blocks until the connection succeeds or fails.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the connection was established and started
 * @retval 1 a NULL argument, or it failed to connect or to start
 */
CERVER_EXPORT int client_connect_and_start (
	Client *client, struct _Connection *connection
);

/**
 * @brief Connects and starts the connection like client_connect_and_start () in a new detachable thread, without
 * blocking.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the thread was created
 * @retval 1 a NULL argument or the thread could not be created
 */
CERVER_EXPORT u8 client_connect_and_start_async (
	Client *client, struct _Connection *connection
);

/**
 * @brief Closes the socket of the connection and sets it as inactive, triggering CLIENT_EVENT_CONNECTION_CLOSE.
 *
 * It does not send a close connection packet to the cerver.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the connection was stopped
 * @retval 1 a NULL argument
 */
CERVER_PUBLIC int client_connection_stop (
	Client *client, struct _Connection *connection
);

/**
 * @brief Sends a close connection packet to the cerver (when the connection got its info) and closes the socket,
 * but does not destroy the connection.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 the connection was closed
 * @retval 1 a NULL argument
 */
CERVER_PUBLIC int client_connection_close (
	Client *client, struct _Connection *connection
);

/**
 * @brief Ends and destroys a connection of the client that is connected to a cerver.
 *
 * The end belongs to the first thread that asks for it: from the update thread of the connection (a handler) the
 * end is deferred until its receive loop ends; from another thread it waits for the update thread before the
 * connection is deleted.
 *
 * @param[in,out] client the client
 * @param[in] connection the connection, it must not be used after the call
 * @retval 0 the connection was ended, or another thread is ending it
 * @retval 1 a NULL argument or the connection does not belong to the client
 */
CERVER_PUBLIC int client_connection_end (
	Client *client, struct _Connection *connection
);

/**
 * @brief Stops every connection and process of the client and destroys it.
 *
 * It cancels the connect backoff of the connections, waits for the async threads, ends the connections, waits for
 * the update threads, stops the handlers and waits for the threads of the events.
 *
 * @param[in] client the client, invalid after a successful call
 * @retval 0 the client was torn down
 * @retval 1 the client is NULL, or it was called from the update thread of one of its connections (a handler with
 * direct handle), which can not wait for itself: then the client is not destroyed
 * @warning Use client_teardown_async () from a handler.
 */
CERVER_EXPORT u8 client_teardown (Client *client);

/**
 * @brief Calls client_teardown () in a new detachable thread, as the handlers might need time to stop.
 *
 * @param[in] client the client, invalid once the thread tears it down
 * @retval 0 the thread was created
 * @retval 1 the client is NULL or the thread could not be created
 */
CERVER_EXPORT u8 client_teardown_async (Client *client);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
