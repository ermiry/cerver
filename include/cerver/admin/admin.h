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

#define ADMIN_CERVER_DEFAULT_MAX_ADMINS					1
#define ADMIN_CERVER_DEFAULT_MAX_ADMIN_CONNECTIONS		2
#define ADMIN_CERVER_DEFAULT_MAX_BAD_PACKETS			4

#define ADMIN_CERVER_DEFAULT_RECEIVE_BUFFER_SIZE		4096

#define ADMIN_CERVER_DEFAULT_POLL_FDS					4
#define ADMIN_CERVER_DEFAULT_POLL_TIMEOUT				2000

#define ADMIN_CERVER_DEFAULT_CHECK_PACKETS				false

#define ADMIN_CERVER_DEFAULT_UPDATE_TICKS				30
#define ADMIN_CERVER_DEFAULT_UPDATE_INTERVAL_SECONDS	1

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Handler;
struct _Packet;

struct _AdminCerver;

#pragma region admin

#define ADMIN_CONNECTIONS_STATUS_MAP(XX)									\
	XX(0,	NONE,		None, 		Undefined)								\
	XX(1,	ERROR,		Error, 		Failed to remove connection)			\
	XX(2,	ONE,		One,		At least one active connection)			\
	XX(3,	DROPPED,	Dropped,	Removed due to no connections left)

typedef enum AdminConnectionsStatus {

	#define XX(num, name, string, description) ADMIN_CONNECTIONS_STATUS_##name = num,
	ADMIN_CONNECTIONS_STATUS_MAP (XX)
	#undef XX

} AdminConnectionsStatus;

CERVER_PUBLIC const char *admin_connections_status_to_string (
	const AdminConnectionsStatus status
);

CERVER_PUBLIC const char *admin_connections_status_description (
	const AdminConnectionsStatus status
);

struct _Admin {

	struct _AdminCerver *admin_cerver;

	String *id;					// unique admin identifier

	struct _Client *client;		// network values for the admin

	// a place to store dedicated admin data
	void *data;
	Action delete_data;

	u32 bad_packets;			// disconnect after a number of bad packets

};

typedef struct _Admin Admin;

CERVER_PRIVATE Admin *admin_new (void);

CERVER_PRIVATE void admin_delete (void *admin_ptr);

CERVER_PRIVATE Admin *admin_create (void);

CERVER_PRIVATE Admin *admin_create_with_client (
	struct _Client *client
);

CERVER_PUBLIC int admin_comparator_with_id (
	const void *a, const void *b
);

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
CERVER_PUBLIC void admin_set_data (
	Admin *admin, void *data, Action delete_data
);

// gets an admin by a matching connection in its client with the specified sock fd
CERVER_PUBLIC Admin *admin_get_with_sock_fd (
	struct _AdminCerver *admin_cerver, const i32 sock_fd
);

// gets an admin by a matching client's session id
CERVER_PUBLIC Admin *admin_get_with_session_id (
	struct _AdminCerver *admin_cerver, const char *session_id
);

// removes the connection from the admin referred to by the sock fd
// and also checks if there is another active connection in the admin, if not it will be dropped
// returns 0 on success, 1 on error
CERVER_PUBLIC AdminConnectionsStatus admin_remove_connection_with_sock_fd (
	struct _AdminCerver *admin_cerver,
	Admin *admin, const i32 sock_fd
);

// sends a packet to the first connection of the specified admin
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 admin_send_packet (
	Admin *admin, struct _Packet *packet
);

// sends a packet to the first connection of the specified admin using packet_send_to_split ()
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 admin_send_packet_split (
	Admin *admin, struct _Packet *packet
);

// sends a packet in pieces to the first connection of the specified admin
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 admin_send_packet_pieces (
	Admin *admin, struct _Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
