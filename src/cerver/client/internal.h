/**
 * @file client/internal.h
 * @internal
 * @brief The methods that the parts of the client share, never exported by libcerver.so.
 */

#ifndef _CERVER_CLIENT_INTERNAL_H_
#define _CERVER_CLIENT_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/files/send.h"
#include "cerver/handler/receive.h"
#include "cerver/packets/packets.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Allocates a ClientConnection with a client and one of its connections.
 *
 * @param[in] client the client
 * @param[in] connection the connection
 * @return the new ClientConnection, released with client_connection_aux_delete (); NULL if it could not be
 * allocated
 */
CERVER_HIDDEN ClientConnection *client_connection_aux_new (
	Client *client, Connection *connection
);

/**
 * @internal
 * @brief Starts a client that is not running: it sets it as running and starts its handlers.
 *
 * @param[in,out] client the client
 * @retval 0 the client was started, or it was already running
 * @retval 1 a NULL client or its handlers failed to start
 */
CERVER_HIDDEN u8 client_start (Client *client);

/**
 * @internal
 * @brief Releases the action of an event, its response and its arguments.
 *
 * @param[in] ptr the event, it can be NULL
 */
CERVER_HIDDEN void client_event_delete (void *ptr);

/**
 * @internal
 * @brief Releases the action of an error and its arguments.
 *
 * @param[in] client_error_ptr the error, it can be NULL
 */
CERVER_HIDDEN void client_error_delete (void *client_error_ptr);

/**
 * @internal
 * @brief Handles a PACKET_TYPE_ERROR packet: it triggers the error of the client of its type.
 *
 * @param[in] packet the packet
 */
CERVER_HIDDEN void client_error_packet_handler (Packet *packet);

/**
 * @internal
 * @brief Counts an async thread of the client that starts (delta > 0) or ends, and wakes up the threads that wait
 * for them.
 *
 * @param[in,out] client the client, one without its lock is ignored
 * @param[in] delta positive for a new thread, otherwise one that ended
 */
CERVER_HIDDEN void client_async_thread_count (Client *client, const int delta);

/**
 * @internal
 * @brief Creates a detachable async thread of the client, counted for client_requests_wait () and
 * client_teardown ().
 *
 * @param[in] cc the client and the connection, the thread owns it; released when the thread could not be created
 * @param[in] work the method of the thread
 * @retval 0 the thread was created
 * @retval 1 the thread could not be created
 */
CERVER_HIDDEN unsigned int client_async_thread_create (
	ClientConnection *cc, void *(*work) (void *)
);

/**
 * @internal
 * @brief Asks the update thread of the connection to end it, when it has one.
 *
 * @param[in,out] connection the connection
 * @return true if the connection has an update thread, that ends it
 */
CERVER_HIDDEN bool client_connection_end_request (Connection *connection);

/**
 * @internal
 * @brief The default file upload handler: it saves the file in the uploads path of the client, as
 * `<uploads path>/<time>-<filename>`.
 *
 * @param[in,out] client the client, with its uploads path
 * @param[in,out] connection the connection
 * @param[in] file_header the header of the file
 * @param[in] file_data the first bytes of the file, that came with its packet
 * @param[in] file_data_len the size of file_data
 * @param[out] saved_filename the name of the saved file, allocated with malloc ()
 * @retval 0 the file was saved
 * @retval 1 the file could not be received
 */
CERVER_HIDDEN u8 client_file_receive (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

/**
 * @internal
 * @brief Handles a packet that a client received, checking it first when the client checks its packets.
 *
 * @param[in] packet the packet
 * @retval 0 the packet was handled
 * @retval 1 the packet was bad or could not be handled
 */
CERVER_HIDDEN u8 client_packet_handler (Packet *packet);

/**
 * @internal
 * @brief Assembles the packets from the received buffers, no matter where the buffers are split, and handles
 * them.
 *
 * @param[in,out] receive_handle the receive of the connection
 */
CERVER_HIDDEN void client_receive_handle_buffer (ReceiveHandle *receive_handle);

/**
 * @internal
 * @brief Performs the recv () of the socket of the connection and handles its failures.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @param[out] buffer the buffer
 * @param[in] buffer_size the size of the buffer
 * @param[out] rc the bytes received
 * @return RECEIVE_ERROR_NONE, RECEIVE_ERROR_TIMEOUT, or the error of the receive
 */
CERVER_HIDDEN ReceiveError client_receive_actual (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t *rc
);

/**
 * @internal
 * @brief Handles a datagram that an UDP connection received: a plain one, or one with the token of the connection
 * once it has one.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @param[in] datagram the datagram
 * @param[in] size the size of the datagram
 * @retval 0 the packet of the datagram was handled
 * @retval 1 the datagram was not valid
 */
CERVER_HIDDEN u8 client_receive_datagram (
	Client *client, Connection *connection,
	const char *datagram, const size_t size
);

/**
 * @internal
 * @brief Reads an amount of bytes from the socket of the connection into the buffer.
 *
 * It only returns once the requested bytes have been received or on an error; a timeout receives again.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @param[out] buffer the buffer
 * @param[in] buffer_size the size of the buffer
 * @param[in] requested_data the bytes to read
 * @retval RECEIVE_ERROR_NONE every byte was received
 * @retval RECEIVE_ERROR_FAILED the receive failed before
 */
CERVER_HIDDEN ReceiveError client_receive_data (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t requested_data
);

#ifdef __cplusplus
}
#endif

#endif
