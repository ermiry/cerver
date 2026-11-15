/**
 * @file cerver/admin/admin.h
 * @brief The admins of a cerver: the defaults of the admin cerver and the Admin, with its lookups and its sends.
 */

#ifndef _CERVER_ADMIN_ADMIN_H_
#define _CERVER_ADMIN_ADMIN_H_

#include <stdbool.h>

#include <poll.h>
#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/packets/packets.h"

/**
 * @defgroup admin Admin
 * @brief The admin cerver: the connections of the admins of a cerver, authenticated with their own method and
 * handled by their own poll and handlers.
 *
 * A cerver enables it with cerver_set_admin_enable (), and it is started and ended with the cerver.
 */

/**
 * @defgroup admin_defaults Defaults
 * @ingroup admin
 * @brief The values that a new admin cerver starts with.
 * @{
 */

/** @brief The default max admins at the same time. */
#define ADMIN_CERVER_DEFAULT_MAX_ADMINS					1

/** @brief The default max connections of each admin. */
#define ADMIN_CERVER_DEFAULT_MAX_ADMIN_CONNECTIONS		2

/** @brief The default bad packets that an admin connection can send before it is dropped. */
#define ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS			4

/** @brief The default size in bytes of the buffer of the admin poll. */
#define ADMIN_CERVER_DEFAULT_RECEIVE_BUFFER_SIZE		4096

/** @brief The default size of the array of the admin poll. */
#define ADMIN_CERVER_DEFAULT_POLL_FDS					4

/** @brief The default timeout in milliseconds of the admin poll. */
#define ADMIN_CERVER_DEFAULT_POLL_TIMEOUT				2000

/** @brief The default of admin_cerver_set_check_packets (). */
#define ADMIN_CERVER_DEFAULT_CHECK_PACKETS				false

/** @brief The default ticks per second of the update method of the admin cerver. */
#define ADMIN_CERVER_DEFAULT_UPDATE_TICKS				30

/** @brief The default seconds between two calls of the update interval method of the admin cerver. */
#define ADMIN_CERVER_DEFAULT_UPDATE_INTERVAL_SECONDS	1

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Handler;
struct _Packet;

struct _AdminCerver;

/**
 * @defgroup admin_admin Admin
 * @ingroup admin
 * @brief An admin: an authenticated client of the admin cerver.
 * @{
 */

/**
 * @brief The X-macro table of what happened when a connection of an admin was removed: the number, the name, the
 * string and the description of each status.
 */
#define ADMIN_CONNECTIONS_STATUS_MAP(XX)									\
	XX(0,	NONE,		None, 		Undefined)								\
	XX(1,	ERROR,		Error, 		Failed to remove connection)			\
	XX(2,	ONE,		One,		At least one active connection)			\
	XX(3,	DROPPED,	Dropped,	Removed due to no connections left)

/**
 * @brief What happened when a connection of an admin was removed, one `ADMIN_CONNECTIONS_STATUS_<name>` for each
 * row of #ADMIN_CONNECTIONS_STATUS_MAP.
 */
typedef enum AdminConnectionsStatus {

	#define XX(num, name, string, description) ADMIN_CONNECTIONS_STATUS_##name = num,
	ADMIN_CONNECTIONS_STATUS_MAP (XX)
	#undef XX

} AdminConnectionsStatus;

/**
 * @brief Returns the string of a status.
 *
 * @param[in] status the status
 * @return the string of its row in #ADMIN_CONNECTIONS_STATUS_MAP, or the one of ADMIN_CONNECTIONS_STATUS_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *admin_connections_status_to_string (
	const AdminConnectionsStatus status
);

/**
 * @brief Returns the description of a status.
 *
 * @param[in] status the status
 * @return the description of its row in #ADMIN_CONNECTIONS_STATUS_MAP, or the one of ADMIN_CONNECTIONS_STATUS_NONE
 * for a value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *admin_connections_status_description (
	const AdminConnectionsStatus status
);

/**
 * @brief An admin of a cerver.
 */
struct _Admin {

	/** @brief The admin cerver that the admin is registered to, NULL when it is not. */
	struct _AdminCerver *admin_cerver;

	/** @brief The identifier of the admin, the time it was created and a random number. */
	String *id;

	/** @brief The client with the connections of the admin, the admin owns it. */
	struct _Client *client;

	/** @brief The data of the application for the admin. */
	void *data;
	/** @brief Releases #data, NULL to keep it. */
	Action delete_data;

	/** @brief The bad packets of the admin. */
	u32 bad_packets;

};

