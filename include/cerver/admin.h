#ifndef _CERVER_ADMIN_H_
#define _CERVER_ADMIN_H_

#include <pthread.h>
#include <poll.h>

#include "cerver/types/types.h"

#include "cerver/collections/dllist.h"

#include "cerver/cerver.h"
#include "cerver/handler.h"
#include "cerver/packets.h"

struct _Cerver;
struct _Handler;

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
	// this handlers will only be accessible for authenticated admins
    struct _Handler *app_packet_handler;
    struct _Handler *app_error_packet_handler;
    struct _Handler *custom_packet_handler;

	pthread_t update_thread_id;

	struct _AdminCerverStats *stats;

};

typedef struct _AdminCerver AdminCerver;

#endif