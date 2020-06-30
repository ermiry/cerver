#ifndef _CERVER_ADMIN_H_
#define _CERVER_ADMIN_H_

#include <stdbool.h>

#include <pthread.h>
#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/dllist.h"

#include "cerver/cerver.h"
#include "cerver/handler.h"
#include "cerver/packets.h"

struct _Cerver;
struct _Client;
struct _Handler;
struct _Packet;

#pragma region stats

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

extern void admin_cerver_stats_print (AdminCerverStats *stats);

#pragma endregion

#pragma region credentials

struct _AdminCredentials {

	estring *username;
	estring *password;

	bool logged_in;

};

typedef struct _AdminCredentials AdminCredentials;

extern AdminCredentials *admin_credentials_new (void);

extern void admin_credentials_delete (void *credentials_ptr);

#pragma endregion

#pragma region admin

struct _Admin {

	estring *id;						// unique admin identifier

	struct _Client *client;				// network values for the admin

	// a place to store dedicated admin data
	void *data;
	Action delete_data;

	bool authenticated;
	AdminCredentials *credentials;

	u32 bad_packets;					// disconnect after a number of bad packets

};

typedef struct _Admin Admin;

extern Admin *admin_new (void);

extern void admin_delete (void *admin_ptr);

extern Admin *admin_create_with_client (struct _Client *client);

extern int admin_comparator_by_id (const void *a, const void *b);

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
extern void admin_set_data (Admin *admin, void *data, Action delete_data);

// sends a packet to the first connection of the specified admin
// returns 0 on success, 1 on error
extern u8 admin_send_packet (Admin *admin, struct _Packet *packet);

#pragma endregion

#pragma region main

#define DEFAULT_MAX_ADMINS							1
#define DEFAULT_MAX_ADMIN_CONNECTIONS				1

#define DEFAULT_N_BAD_PACKETS_LIMIT					5
#define DEFAULT_N_BAD_PACKETS_LIMIT_AUTH			20

#define DEFAULT_ADMIN_MAX_N_FDS						10
#define DEFAULT_ADMIN_POLL_TIMEOUT					2000

struct _AdminCerver {

	struct _Cerver *cerver;				// the cerver this belongs to

	DoubleList *credentials;			// 29/06/2020 -- using only hardcoded admin credentials

	DoubleList *admins;					// connected admins to the cerver

	u8 max_admins;						// the max numbers of admins allowed at any time
	u8 max_admin_connections;			// the max number of connections allowed per admin

	// number of bad packets before ending connection
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

	// custom admin packet handlers
	// these handlers will only be accessible for authenticated admins
	struct _Handler *app_packet_handler;
	struct _Handler *app_error_packet_handler;
	struct _Handler *custom_packet_handler;

	unsigned int num_handlers_alive;       // handlers currently alive
    unsigned int num_handlers_working;     // handlers currently working
	pthread_mutex_t *handlers_lock;

	bool app_packet_handler_delete_packet;
	bool app_error_packet_handler_delete_packet;
	bool custom_packet_handler_delete_packet;

	struct _AdminCerverStats *stats;

};

typedef struct _AdminCerver AdminCerver;

extern AdminCerver *admin_cerver_new (void);

extern void admin_cerver_delete (AdminCerver *admin_cerver);

extern AdminCerver *admin_cerver_create (void);

// sets the max numbers of admins allowed at any given time
extern void admin_cerver_set_max_admins (AdminCerver *admin_cerver, u8 max_admins);

// sets the max number of connections allowed per admin
extern void admin_cerver_set_max_admin_connections (AdminCerver *admin_cerver, u8 max_admin_connections);

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
extern void admin_cerver_set_bad_packets_limit (AdminCerver *admin_cerver, 
	i32 n_bad_packets_limit, i32 n_bad_packets_limit_auth);

// sets the max number of poll fds for the admin cerver
extern void admin_cerver_set_max_fds (AdminCerver *admin_cerver, u32 max_n_fds);

// sets a custom poll time out to use for admins
extern void admin_cerver_set_poll_timeout (AdminCerver *admin_cerver, u32 poll_timeout);

// sets an action to be performed when a new admin failed to authenticate
extern void admin_cerver_set_on_fail_connection (AdminCerver *admin_cerver, Action on_fail_connection);

// sets an action to be performed when a new admin authenticated successfully
// a struct _OnAdminConnection will be passed as the argument
extern void admin_cerver_set_on_success_connection (AdminCerver *admin_cerver, Action on_success_connection);

// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
extern void admin_cerver_set_app_handlers (AdminCerver *admin_cerver, 
	struct _Handler *app_handler, struct _Handler *app_error_handler);

// sets option to automatically delete APP_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
extern void admin_cerver_set_app_handler_delete (AdminCerver *admin_cerver, bool delete_packet);

// sets option to automatically delete APP_ERROR_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
extern void admin_cerver_set_app_error_handler_delete (AdminCerver *admin_cerver, bool delete_packet);

// sets a CUSTOM_PACKET packet type handler
extern void admin_cerver_set_custom_handler (AdminCerver *admin_cerver, struct _Handler *custom_handler);

// sets option to automatically delete CUSTOM_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
extern void admin_cerver_set_custom_handler_delete (AdminCerver *admin_cerver, bool delete_packet);

// registers new admin credentials
// returns 0 on success, 1 on error
extern u8 admin_cerver_register_admin_credentials (AdminCerver *admin_cerver,
	const char *username, const char *password);

// removes a registered admin credentials
extern AdminCredentials *admin_cerver_unregister_admin_credentials (AdminCerver *admin_cerver, 
	const char *username);

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
extern u8 admin_cerver_broadcast_to_admins (AdminCerver *admin_cerver, struct _Packet *packet);

#pragma endregion

#pragma region start

extern u8 admin_cerver_start (struct _Cerver *cerver);

#pragma endregion

#pragma region end

extern u8 admin_cerver_end (AdminCerver *admin_cerver);

#pragma endregion

#endif