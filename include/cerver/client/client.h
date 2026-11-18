/**
 * @file cerver/client/client.h
 * @brief The Client: its stats, its struct, its setters and how it is registered to a cerver.
 */

#ifndef _CERVER_CLIENT_CLIENT_H_
#define _CERVER_CLIENT_CLIENT_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"
#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"

/**
 * @defgroup client Client
 * @brief A client: anyone that connects to a cerver, with one or more connections.
 *
 * A cerver creates a client for each new connection (or joins it to the client of its session); an application
 * creates its own clients with client_create () to connect to a cerver.
 */

/**
 * @defgroup client_defaults Defaults
 * @ingroup client
 * @brief The values that a new client starts with.
 * @{
 */

/** @brief The size of the name of a client, with its terminator. */
#define CLIENT_NAME_SIZE							64

/** @brief The max paths where a client searches the files that a cerver requests. */
#define CLIENT_FILES_MAX_PATHS       				32

/** @brief The name of a client created with client_create (). */
#define CLIENT_DEFAULT_NAME							"no-name"

/** @brief The default size in bytes of the biggest packet that a client handles. */
#define CLIENT_DEFAULT_MAX_RECEIVED_PACKET_SIZE		MAX_UDP_PACKET_SIZE

/** @brief The default bad packets that a connection of a client can receive before the client ends it. */
#define CLIENT_DEFAULT_MAX_BAD_PACKETS				16

/** @brief The pieces in which client_connection_get_next_packet () reads a packet. */
#define CLIENT_NEXT_PACKET_BUFFER_SIZE				(1 << 12)

/** @brief The deleted clients that are kept to be reused, a cerver creates one per connection. */
#define CLIENT_RECYCLE_LIMIT						FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _PacketsPerType;
struct _Handler;

struct FileHeader;

struct _ClientEvent;
struct _ClientError;

/**
 * @defgroup client_stats Stats
 * @ingroup client
 * @brief What a client received and sent, and its files.
 * @{
 */

/**
 * @brief The stats of a client, of all of its connections.
 */
struct _ClientStats {

	/**
	 * @brief How often the stats should be reset.
	 *
	 * @warning It is only stored and printed: cerver does not reset the stats with it.
	 * @deprecated Only the stats of the cerver are reset (CerverStats::threshold_time); it goes away in the 3.0.
	 */
	time_t threshold_time;

	/** @brief The calls to recv (), updated with __atomic. */
	u64 n_receives_done;

	/** @brief The bytes received from the client, updated with __atomic. */
	u64 total_bytes_received;
	/** @brief The bytes sent to the client, by all of its connections. */
	u64 total_bytes_sent;

	/** @brief The packets received from the client (packet header and data). */
	u64 n_packets_received;
	/** @brief The packets sent to the client, by all of its connections. */
	u64 n_packets_sent;

	/** @brief The packets received by type, it points to #received_packets_storage. */
	struct _PacketsPerType *received_packets;
	/** @brief The packets sent by type, it points to #sent_packets_storage. */
	struct _PacketsPerType *sent_packets;

	/** @brief The storage of #received_packets, so the stats of a client are not extra allocations. */
	struct _PacketsPerType received_packets_storage;
	/** @brief The storage of #sent_packets. */
	struct _PacketsPerType sent_packets_storage;

};

/** @brief The stats of a client, see struct _ClientStats. */
typedef struct _ClientStats ClientStats;

/**
 * @brief Prints the received and the sent packets of the client by type.
 *
 * @param[in] client the client, a NULL one or one without stats logs it
 */
CERVER_PUBLIC void client_stats_print (
	struct _Client *client
);

/**
 * @brief The stats of the files of a client.
 */
struct _ClientFileStats {

	/** @brief The requests to get a file. */
	u64 n_files_requests;
	/** @brief The requests to get a file that were fulfilled. */
	u64 n_success_files_requests;
	/** @brief The bad requests to get a file. */
	u64 n_bad_files_requests;
	/** @brief The files sent. */
	u64 n_files_sent;
	/** @brief The files that failed to be sent. */
	u64 n_bad_files_sent;
	/** @brief The bytes of the files sent. */
	u64 n_bytes_sent;

	/** @brief The requests to upload a file. */
	u64 n_files_upload_requests;
	/** @brief The files received. */
	u64 n_success_files_uploaded;
	/** @brief The bad requests to upload a file. */
	u64 n_bad_files_upload_requests;
	/** @brief The files that failed to be received. */
	u64 n_bad_files_received;
	/** @brief The bytes of the files received. */
	u64 n_bytes_received;

};