/** @brief An admin, see struct _Admin. */
typedef struct _Admin Admin;

/**
 * @brief Allocates an empty admin.
 *
 * @return the new admin, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Admin *admin_new (void);

/**
 * @brief Releases an admin, its client and its data (with its delete_data).
 *
 * @param[in] admin_ptr the admin, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void admin_delete (void *admin_ptr);

/**
 * @brief Creates an admin with its id.
 *
 * @return the new admin, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Admin *admin_create (void);

/**
 * @brief Creates an admin with its id for a client.
 *
 * @param[in] client the client, the admin owns it from now on
 * @return the new admin, NULL for a NULL client or if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Admin *admin_create_with_client (
	struct _Client *client
);

/**
 * @brief Compares two admins by their id, for the list of the admin cerver.
 *
 * @param[in] a the first admin
 * @param[in] b the second admin
 * @return the result of strcmp () on their ids; a NULL admin goes after the other one
 */
CERVER_PUBLIC int admin_comparator_with_id (
	const void *a, const void *b
);

/**
 * @brief Sets the data of the application for an admin, and how to release it.
 *
 * @param[in,out] admin the admin
 * @param[in] data the data
 * @param[in] delete_data releases the data with the admin, NULL to never release it
 */
CERVER_PUBLIC void admin_set_data (
	Admin *admin, void *data, Action delete_data
);

/**
 * @brief Returns the admin that has a connection with a socket.
 *
 * @param[in] admin_cerver the admin cerver
 * @param[in] sock_fd the socket
 * @return the admin, NULL if none has it
 * @threadsafety It takes the lock of the list of the admins.
 */
CERVER_PUBLIC Admin *admin_get_with_sock_fd (
	struct _AdminCerver *admin_cerver, const i32 sock_fd
);

/**
 * @brief Returns the admin whose client has a session id.
 *
 * @param[in] admin_cerver the admin cerver
 * @param[in] session_id the session id
 * @return the admin, NULL for a NULL argument or if none has it
 * @threadsafety It takes the lock of the list of the admins.
 */
CERVER_PUBLIC Admin *admin_get_with_session_id (
	struct _AdminCerver *admin_cerver, const char *session_id
);

/**
 * @brief Removes the connection of an admin with a socket, and drops the admin when it has no connections left.
 *
 * @param[in,out] admin_cerver the admin cerver
 * @param[in,out] admin the admin, it is deleted when it is dropped
 * @param[in] sock_fd the socket
 * @retval ADMIN_CONNECTIONS_STATUS_NONE the connection was removed and the admin has others
 * @retval ADMIN_CONNECTIONS_STATUS_DROPPED the admin had no other connection, it was unregistered and deleted
 * @retval ADMIN_CONNECTIONS_STATUS_ERROR a NULL argument, or the connection was not found or not removed
 * @note ADMIN_CONNECTIONS_STATUS_ONE is never returned.
 */
CERVER_PUBLIC AdminConnectionsStatus admin_remove_connection_with_sock_fd (
	struct _AdminCerver *admin_cerver,
	Admin *admin, const i32 sock_fd
);

/**
 * @brief Sends a packet to the first connection of an admin, and counts it in the stats of the admin cerver.
 *
 * @param[in] admin the admin, registered to its admin cerver with at least one connection
 * @param[in,out] packet the packet, its network values are set to the admin
 * @retval 0 the packet was sent
 * @retval 1 a NULL argument or the packet was not sent
 */
CERVER_PUBLIC u8 admin_send_packet (
	Admin *admin, struct _Packet *packet
);

/**
 * @brief Sends a packet to the first connection of an admin with packet_send_to_split ().
 *
 * @param[in] admin the admin, registered to its admin cerver with at least one connection
 * @param[in] packet the packet
 * @retval 0 the packet was sent
 * @retval 1 a NULL argument or the packet was not sent
 */
CERVER_PUBLIC u8 admin_send_packet_split (
	Admin *admin, struct _Packet *packet
);

/**
 * @brief Sends a packet in pieces to the first connection of an admin with packet_send_pieces ().
 *
 * @param[in] admin the admin, registered to its admin cerver with at least one connection
 * @param[in,out] packet the packet, its network values are set to the admin
 * @param[in] pieces the pieces
 * @param[in] sizes the size in bytes of each piece
 * @param[in] n_pieces the number of pieces
 * @retval 0 the packet was sent
 * @retval 1 a NULL argument or the packet was not sent
 */
CERVER_PUBLIC u8 admin_send_packet_pieces (
	Admin *admin, struct _Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
