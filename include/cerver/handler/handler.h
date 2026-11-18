/**
 * @file cerver/handler/handler.h
 * @brief The handlers of the packets and how a cerver receives and handles its connections.
 */

#ifndef _CERVER_HANDLER_H_
#define _CERVER_HANDLER_H_

#include <stdlib.h>

#include "cerver/types/types.h"

#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/receive.h"
#include "cerver/packets/packets.h"

#include "cerver/game/lobby.h"

#include "cerver/threads/jobs.h"

#include "cerver/utils/histogram.h"

/**
 * @defgroup handler Handler
 * @brief The handlers of the packets, the events and the errors, and the receive, the poll and the threads of the
 * connections of a cerver.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct _Socket;

struct _Admin;
struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;

struct _Packet;

/**
 * @defgroup handler_main Handler
 * @ingroup handler
 * @brief A handler of packets: a method that runs in its own thread with a job queue, or directly.
 * @{
 */

/**
 * @brief Who a handler belongs to.
 */
typedef enum HandlerType {

	/** @brief It does not belong to anyone yet, it can not start. */
	HANDLER_TYPE_NONE         = 0,

	/** @brief A handler of a cerver. */
	HANDLER_TYPE_CERVER       = 1,
	/** @brief A handler of a client. */
	HANDLER_TYPE_CLIENT       = 2,
	/** @brief A handler of an admin cerver. */
	HANDLER_TYPE_ADMIN        = 3,

} HandlerType;

/**
 * @brief What the method of a handler with a job queue gets.
 */
typedef struct HandlerData {

	/** @brief The id of the handler. */
	int handler_id;

	/** @brief The data of the handler. */
	void *data;
	/** @brief The packet to handle. */
	struct _Packet *packet;

} HandlerData;

/**
 * @brief The packets that a handler handled, added from the threads that run it.
 */
typedef struct HandlerStats {

	/** @brief The microseconds that the handler took with each packet. */
	Histogram durations;
	/** @brief The bytes of each packet, with its header. */
	Histogram sizes;

} HandlerStats;

/**
 * @brief A handler of packets.
 */
struct _Handler {

	/** @brief Who the handler belongs to, set when it is added to a cerver, a client or an admin cerver. */
	HandlerType type;

	/** @brief A unique id, added every time a handler is created. */
	int unique_id;

	/** @brief The id of the handler in the multiple app handlers of a cerver. */
	int id;
	/** @brief The thread of the handler. */
	pthread_t thread_id;

	/** @brief Its thread was started and has not been joined yet. */
	bool joinable;

	/** @brief The data of the handler, passed to the method with each packet. */
	void *data;

	/** @brief Creates the data of the handler when its thread starts; it returns newly allocated data. */
	void *(*data_create) (void *args);
	/** @brief The argument of #data_create. */
	void *data_create_args;

	/**
	 * @brief Releases the data of the handler once, when its thread ends, or when the handler is deleted if it
	 * never had a thread; without it the data is not released.
	 */
	Action data_delete;

	/** @brief The method that handles the packets. */
	Action handler;

	/**
	 * @brief The packets are handled directly in the thread that received them instead of the job queue (false by
	 * default).
	 *
	 * The handle is immediate and the method can run in many threads at the same time, but it gets the Packet
	 * without the data of the handler, and the receiving thread is busy until the method returns.
	 */
	bool direct_handle;

	/** @brief The packets that wait to be handled. */
	JobQueue *job_queue;

	/** @brief The cerver that the handler belongs to. */
	struct _Cerver *cerver;
	/** @brief The client that the handler belongs to. */
	struct _Client *client;

	/** @brief The stats of the packets that the handler handled. */
	HandlerStats stats;

};

/** @brief A handler of packets, see struct _Handler. */
typedef struct _Handler Handler;

/**
 * @brief Releases a handler and its job queue, and its data if it never had a thread.
 *
 * A handler whose thread was never stopped detaches it, and its thread still uses (and releases) its data.
 *
 * @param[in] handler_ptr the handler, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void handler_delete (void *handler_ptr);

/**
 * @brief Creates a handler with its method and its job queue.
 *
 * @param[in] handler_method the method that handles the packets: with a job queue it gets a HandlerData, directly
 * a Packet
 * @return the new handler, NULL if it or its job queue could not be allocated
 */