/** @brief The stats of the files of a client, see struct _ClientFileStats. */
typedef struct _ClientFileStats ClientFileStats;

/**
 * @brief Prints the stats of the files of the client.
 *
 * @param[in] client the client, a NULL one or one without file stats prints nothing
 */
CERVER_PUBLIC void client_file_stats_print (
	struct _Client *client
);

/** @} */

/**
 * @defgroup client_main Client
 * @ingroup client
 * @brief The struct of a client, its setters and how it is registered to a cerver.
 *
 * The setters ignore a NULL client.
 * @{
 */

/** @brief The size of the array of the events of a client, the ClientEventType values must be below it. */
#define CLIENT_MAX_EVENTS				32

/** @brief The size of the array of the errors of a client, the ClientErrorType values must be below it. */
#define CLIENT_MAX_ERRORS				32

/**
 * @brief The X-macro table of what happened with the connections of a client after one was removed: the number,
 * the name, the string and the description of each status.
 */
#define CLIENT_CONNECTIONS_STATUS_MAP(XX)									\
	XX(0,	NONE,		None, 		Undefined)								\
	XX(1,	ERROR,		Error, 		Failed to remove connection)			\
	XX(2,	ONE,		One,		At least one active connection)			\
	XX(3,	DROPPED,	Dropped,	Removed due to no connections left)

/**
 * @brief The status of the connections of a client, one `CLIENT_CONNECTIONS_STATUS_<name>` for each row of
 * #CLIENT_CONNECTIONS_STATUS_MAP.
 */
typedef enum ClientConnectionsStatus {

	#define XX(num, name, string, description) CLIENT_CONNECTIONS_STATUS_##name = num,
	CLIENT_CONNECTIONS_STATUS_MAP (XX)
	#undef XX

} ClientConnectionsStatus;

/**
 * @brief Returns the string of a status.
 *
 * @param[in] status the status
 * @return the string of its row in #CLIENT_CONNECTIONS_STATUS_MAP, or the one of CLIENT_CONNECTIONS_STATUS_NONE
 * for a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *client_connections_status_to_string (
	const ClientConnectionsStatus status
);

/**
 * @brief Returns the description of a status.
 *
 * @param[in] status the status
 * @return the description of its row in #CLIENT_CONNECTIONS_STATUS_MAP, or the one of
 * CLIENT_CONNECTIONS_STATUS_NONE for a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *client_connections_status_description (
	const ClientConnectionsStatus status
);

/**
 * @brief A client: anyone that connects to a cerver.
 *
 * The fields are read by the parts of cerver; an application sets them with the setters.
 */
struct _Client {

	/** @brief The id of the client, unique in the process. */
	u64 id;
	/** @brief When the client was created. */
	time_t connected_timestamp;

	/** @brief The name of the client, that also names the update threads of its connections. */
	char name[CLIENT_NAME_SIZE];

	/** @brief The connections of the client, that the client owns. */
	DoubleList *connections;

	/**
	 * @brief The session id that joins many connections into the same client.
	 *
	 * It is set and read with #session_lock, use client_get_session_id ().
	 */
	String *session_id;
	/** @brief Protects #session_id. */
	pthread_mutex_t session_lock;

	/** @brief The last time the client sent or received data. */
	time_t last_activity;

	/** @brief The client failed to authenticate. */
	bool drop_client;

	/** @brief The data of the application. */
	void *data;
	/** @brief Releases #data, free () when it is NULL. */
	Action delete_data;

	/** @brief The client is running, when it connects to a cerver; set and read with __atomic. */
	bool running;
	/** @brief When the client started. */
	time_t time_started;
	/** @brief The seconds the client has been up. */
	u64 uptime;

	/** @brief The handlers that are alive (ready to handle packets). */
	unsigned int num_handlers_alive;
	/** @brief The handlers that are handling a packet. */
	unsigned int num_handlers_working;
	/** @brief Protects the counts of the handlers. */
	pthread_mutex_t *handlers_lock;
	/** @brief The handler of the PACKET_TYPE_APP packets. */
	struct _Handler *app_packet_handler;
	/** @brief The handler of the PACKET_TYPE_APP_ERROR packets. */
	struct _Handler *app_error_packet_handler;
	/** @brief The handler of the PACKET_TYPE_CUSTOM packets. */
	struct _Handler *custom_packet_handler;

