#ifndef _CERVER_ADMIN_H_
#define _CERVER_ADMIN_H_

#include <stdbool.h>

#include <pthread.h>
#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/handler.h"
#include "cerver/packets.h"

struct _Cerver;
struct _Client;
struct _Handler;
struct _Packet;

struct _AdminCerver;

#pragma region stats

struct _AdminCerverStats {

	time_t threshold_time;                          // every time we want to reset cerver stats (like packets), defaults 24hrs

	u64 total_n_packets_received;                   // total number of packets received (packet header + data)
	u64 total_n_receives_done;                      // total amount of actual calls to recv ()
	u64 total_bytes_received;                       // total amount of bytes received in the cerver
	
	u64 total_n_packets_sent;                       // total number of packets that were sent
	u64 total_bytes_sent;                           // total amount of bytes sent by the cerver

	u64 current_connections;      					// all of the current auth active connections for all current clients
	u64 current_connected_admins;            		// the current number of auth admins connected 

	u64 total_n_admins;                            	// the total amount of clients that were registered to the cerver (no auth required)
	u64 unique_admins;                             	// n unique clients connected in a threshold time (check used authentication)
	u64 total_admin_connections;                   	// the total amount of client connections that have been done to the cerver

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

};

typedef struct _AdminCerverStats AdminCerverStats;

CERVER_PUBLIC void admin_cerver_stats_print (AdminCerverStats *stats);

#pragma endregion

#pragma region admin

struct _Admin {

	estring *id;						// unique admin identifier

	struct _Client *client;				// network values for the admin

	// a place to store dedicated admin data
	void *data;
	Action delete_data;

	u32 bad_packets;					// disconnect after a number of bad packets

};

typedef struct _Admin Admin;

CERVER_PRIVATE Admin *admin_new (void);

CERVER_PRIVATE void admin_delete (void *admin_ptr);

CERVER_PRIVATE Admin *admin_create (void);

CERVER_PRIVATE Admin *admin_create_with_client (struct _Client *client);

CERVER_PUBLIC int admin_comparator_by_id (const void *a, const void *b);

// sets dedicated admin data and a way to delete it, if NULL, it won't be deleted
CERVER_PUBLIC void admin_set_data (Admin *admin, void *data, Action delete_data);

// gets an admin by a matching connection in its client with the specified sock fd
CERVER_PUBLIC Admin *admin_get_by_sock_fd (struct _AdminCerver *admin_cerver, const i32 sock_fd);

// gets an admin by a matching client's session id
CERVER_PUBLIC Admin *admin_get_by_session_id (struct _AdminCerver *admin_cerver, const char *session_id);

// removes the connection from the admin referred to by the sock fd
// and also checks if there is another active connection in the admin, if not it will be dropped
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 admin_remove_connection_by_sock_fd (struct _AdminCerver *admin_cerver, Admin *admin, const i32 sock_fd);

// sends a packet to the first connection of the specified admin
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 admin_send_packet (Admin *admin, struct _Packet *packet);

#pragma endregion

#pragma region main

#define DEFAULT_MAX_ADMINS							1
#define DEFAULT_MAX_ADMIN_CONNECTIONS				1

#define DEFAULT_N_BAD_PACKETS_LIMIT					5

#define DEFAULT_ADMIN_MAX_N_FDS						10
#define DEFAULT_ADMIN_POLL_TIMEOUT					2000

struct _AdminCerver {

	struct _Cerver *cerver;				// the cerver this belongs to

	DoubleList *admins;					// connected admins to the cerver

	delegate authenticate;              // authentication method

	u8 max_admins;						// the max numbers of admins allowed at any time
	u8 max_admin_connections;			// the max number of connections allowed per admin

	// number of bad packets before ending connection
	u32 n_bad_packets_limit;

	struct pollfd *fds;
	u32 max_n_fds;                      // current max n fds in pollfd
	u16 current_n_fds;                  // n of active fds in the pollfd array
	u32 poll_timeout;
	pthread_mutex_t *poll_lock;

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

	bool check_packets;                     // enable / disbale packet checking

	pthread_t update_thread_id;
    Action update;                          // method to be executed every tick
    void *update_args;                      // args to pass to custom update method
    u8 update_ticks;                        // like fps

    pthread_t update_interval_thread_id;
    Action update_interval;                 // the actual method to execute every x seconds
    void *update_interval_args;             // args to pass to the update method
    u32 update_interval_secs;               // the interval in seconds

	struct _AdminCerverStats *stats;

};

typedef struct _AdminCerver AdminCerver;

CERVER_PRIVATE AdminCerver *admin_cerver_new (void);

CERVER_PRIVATE void admin_cerver_delete (AdminCerver *admin_cerver);

CERVER_PRIVATE AdminCerver *admin_cerver_create (void);