CERVER_EXPORT Handler *handler_create (Action handler_method);

/**
 * @brief Creates a handler with an id for the multiple app handlers of a cerver (cerver_handlers_add ()).
 *
 * The id matches the handler id of the header of the incoming packets; register it before the cerver starts.
 *
 * @param[in] id the id, set by the application
 * @param[in] handler_method the method that handles the packets
 * @return the new handler, NULL if it could not be allocated
 */
CERVER_EXPORT Handler *handler_create_with_id (
	int id, Action handler_method
);

/**
 * @brief Sets the data of the handler.
 *
 * A handler with a queue passes it to its method in a HandlerData, a direct handler only gets the Packet.
 *
 * @param[in,out] handler the handler, it can be NULL
 * @param[in] data the data
 */
CERVER_EXPORT void handler_set_data (
	Handler *handler, void *data
);

/**
 * @brief Sets a method that creates the data of the handler in its thread, before it handles any packet.
 *
 * The data is passed to the method in a HandlerData; a direct handler does not have a thread, so it is never
 * called.
 *
 * @param[in,out] handler the handler, it can be NULL
 * @param[in] data_create creates the data
 * @param[in] data_create_args the argument of data_create
 */
CERVER_EXPORT void handler_set_data_create (
	Handler *handler,
	void *(*data_create) (void *args), void *data_create_args
);

/**
 * @brief Sets the method that releases the data of the handler.
 *
 * It is called once, when the thread of the handler ends, or when the handler is deleted if it never had a thread.
 *
 * @param[in,out] handler the handler, it can be NULL
 * @param[in] data_delete releases the data
 */
CERVER_EXPORT void handler_set_data_delete (
	Handler *handler, Action data_delete
);

/**
 * @brief Sets whether the packets are handled directly in the thread that received them, instead of the job queue.
 *
 * The handle is immediate and the method can run in many threads at the same time, but it gets the Packet without
 * the data of the handler, and the receiving thread is busy until the method returns.
 *
 * @param[in,out] handler the handler, it can be NULL
 * @param[in] direct_handle true for a direct handler, false (the default) for the job queue
 */
CERVER_EXPORT void handler_set_direct_handle (
	Handler *handler, bool direct_handle
);

/**
 * @brief Copies the stats of the packets that the handler handled.
 *
 * @param[in] handler the handler
 * @param[out] stats the copy; nothing is copied with a NULL argument
 */
CERVER_EXPORT void handler_get_stats (
	const Handler *handler, HandlerStats *stats
);

/**
 * @brief Prints the stats of the handler with the log of cerver.
 *
 * @param[in] handler the handler
 */
CERVER_EXPORT void handler_stats_print (const Handler *handler);

/**
 * @brief Starts the handler in its own joinable thread.
 *
 * @param[in,out] handler the handler, it must belong to someone (not HANDLER_TYPE_NONE)
 * @retval 0 the thread was started
 * @retval 1 a NULL handler, a handler of HANDLER_TYPE_NONE or the thread could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE int handler_start (Handler *handler);

/**
 * @brief Wakes the thread of the handler until it sees that its owner stopped, and joins it.
 *
 * @param[in,out] handler the handler, it can be NULL or without a thread
 * @note Call it after the cerver or the client stopped running. For the internal methods of cerver.
 */
CERVER_PRIVATE void handler_stop (Handler *handler);

/**
 * @brief Passes a packet to its handler, directly or by its job queue.
 *
 * A direct handler releases the packet after it returns only if delete_packet is set, and the packet is always
 * released when there is no handler or it can not be queued.
 *
 * @param[in] handler the handler, it can be NULL
 * @param[in] packet the packet
 * @param[in] delete_packet whether the packet is released after a direct handler
 * @param[in] owner_name the name of the owner, only for the logs
 * @param[in] handler_name the name of the handler, only for the logs
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void handler_packet_dispatch (
	Handler *handler, struct _Packet *packet, const bool delete_packet,
	const char *owner_name, const char *handler_name
);

/** @} */

/**
 * @defgroup handler_handlers Handlers
 * @ingroup handler
 * @brief The handlers of the packets that cerver handles by itself.
 * @{
 */