	/** @brief The size in bytes of the biggest packet that the client handles. */
	size_t max_received_packet_size;

	/** @brief The packets are checked with packet_check (). */
	bool check_packets;
	/** @brief The bad packets that a connection can receive before it is ended, 0 disables it. */
	u8 max_bad_packets;

	/**
	 * @brief The general lock of the client.
	 *
	 * It also protects the list of the connections against the update threads that remove their connections.
	 */
	pthread_mutex_t *lock;

	/** @brief The update threads of the connections that are alive, client_teardown () waits for them. */
	unsigned int n_update_threads;
	/** @brief Signaled with #lock when an update or an async thread ends, or a connection is removed. */
	pthread_cond_t updates_cond;

	/** @brief The async connect and request threads that use the client, client_teardown () waits for them. */
	unsigned int n_async_threads;

	/** @brief Set by client_teardown (), it cancels the connect backoff of the connections. */
	bool teardown_requested;

	/** @brief The threads of the events and errors actions, client_teardown () waits for them. */
	ThreadGroup event_threads;

	/** @brief The events that the application registered, by type. */
	struct _ClientEvent *events[CLIENT_MAX_EVENTS];
	/** @brief The errors that the application registered, by type. */
	struct _ClientError *errors[CLIENT_MAX_ERRORS];

	/** @brief The number of #paths. */
	unsigned int n_paths;
	/** @brief The paths where the files that a cerver requests are searched. */
	String *paths[CLIENT_FILES_MAX_PATHS];

	/** @brief The path where the received files are placed. */
	String *uploads_path;

	/** @brief Receives a file that the cerver sends, client_file_receive () by default. */
	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	);

	/** @brief Runs after a file was received. */
	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	);

	/** @brief The stats of the files, it points to #file_stats_storage. */
	ClientFileStats *file_stats;

	/** @brief The stats of the client, it points to #stats_storage. */
	ClientStats *stats;

	/** @brief The storage of #file_stats, kept while the client is recycled. */
	ClientFileStats file_stats_storage;
	/** @brief The storage of #stats, kept while the client is recycled. */
	ClientStats stats_storage;
	/** @brief The storage of #lock, kept while the client is recycled. */
	pthread_mutex_t lock_storage;

	/** @brief The next deleted client that waits to be reused. */
	struct _Client *recycle_next;

	/**
	 * @brief The TLS of the TCP connections of the client, NULL without it (see client_set_tls ()).
	 *
	 * @since 2.1b-22
	 */
	struct _ClientTls *tls;

};

/** @brief A client, see struct _Client. */
typedef struct _Client Client;

/**
 * @brief Allocates a client with the default values, without its connections list, lock or stats.
 *
 * It reuses a deleted client when there is one.
 *
 * @return the new client, NULL if it could not be allocated
 */
CERVER_PUBLIC Client *client_new (void);

/**
 * @brief Deletes a client and all of its data: its session id, its data, its handlers, its events, its errors and
 * its paths.
 *
 * The client is kept to be reused, up to #CLIENT_RECYCLE_LIMIT.
 *
 * @param[in] ptr the client, it can be NULL
 */
CERVER_PUBLIC void client_delete (void *ptr);

/**
 * @brief Does nothing: the delete method of the structures that must not release the client.
 *
 * @param[in] ptr ignored
 */
CERVER_PUBLIC void client_delete_dummy (void *ptr);

/**
 * @brief Creates a client with a new id, the default name, its connections list, its lock and its stats.
 *
 * @return the new client, released with client_teardown () or client_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Client *client_create (void);

/**
 * @brief Creates a client with the connection of an accepted socket.
 *
 * @param[in] cerver the cerver, its protocol is the one of the connection
 * @param[in] sock_fd the accepted socket
 * @param[in] address the address of the peer
 * @return the new client, NULL if the client or its connection could not be created
 */
CERVER_PUBLIC Client *client_create_with_connection (
	struct _Cerver *cerver,
	const i32 sock_fd, const struct sockaddr_storage *address
);

/**
 * @brief Sets the name of the client.
 *
 * @param[in,out] client the client
 * @param[in] name the name, truncated to CLIENT_NAME_SIZE - 1 characters; NULL keeps the current one
 */
CERVER_EXPORT void client_set_name (
	Client *client, const char *name
);

