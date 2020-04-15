#ifndef _CERVER_ADMIN_H_
#define _CERVER_ADMIN_H_

#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/dllist.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/packets.h"

struct _Cerver;
struct _Client;
struct _Packet;
struct _PacketsPerType;

struct _AdminCerver;

// 24/01/2020 -- using only hardcoded admin credentials
struct AdminCredentials {

	estring *username;
	estring *password;

	bool logged_in;

};

typedef struct AdminCredentials AdminCredentials;

// serialized admin credentials
struct _SAdminCredentials {

	SStringM username;
	SStringM password;

};

typedef struct _SAdminCredentials SAdminCredentials;

// the data of an actual admin connected to the cerver
struct _Admin {

	estring *id;						// unique admin identifier

	struct _Client *client;				// network values for the admin

	// a place to store dedicated admin data
	void *data;
	Action delete_data;

	bool authenticated;
	AdminCredentials *credentials;

	u32 bad_packets;					// 23/01/2020 -- 17:41 -- disconnect after a number of bad packets

};

typedef struct _Admin Admin;

extern int admin_comparator_by_id (const void *a, const void *b);

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
extern void admin_set_data (Admin *admin, void *data, Action delete_data);

// sends a packet to the specified admin
// returns 0 on success, 1 on error
extern u8 admin_send_packet (Admin *admin, struct _Packet *packet);

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
extern u8 admin_broadcast_to_all (struct _AdminCerver *admin_cerver, struct _Packet *packet);

/*** Admin Cerver ***/

// auxiliary structure that is passed to the custom app handlers
typedef struct AdminAppHandler {

	struct _AdminCerver *admin_cerver;		// the admin cerver where the packet arrived
	struct _Packet *packet;					// the actual packet to handle
	Admin *admin;							// the admin that made the request

} AdminAppHandler;

#define ADMIN_CERVER_CONNECTION_QUEUE				2

#define DEFAULT_MAX_ADMINS							1
#define DEFAULT_MAX_ADMIN_CONNECTIONS				1

#define DEFAULT_N_BAD_PACKETS_LIMIT					5
#define DEFAULT_N_BAD_PACKETS_LIMIT_AUTH			20

// 22/01/2020 -- added to meassure admin cerver connections
struct _AdminCerverStats {

	time_t threshold_time;                          // every time we want to reset cerver stats (like packets), defaults 24hrs
	
	u64 admin_n_packets_received;                  	// packets received from clients
	u64 admin_receives_done;                       	// receives done to clients
	u64 admin_bytes_received;                      	// bytes received from clients

	u64 total_n_receives_done;                      // total amount of actual calls to recv ()
	u64 total_n_packets_received;                   // total number of cerver packets received (packet header + data)
	u64 total_bytes_received;                       // total amount of bytes received in the cerver
	
	u64 total_n_packets_sent;                       // total number of packets that were sent
	u64 total_bytes_sent;                           // total amount of bytes sent by the cerver

	u64 current_active_admin_connections;          	// all of the current active connections for all current clients
	u64 current_n_connected_admins;                	// the current number of admins connected 

	u64 current_auth_active_admin_connections;      // all of the current auth active connections for all current clients
	u64 current_auth_n_connected_admins;            // the current number of auth admins connected 

	u64 total_n_admins;                            	// the total amount of clients that were registered to the cerver (no auth required)
	u64 unique_admins;                             	// n unique clients connected in a threshold time (check used authentication)
	u64 total_admin_connections;                   	// the total amount of client connections that have been done to the cerver

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

};

typedef struct _AdminCerverStats AdminCerverStats;

// 21/01/2020 -- dedicated admin structures to handle admin in a dedicated connection
// with a dedicated packet handler to keep them separated from normal clients
struct _AdminCerver {

	struct _Cerver *cerver;				// the cerver this belongs to

	i32 sock;                           // server socket
	struct sockaddr_storage address;