/**
 * @brief The X-macro table of the errors of the handlers: the number, the name, the string and the description of
 * each one.
 */
#define CERVER_HANDLER_ERROR_MAP(XX)											\
	XX(0,	NONE,			None,				No handler error)				\
	XX(1,	PACKET,			Bad Packet,			Packet check failed)			\
	XX(2,	DROPPED,		Dropped Connection, The connection has been ended)

/**
 * @brief An error of a handler, one `CERVER_HANDLER_ERROR_<name>` for each row of #CERVER_HANDLER_ERROR_MAP.
 */
typedef enum CerverHandlerError {

	#define XX(num, name, string, description) CERVER_HANDLER_ERROR_##name = num,
	CERVER_HANDLER_ERROR_MAP (XX)
	#undef XX

} CerverHandlerError;

/**
 * @brief Returns the string of an error of a handler.
 *
 * @param[in] error the error
 * @return the string of its row in #CERVER_HANDLER_ERROR_MAP, or the one of CERVER_HANDLER_ERROR_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_handler_error_to_string (
	const CerverHandlerError error
);

/**
 * @brief Returns the description of an error of a handler.
 *
 * @param[in] error the error
 * @return the description of its row in #CERVER_HANDLER_ERROR_MAP, or the one of CERVER_HANDLER_ERROR_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_handler_error_description (
	const CerverHandlerError error
);

/**
 * @brief Handles the request of a client to get a file.
 *
 * @param[in] packet the request packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_request_get_file (
	struct _Packet *packet
);

/**
 * @brief Handles the request of a client to upload a file.
 *
 * @param[in] packet the request packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_request_send_file (
	struct _Packet *packet
);

/**
 * @brief Sends a test packet back to the client.
 *
 * @param[in] packet the test packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_test_packet_handler (
	struct _Packet *packet
);

/**
 * @brief Handles a PACKET_TYPE_CLIENT packet that was received outside the packet handler, like the ones that the
 * load balancer receives.
 *
 * @param[in] packet the packet
 * @return CERVER_HANDLER_ERROR_NONE, or the error of the packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE CerverHandlerError cerver_client_packet_handle (
	struct _Packet *packet
);

/**
 * @brief Handles the packet of a datagram of an UDP cerver, like the packets of a TCP connection.
 *
 * The client, request and cerver packets of a peer are handled by its peers (cerver/network/udp.h).
 *
 * @param[in] packet the packet
 * @retval 0 the packet was handled
 * @retval 1 the packet was bad or its peer must be removed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_udp_packet_handle (struct _Packet *packet);

/** @} */

/**
 * @defgroup handler_cerver_receive Cerver receive
 * @ingroup handler
 * @brief How a cerver receives from its connections.
 * @{
 */

/** @brief The deleted receives that are kept to be reused. */
#define CERVER_RECEIVE_RECYCLE_LIMIT				FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

/**
 * @brief The default receive handler of a cerver: it assembles the cerver packets of the buffer and handles them.
 *
 * @param[in] receive_handle_ptr the ReceiveHandle of the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_receive_handle_buffer (
	void *receive_handle_ptr
);

/**
 * @brief A receive from a connection of a cerver.
 */
typedef struct CerverReceive {

	/** @brief What the receive is for. */
	ReceiveType type;

	/** @brief The cerver. */
	struct _Cerver *cerver;

	/** @brief The socket, with a reference of the receive. */
	struct _Socket *socket;
	/** @brief The connection. */
	struct _Connection *connection;
	/** @brief The client of the connection. */
	struct _Client *client;
	/** @brief The admin of the connection. */
	struct _Admin *admin;

	/** @brief The lobby of the connection. */
	struct _Lobby *lobby;

	/** @brief The next deleted receive that waits to be reused. */
	struct CerverReceive *recycle_next;

} CerverReceive;