/**
 * @brief Sets the session id of the client.
 *
 * @param[in,out] client the client
 * @param[in] session_id the session id, copied; NULL removes it
 * @retval 0 the session id was set
 * @retval 1 the client is NULL
 * @threadsafety Safe from any thread while the client exists, it uses the session lock.
 */
CERVER_PUBLIC u8 client_set_session_id (
	Client *client, const char *session_id
);

/**
 * @brief Copies the session id of the client, cut to size - 1 bytes and NUL terminated.
 *
 * @param[in] client the client
 * @param[out] session_id the buffer
 * @param[in] size the size of the buffer
 * @return the bytes copied, 0 if the client does not have a session id or for a NULL argument
 * @threadsafety Safe from any thread while the client exists, it uses the session lock.
 */
CERVER_EXPORT size_t client_get_session_id (
	const Client *client, char *session_id, const size_t size
);

/**
 * @brief Returns the data of the application of the client.
 *
 * @param[in] client the client
 * @return the data, NULL for a NULL client
 */
CERVER_EXPORT void *client_get_data (Client *client);

/**
 * @brief Sets the data of the client and how to release it; the previous data is released first.
 *
 * @param[in,out] client the client
 * @param[in] data the data, the client owns it from now on
 * @param[in] delete_data releases the data, NULL to release it with free ()
 */
CERVER_EXPORT void client_set_data (
	Client *client, void *data, Action delete_data
);

/**
 * @brief Sets the handlers of the PACKET_TYPE_APP and PACKET_TYPE_APP_ERROR packets.
 *
 * @param[in,out] client the client
 * @param[in] app_handler the handler of the PACKET_TYPE_APP packets, the client owns it from now on; it can be NULL
 * @param[in] app_error_handler the handler of the PACKET_TYPE_APP_ERROR packets, the client owns it from now on; it
 * can be NULL
 * @note A different handler deletes the one that was already set; the same handler again is kept. Set them
 * before the client connects.
 */
CERVER_EXPORT void client_set_app_handlers (
	Client *client,
	struct _Handler *app_handler, struct _Handler *app_error_handler
);

/**
 * @brief Sets the handler of the PACKET_TYPE_CUSTOM packets.
 *
 * @param[in,out] client the client
 * @param[in] custom_handler the handler, the client owns it from now on; it can be NULL
 * @note A different handler deletes the one that was already set; the same handler again is kept. Set them
 * before the client connects.
 */
CERVER_EXPORT void client_set_custom_handler (
	Client *client, struct _Handler *custom_handler
);

/**
 * @brief Sets the biggest packet that the client handles; a bigger one is a bad packet.
 *
 * @param[in,out] client the client
 * @param[in] max_received_packet_size the size in bytes, the default is #CLIENT_DEFAULT_MAX_RECEIVED_PACKET_SIZE
 */
CERVER_EXPORT void client_set_max_received_packet_size (
	Client *client, size_t max_received_packet_size
);

/**
 * @brief Sets whether the client checks the protocol id and the version of the header of each packet
 * (packet_check ()); a packet that fails is not handled and is destroyed right away.
 *
 * The size of the packets is checked by the handlers.
 *
 * @param[in,out] client the client
 * @param[in] check_packets true to check them; by default they are not checked
 */
CERVER_EXPORT void client_set_check_packets (
	Client *client, bool check_packets
);

/**
 * @brief Sets how many bad packets a connection can receive before the client ends it.
 *
 * A bad packet fails packet_check () or has an unknown type.
 *
 * @param[in,out] client the client
 * @param[in] max_bad_packets the packets, 0 disables the limit; the default is #CLIENT_DEFAULT_MAX_BAD_PACKETS
 */
CERVER_EXPORT void client_set_max_bad_packets (
	Client *client, u8 max_bad_packets
);

/**
 * @brief Compares two clients by their ids.
 *
 * @param[in] a the first client
 * @param[in] b the second client
 * @return -1, 0 or 1 as the id of a is smaller, equal or bigger than the one of b; 0 when a client is NULL
 */
CERVER_PUBLIC int client_comparator_client_id (
	const void *a, const void *b
);

/**
 * @brief Compares two clients by their session ids, with str_compare ().
 *
 * @param[in] a the first client
 * @param[in] b the second client
 * @return the result of str_compare (); -1 when only b is NULL, 1 when only a is NULL, 0 when both are NULL
 */
CERVER_PUBLIC int client_comparator_session_id (
	const void *a, const void *b
);