	u16 port; 
	bool use_ipv6;  
	u32 receive_buffer_size;

	bool running;

	DoubleList *credentials;			// 24/01/2020 -- using only hardcoded admin credentials

	DoubleList *admins;                 // connected admins 9auth or not

	u8 max_admins;						// the max numbers of admins allowed concurrently
	u8 max_admin_connections;			// the max number of connections allowed per admin

	// 23/01/2020 -- number of bad packets before ending connection
	u32 n_bad_packets_limit;
	u32 n_bad_packets_limit_auth;		// for auth admins we might want more tolerance

	struct pollfd *fds;
	u32 max_n_fds;                      // current max n fds in pollfd
	u16 current_n_fds;                  // n of active fds in the pollfd array
	u32 poll_timeout;

	// action to be performed when a new admin fail to authenticate
	Action on_admin_fail_connection;

	// action to be performed when a new admin authenticated successfully
	Action on_admin_success_connection;

	// custom admin packet hanlders
	// this handlers will only be accessible for authenticated admins
    Action admin_packet_handler;
    Action admin_error_packet_handler;

	struct _AdminCerverStats *stats;

};

typedef struct _AdminCerver AdminCerver;

// called only after success teardown, just for memory clean up
extern void admin_cerver_delete (void *admin_cerver_ptr);

extern AdminCerver *admin_cerver_create (u16 port, bool use_ipv6);

// sets a custom receive buffer size to use for admin handler
extern void admin_cerver_set_receive_buffer_size (AdminCerver *admin_cerver, u32 receive_buffer_size);

// 24/01/2020 -- 11:20 -- register new admin credentials
// returns 0 on success, 1 on error
extern u8 admin_cerver_register_admin_credentials (AdminCerver *admin_cerver,
	const char *username, const char *password);

// sets the max numbers of admins allowed concurrently
// by default, only ONE admin is allowed to be connected at any given time
extern void admin_cerver_set_max_admins (AdminCerver *admin_cerver, u8 max_admins);

// sets the max number of connections allowed per admin
// by default, onlye ONE connection is allowed per admin
extern void admin_cerver_set_max_admin_connections (AdminCerver *admin_cerver, u8 max_admin_connections);

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
extern void admin_cerver_set_bad_packets_limit (AdminCerver *admin_cerver, 
	i32 n_bad_packets_limit, i32 n_bad_packets_limit_auth);

// sets a custom poll time in ms out to use for admins
extern void admin_cerver_set_poll_timeout (AdminCerver *admin_cerver, u32 poll_timeout);

// 02/02/2020 -- aux structure that will be passed to the set
// on_admin_success_connection && on_admin_fail_connection
struct _OnAdminConnection {

	struct _AdminCerver *admin_cerver;
	struct _Admin *admin;

};

typedef struct _OnAdminConnection OnAdminConnection;

// sets and action to be performed when a new admin fail to authenticate
extern void admin_cerver_set_on_fail_connection (AdminCerver *admin_cerver, Action on_fail_connection);

// sets an action to be performed when a new admin authenticated successfully
// a struct _OnAdminConnection will be passed as the argument
extern void admin_cerver_set_on_success_connection (AdminCerver *admin_cerver, Action on_success_connection);

// sets customa admin packet handlers
extern void admin_cerver_set_handlers (AdminCerver *admin_cerver, 
	Action admin_packet_handler, Action admin_error_packet_handler);

// quick way to get the number of current authenticated admins
extern u32 admin_cerver_get_current_auth_admins (AdminCerver *admin_cerver);

/*** main ***/

// start the admin cerver, this is ment to be called in a dedicated thread internally
extern void *admin_cerver_start (void *args);

/*** teardown ***/

// correctly stops the admin cerver, disconnects admins and free any memory used by admin cerver
// returns 0 on success, 1 on error
extern u8 admin_cerver_teardown (AdminCerver *admin_cerver);

#endif