/**
 * @brief Releases a receive and its reference to the socket.
 *
 * @param[in] ptr the receive, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_receive_delete (void *ptr);

/**
 * @brief Creates a receive for the connection of a socket of the cerver.
 *
 * @param[in] receive_type what the receive is for
 * @param[in] cerver the cerver
 * @param[in] sock_fd the socket of the connection
 * @return the new receive, without a connection if the socket does not have one; NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE CerverReceive *cerver_receive_create (
	ReceiveType receive_type,
	struct _Cerver *cerver,
	const i32 sock_fd
);

/**
 * @brief Creates a receive for a connection and its client.
 *
 * @param[in] receive_type what the receive is for
 * @param[in] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection, the receive takes a reference to its socket
 * @return the new receive, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE CerverReceive *cerver_receive_create_full (
	ReceiveType receive_type,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection
);

/**
 * @brief Handles a failed receive from a connection of a client: it ends the connection, so its socket is never
 * used again.
 *
 * @param[in] cr the receive
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_receive_handle_failed (
	CerverReceive *cr
);

/**
 * @brief Handles the bytes that a receive got from its connection.
 *
 * @param[in] cr the receive
 * @param[in] packet_buffer the received bytes
 * @param[in] packet_buffer_size the number of received bytes
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_receive_internal (
	CerverReceive *cr,
	char *packet_buffer, const size_t packet_buffer_size
);

/** @} */

/**
 * @defgroup handler_register Register
 * @ingroup handler
 * @brief How the new connections of a cerver are handled.
 * @{
 */

/**
 * @brief Takes out of the cerver a new connection that no handler could take, with its new client.
 *
 * The client is unregistered while it still has the connection, so its socket leaves the maps, and the connection
 * is left for its caller to drop.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_new_connection_release (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection
);

/**
 * @brief Selects how a connection of a client is handled, with the handler type of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @retval 0 the connection is handled
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_register_new_connection_normal_default_select_handler (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection
);

/**
 * @brief Selects how a connection is handled, with the handler type of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @retval 0 the connection is handled
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_handler_register_connection (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection
);

/** @} */

/**
 * @defgroup handler_poll Poll
 * @ingroup handler
 * @brief The main poll of a cerver of CERVER_HANDLER_TYPE_POLL.
 * @{
 */

/**
 * @brief Grows the array of the main poll of the cerver; the current array is kept if it can not grow.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the array grew
 * @retval 1 a NULL cerver or it could not grow
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_realloc_main_poll_fds (
	struct _Cerver *cerver
);

/**
 * @brief Returns a free index of the main poll of the cerver.
 *
 * @param[in] cerver the cerver
 * @return the index, -1 for a NULL cerver or a full array
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE i32 cerver_poll_get_free_idx (
	struct _Cerver *cerver
);

/**
 * @brief Returns the index of a socket in the main poll of the cerver.
 *
 * @param[in] cerver the cerver
 * @param[in] sock_fd the socket
 * @return the index, -1 for a NULL cerver or a socket that is not in the poll
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE i32 cerver_poll_get_idx_with_sock_fd (
	struct _Cerver *cerver, i32 sock_fd
);

/**
 * @brief Registers a connection of a client in the main poll of the cerver, and maps its socket to the client.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was registered
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_poll_register_connection (
	struct _Cerver *cerver, struct _Connection *connection
);

/**
 * @brief Removes a socket from the main poll of the cerver, with the lock of the poll.
 *
 * @param[in,out] cerver the cerver
 * @param[in] sock_fd the socket
 * @retval 0 the socket was removed
 * @retval 1 a NULL cerver or a socket that is not in the poll
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_poll_unregister_sock_fd (
	struct _Cerver *cerver, const i32 sock_fd
);

/**
 * @brief Removes a connection of a client from the main poll of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] connection the connection
 * @retval 0 the connection was removed
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_poll_unregister_connection (
	struct _Cerver *cerver, struct _Connection *connection
);

/**
 * @brief The loop of the main poll: accepts the connections and handles the events of their sockets until the
 * cerver stops.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the cerver stopped
 * @retval 1 the poll could not start
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_poll (struct _Cerver *cerver);

/** @} */

/**
 * @defgroup handler_threads Threads
 * @ingroup handler
 * @brief The connections of a cerver of CERVER_HANDLER_TYPE_THREADS.
 * @{
 */

/**
 * @brief Accepts the new connections and handles each one in its own thread, until the cerver stops.
 *
 * @param[in,out] cerver the cerver
 * @retval 0 the cerver stopped
 * @retval 1 the accept loop could not start
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 cerver_threads (struct _Cerver *cerver);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