/**
 * @brief Closes all the connections of the client.
 *
 * @param[in,out] client the client
 * @retval 0 the connections were closed
 * @retval 1 the client is NULL
 */
CERVER_EXPORT u8 client_disconnect (Client *client);

/**
 * @brief The client got disconnected from the cerver: it closes its connections and stops running.
 *
 * @param[in,out] client the client
 */
CERVER_EXPORT void client_got_disconnected (Client *client);

/**
 * @brief Drops a client from the cerver: it unregisters it from the cerver and deletes it.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client, invalid after the call; a NULL argument does nothing
 */
CERVER_EXPORT void client_drop (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Adds a connection to the end of the connections of the client, without adding it to any other structure.
 *
 * @param[in,out] client the client, it owns the connection from now on
 * @param[in] connection the connection
 * @retval 0 the connection was added
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT u8 client_connection_add (
	Client *client, struct _Connection *connection
);

/**
 * @brief Removes the connection from the client, without deleting it.
 *
 * @param[in,out] client the client
 * @param[in] connection the connection
 * @retval 0 the connection was removed
 * @retval 1 a NULL argument or the client does not have it
 */
CERVER_EXPORT u8 client_connection_remove (
	Client *client, struct _Connection *connection
);

/**
 * @brief Removes the connection from the client, ends it and deletes it.
 *
 * @param[in] cerver the cerver
 * @param[in,out] client the client
 * @param[in] connection the connection, invalid after a successful call
 * @retval 0 the connection was dropped
 * @retval 1 a NULL argument or the client does not have it
 */
CERVER_EXPORT u8 client_connection_drop (
	struct _Cerver *cerver,
	Client *client, struct _Connection *connection
);

/**
 * @brief Removes the connection of a socket from the client and from the structures of the cerver, and drops the
 * client when it has no connections left.
 *
 * @param[in,out] cerver the cerver
 * @param[in,out] client the client, invalid when the status is CLIENT_CONNECTIONS_STATUS_DROPPED and it had one
 * connection or none
 * @param[in] sock_fd the fd of the socket of the connection
 * @return CLIENT_CONNECTIONS_STATUS_DROPPED when the connection (or the client) was dropped,
 * CLIENT_CONNECTIONS_STATUS_ERROR on error or when the connection does not belong to the client
 */
CERVER_PUBLIC ClientConnectionsStatus client_remove_connection_with_sock_fd (
	struct _Cerver *cerver,
	Client *client, const i32 sock_fd
);

/**
 * @brief Maps every connection of the client to it in the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @retval 0 at least one connection was registered
 * @retval 1 a NULL argument or every connection failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 client_register_connections_to_cerver (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Removes every connection of the client from the map of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @retval 0 at least one connection was unregistered, or the client has none
 * @retval 1 a NULL argument or every connection failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 client_unregister_connections_from_cerver (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Adds every connection of the client to the poll of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @retval 0 at least one connection was registered
 * @retval 1 a NULL argument or every connection failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 client_register_connections_to_cerver_poll (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Removes every connection of the client from the poll of the cerver.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @retval 0 at least one connection was unregistered
 * @retval 1 a NULL argument or every connection failed; the client is not dropped, its caller decides
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 client_unregister_connections_from_cerver_poll (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Removes the client from the clients of the cerver, without its connections.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @return the client that was removed, NULL if the cerver did not have it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Client *client_remove_from_cerver (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Registers a client to the cerver: its connections, its poll (with CERVER_HANDLER_TYPE_POLL) and its
 * clients.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @retval 0 the client was registered
 * @retval 1 on error, the client is not registered and the caller releases it with its connections
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 client_register_to_cerver (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Unregisters a client from a cerver: its connections from the map and the poll, and the client from the
 * clients.
 *
 * @param[in,out] cerver the cerver
 * @param[in] client the client
 * @return the client that was removed, NULL if the cerver did not have it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Client *client_unregister_from_cerver (
	struct _Cerver *cerver, Client *client
);

/**
 * @brief Gets the client of a socket, with the map of the cerver.
 *
 * @param[in] cerver the cerver
 * @param[in] sock_fd the fd of the socket
 * @return the client, NULL if the cerver does not have it
 */
CERVER_PUBLIC Client *client_get_with_sock_fd (
	struct _Cerver *cerver, i32 sock_fd
);

/**
 * @brief Searches the clients of the cerver for the one of a session id.
 *
 * @param[in] cerver the cerver, it must support sessions
 * @param[in] session_id the session id
 * @return the client, NULL if the cerver does not have it or for a NULL argument
 */
CERVER_PUBLIC Client *client_get_with_session_id (
	struct _Cerver *cerver, const char *session_id
);

/**
 * @brief Sends a packet to every connection of every client of a tree of the cerver.
 *
 * @param[in] node the root of the tree, like the root of the clients of the cerver
 * @param[in] cerver the cerver
 * @param[in,out] packet the packet, its network values are set for each connection; the caller keeps it
 */
CERVER_PUBLIC void client_broadcast_to_all_avl (
	AVLNode *node,
	struct _Cerver *cerver,
	struct _Packet *packet
);

/** @} */

/**
 * @defgroup client_connections Connections
 * @ingroup client
 * @brief The connections of a client that connects to a cerver.
 * @{
 */

/**
 * @brief A client and one of its connections, what the threads of a connection get.
 */
typedef struct ClientConnection {

	/** @brief The thread that uses the connection. */
	pthread_t connection_thread_id;
	/** @brief The client. */
	struct _Client *client;
	/** @brief The connection. */
	struct _Connection *connection;

} ClientConnection;

/**
 * @brief Releases a ClientConnection, not its client or its connection.
 *
 * @param[in] cc the ClientConnection, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_connection_aux_delete (
	ClientConnection *cc
);

/**
 * @brief Returns whether the client is running.
 *
 * @param[in] client the client
 * @return true while the client is running, false for a NULL client
 * @threadsafety Safe from any thread, it reads the flag with __atomic (its update threads read it).
 */
CERVER_PUBLIC bool client_is_running (const Client *client);

/**
 * @brief Returns whether client_teardown () was called, so the connections stop their connect backoff.
 *
 * @param[in] client the client
 * @return true after client_teardown (), false for a NULL client
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool client_teardown_is_requested (const Client *client);

/**
 * @brief Completes the end of a connection that its update thread owns, called by the update thread when its
 * receive loop has ended.
 *
 * @param[in,out] client the client
 * @param[in] connection the connection, deleted by the call
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_connection_end_deferred (
	Client *client, struct _Connection *connection
);

/**
 * @brief Counts an update thread of the client that is about to be created, or one that failed to be created.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection, its updating value is set to started
 * @param[in] started true before the thread is created, false when it failed to be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_update_thread_started (
	Client *client, struct _Connection *connection, const bool started
);

/**
 * @brief An update thread of the client has ended and it does not use the client anymore.
 *
 * @param[in,out] client the client
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void client_update_thread_ended (Client *client);

/**
 * @brief Creates a connection that is ready to connect to a cerver and registers it to the client.
 *
 * @param[in,out] client the client, it owns the connection from now on
 * @param[in] ip_address the ip or the name of the cerver
 * @param[in] port the port of the cerver
 * @param[in] protocol the protocol
 * @param[in] use_ipv6 true to use IPv6
 * @return the new connection, NULL for a NULL client or if it could not be created
 */
CERVER_EXPORT struct _Connection *client_connection_create (
	Client *client,
	const char *ip_address, u16 port,
	Protocol protocol, bool use_ipv6
);

/**
 * @brief Registers an existing connection to a client.
 *
 * @param[in,out] client the client, it owns the connection from now on
 * @param[in] connection the connection
 * @retval 0 the connection was registered
 * @retval 1 a NULL argument or it could not be added
 */
CERVER_EXPORT int client_connection_register (
	Client *client, struct _Connection *connection
);

/**
 * @brief Unregisters a connection from the client, without deleting it.
 *
 * @param[in,out] client the client
 * @param[in] connection the connection
 * @retval 0 the connection was unregistered
 * @retval 1 a NULL argument or the connection does not belong to the client
 */
CERVER_EXPORT int client_connection_unregister (
	Client *client, struct _Connection *connection
);

/**
 * @brief Receives a complete packet (or a datagram) in the socket of the connection and handles it.
 *
 * It blocks until the packet arrives, reading it in pieces of #CLIENT_NEXT_PACKET_BUFFER_SIZE.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @retval 0 a packet was handled
 * @retval 1 a NULL argument, the receive failed, or the header or the size of the packet are not valid, which
 * closes the connection
 */
CERVER_PUBLIC unsigned int client_connection_get_next_packet (
	Client *client, struct _Connection *connection
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