// sets the authentication methods to be used to successfully authenticate admin credentials
// must return 0 on success, 1 on error
CERVER_EXPORT void admin_cerver_set_authenticate (AdminCerver *admin_cerver, delegate authenticate);

// sets the max numbers of admins allowed at any given time
CERVER_EXPORT void admin_cerver_set_max_admins (AdminCerver *admin_cerver, u8 max_admins);

// sets the max number of connections allowed per admin
CERVER_EXPORT void admin_cerver_set_max_admin_connections (AdminCerver *admin_cerver, u8 max_admin_connections);

// sets the mac number of packets to tolerate before dropping an admin connection
// n_bad_packets_limit for NON auth admins
// n_bad_packets_limit_auth for authenticated clients
// -1 to use defaults (5 and 20)
CERVER_EXPORT void admin_cerver_set_bad_packets_limit (AdminCerver *admin_cerver, i32 n_bad_packets_limit);

// sets the max number of poll fds for the admin cerver
CERVER_EXPORT void admin_cerver_set_max_fds (AdminCerver *admin_cerver, u32 max_n_fds);

// sets a custom poll time out to use for admins
CERVER_EXPORT void admin_cerver_set_poll_timeout (AdminCerver *admin_cerver, u32 poll_timeout);

// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
CERVER_EXPORT void admin_cerver_set_app_handlers (AdminCerver *admin_cerver, 
	struct _Handler *app_handler, struct _Handler *app_error_handler);

// sets option to automatically delete APP_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
CERVER_EXPORT void admin_cerver_set_app_handler_delete (AdminCerver *admin_cerver, bool delete_packet);

// sets option to automatically delete APP_ERROR_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
CERVER_EXPORT void admin_cerver_set_app_error_handler_delete (AdminCerver *admin_cerver, bool delete_packet);

// sets a CUSTOM_PACKET packet type handler
CERVER_EXPORT void admin_cerver_set_custom_handler (AdminCerver *admin_cerver, struct _Handler *custom_handler);

// sets option to automatically delete CUSTOM_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
CERVER_EXPORT void admin_cerver_set_custom_handler_delete (AdminCerver *admin_cerver, bool delete_packet);

// returns the total number of handlers currently alive (ready to handle packets)
CERVER_EXPORT unsigned int admin_cerver_get_n_handlers_alive (AdminCerver *admin_cerver);

// returns the total number of handlers currently working (handling a packet)
CERVER_EXPORT unsigned int admin_cerver_get_n_handlers_working (AdminCerver *admin_cerver);

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
CERVER_EXPORT void admin_cerver_set_check_packets (AdminCerver *admin_cerver, bool check_packets);

// sets a custom update function to be executed every n ticks
// a new thread will be created that will call your method each tick
// the update args will be passed to your method as a CerverUpdate & won't be deleted 
CERVER_EXPORT void admin_cerver_set_update (AdminCerver *admin_cerver, Action update, void *update_args, const u8 fps);

// sets a custom update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
// the update interval args will be passed to your method as a CerverUpdate & won't be deleted 
CERVER_EXPORT void admin_cerver_set_update_interval (AdminCerver *admin_cerver, Action update, void *update_args, const u32 interval);

// returns the current number of connected admins
CERVER_EXPORT u8 admin_cerver_get_current_admins (AdminCerver *admin_cerver);

// broadcasts a packet to all connected admins in an admin cerver
// returns 0 on success, 1 on error
CERVER_EXPORT u8 admin_cerver_broadcast_to_admins (AdminCerver *admin_cerver, struct _Packet *packet);

// registers a newly created admin to the admin cerver structures (internal & poll)
// this will allow the admin cerver to start handling admin's packets
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_register_admin (AdminCerver *admin_cerver, Admin *admin);

// unregisters an existing admin from the admin cerver structures (internal & poll)
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_unregister_admin (AdminCerver *admin_cerver, Admin *admin);

// unregisters an admin from the cerver & then deletes it
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_drop_admin (AdminCerver *admin_cerver, Admin *admin);

#pragma endregion

#pragma region start

CERVER_PRIVATE u8 admin_cerver_start (AdminCerver *admin_cerver);

#pragma endregion

#pragma region end

CERVER_PRIVATE u8 admin_cerver_end (AdminCerver *admin_cerver);

#pragma endregion

#pragma region handler

// handles a packet from an admin
CERVER_PRIVATE void admin_packet_handler (struct _Packet *packet);

#pragma endregion

#pragma region poll

// regsiters a client connection to the cerver's admin poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_poll_register_connection (AdminCerver *admin_cerver, struct _Connection *connection);

// unregsiters a sock fd connection from the cerver's admin poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 admin_cerver_poll_unregister_sock_fd (AdminCerver *admin_cerver, const i32 sock_fd);

#pragma endregion

#endif