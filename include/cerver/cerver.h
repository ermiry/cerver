#ifndef _CERVER_CERVER_H_
#define _CERVER_CERVER_H_

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

#include "cerver/admin.h"
#include "cerver/auth.h"
#include "cerver/network.h"
#include "cerver/packets.h"

#include "cerver/threads/thpool.h"

#include "cerver/game/game.h"

#define DEFAULT_CONNECTION_QUEUE        7

#define DEFAULT_TH_POOL_INIT            4

#define MAX_PORT_NUM                    65535
#define MAX_UDP_PACKET_SIZE             65515

#define DEFAULT_POLL_TIMEOUT            2000
#define poll_n_fds                      100         // n of fds for the pollfd array

struct _AdminCerver;
struct _Auth;
struct _Cerver;
struct _Packet;
struct _PacketsPerType;

typedef enum CerverType {

    CUSTOM_CERVER = 0,
    FILE_CERVER,
    GAME_CERVER,
    WEB_CERVER, 

} CerverType;

typedef struct CerverInfo {

    estring *name;
    estring *welcome_msg;                            // this msg is sent to the client when it first connects
    struct _Packet *cerver_info_packet;             // useful info that we can send to clients

    time_t time_started;                            // the actual time the cerver was started
    u64 uptime;                                     // the seconds the cerver has been up

} CerverInfo;

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
extern u8 cerver_set_welcome_msg (struct _Cerver *cerver, const char *msg);

typedef struct CerverStats {

    time_t threshold_time;                          // every time we want to reset cerver stats (like packets), defaults 24hrs
    
    u64 client_n_packets_received;                  // packets received from clients
    u64 client_receives_done;                       // receives done to clients
    u64 client_bytes_received;                      // bytes received from clients

    u64 on_hold_n_packets_received;                 // packets received from on hold connections
    u64 on_hold_receives_done;                      // received done to on hold connections
    u64 on_hold_bytes_received;                     // bytes received from on hold connections

    u64 total_n_packets_received;                   // total number of cerver packets received (packet header + data)
    u64 total_n_receives_done;                      // total amount of actual calls to recv ()
    u64 total_bytes_received;                       // total amount of bytes received in the cerver
    
    u64 n_packets_sent;                             // total number of packets that were sent
    u64 total_bytes_sent;                           // total amount of bytes sent by the cerver

    u64 current_active_client_connections;          // all of the current active connections for all current clients
    u64 current_n_connected_clients;                // the current number of clients connected 
    u64 current_n_hold_connections;                 // current numbers of on hold connections (only if the cerver requires authentication)
    u64 total_n_clients;                            // the total amount of clients that were registered to the cerver (no auth required)
    u64 unique_clients;                             // n unique clients connected in a threshold time (check used authentication)
    u64 total_client_connections;                   // the total amount of client connections that have been done to the cerver

    struct _PacketsPerType *received_packets;
    struct _PacketsPerType *sent_packets;

} CerverStats;

// sets the cerver stats threshold time (how often the stats get reset)
extern void cerver_stats_set_threshold_time (struct _Cerver *cerver, time_t threshold_time);

// prints the cerver stats
extern void cerver_stats_print (struct _Cerver *cerver);

// this is the generic cerver struct, used to create different server types
struct _Cerver {

    i32 sock;                           // server socket
    struct sockaddr_storage address;

    u16 port; 
    Protocol protocol;                  // we only support either tcp or udp
    bool use_ipv6;  
    u16 connection_queue;               // each server can handle connection differently
    u32 receive_buffer_size;

    bool isRunning;                     // the server is recieving and/or sending packetss
    bool blocking;                      // sokcet fd is blocking?

    CerverType type;
    void *cerver_data;
    Action delete_cerver_data;

    u16 n_thpool_threads;
    // threadpool *thpool;
    threadpool thpool;

    AVLTree *clients;                   // connected clients 
    Htab *client_sock_fd_map;           // direct indexing by sokcet fd as key
    // action to be performed when a new client connects
    Action on_client_connected;   

    struct pollfd *fds;
    u32 max_n_fds;                      // current max n fds in pollfd
    u16 current_n_fds;                  // n of active fds in the pollfd array
    bool compress_clients;              // compress the fds array?
    u32 poll_timeout;           

    Htab *sock_buffer_map;

    /*** auth ***/
    bool auth_required;                 // does the server requires authentication?
    struct _Auth *auth;                         // server auth info
     
    AVLTree *on_hold_connections;       // hold on the connections until they authenticate
    Htab *on_hold_connection_sock_fd_map;
    struct pollfd *hold_fds;
    u32 max_on_hold_connections;
    u16 current_on_hold_nfds;
    bool compress_on_hold;              // compress the hold fds array?
    bool holding_connections;

    // allow the clients to use sessions (have multiple connections)
    bool use_sessions;  
    // admin defined function to generate session ids, it takes a session data struct            
    void *(*session_id_generator) (const void *);

    // the admin can define a function to handle the recieve buffer if they are using a custom protocol
    // otherwise, it will be set to the default one
    Action handle_received_buffer;

    // custom packet hanlders
    Action app_packet_handler;
    Action app_error_packet_handler;
    Action custom_packet_handler;

    Action update;                          // method to be executed every tick
    void *update_args;                      // args to pass to custom update method
    u8 update_ticks;                        // like fps

    Action update_interval;                 // the actual method to execute every x seconds
    void *update_interval_args;             // args to pass to the update method
    u32 update_interval_secs;               // the interval in seconds          

    struct _AdminCerver *admin;

    CerverInfo *info;
    CerverStats *stats;

};

typedef struct _Cerver Cerver;

/*** Cerver Methods ***/

extern Cerver *cerver_new (void);

extern void cerver_delete (void *ptr);

// sets the cerver main network values
extern void cerver_set_network_values (Cerver *cerver, const u16 port, const Protocol protocol,
    bool use_ipv6, const u16 connection_queue);

// sets the cerver connection queue (how many connections to queue for accept)
extern void cerver_set_connection_queue (Cerver *cerver, const u16 connection_queue);

// sets the cerver's receive buffer size used in recv method
extern void cerver_set_receive_buffer_size (Cerver *cerver, const u32 size);

// sets the cerver's data and a way to free it
extern void cerver_set_cerver_data (Cerver *cerver, void *data, Action delete_data);

// sets the cerver's thpool number of threads
extern void cerver_set_thpool_n_threads (Cerver *cerver, u16 n_threads);

// sets an action to be performed by the cerver when a new client connects
extern void cerver_set_on_client_connected  (Cerver *cerver, Action on_client_connected);

// sets the cerver poll timeout in ms
extern void cerver_set_poll_time_out (Cerver *cerver, const u32 poll_timeout);

// configures the cerver to require client authentication upon new client connections
// retuns 0 on success, 1 on error
extern u8 cerver_set_auth (Cerver *cerver, u8 max_auth_tries, delegate authenticate);

// configures the cerver to use client sessions
// This will allow for multiple connections from the same client, 
// or you can use it to allow different connections from different devices using a token
// retuns 0 on success, 1 on error
extern u8 cerver_set_sessions (Cerver *cerver, void *(*session_id_generator) (const void *));

// sets a custom method to handle the raw received buffer from the socket
extern void cerver_set_handle_recieved_buffer (Cerver *cerver, Action handle_received_buffer);

// sets a cutom app packet hanlder and a custom app error packet handler
extern void cerver_set_app_handlers (Cerver *cerver, Action app_handler, Action app_error_handler);

// sets a custom packet handler
extern void cerver_set_custom_handler (Cerver *cerver, Action custom_handler);

// sets a custom cerver update function to be executed every n ticks
// a new thread will be created that will call your method each tick
extern void cerver_set_update (Cerver *cerver, Action update, void *update_args, const u8 fps);

// sets a custom cerver update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
extern void cerver_set_update_interval (Cerver *cerver, Action update, void *update_args, u32 interval);

// enables admin connections to cerver
// admin connections are handled in a different port and using a dedicated handler
// returns 0 on success, 1 on error
extern u8 cerver_admin_enable (Cerver *cerver, u16 port, bool use_ipv6);

// returns a new cerver with the specified parameters
extern Cerver *cerver_create (const CerverType type, const char *name, 
    const u16 port, const Protocol protocol, bool use_ipv6,
    u16 connection_queue, u32 poll_timeout);

// teardowns the cerver and creates a fresh new one with the same parameters
// returns 0 on success, 1 on error
extern u8 cerver_restart (Cerver *cerver);

// starts the cerver
extern u8 cerver_start (Cerver *cerver);

// disable socket I/O in both ways and stop any ongoing job
extern u8 cerver_shutdown (Cerver *cerver);

// teardown a server -> stop the server and clean all of its data
extern u8 cerver_teardown (Cerver *cerver);

// 31/01/2020 -- 11:15 -- aux structure for cerver update methods
struct _CerverUpdate {

    Cerver *cerver;
    void *args;

};

typedef struct _CerverUpdate CerverUpdate;

/*** Serialization ***/

// serialized cerver structure
typedef struct SCerver {

    bool use_ipv6;  
    Protocol protocol;
    u16 port; 

    char name[32];
    CerverType type;
    bool auth_required;

    bool uses_sessions;

} SCerver;

// creates a cerver info packet ready to be sent
extern struct _Packet *cerver_packet_generate (Cerver *cerver);

/*** Handler ***/

// information that we get from another cerver when connecting to it
struct _CerverReport {
    
    bool use_ipv6;  
    Protocol protocol;
    u16 port; 
    estring *ip;

    estring *name;
    CerverType type;
    bool auth_required;

    bool uses_sessions;
    struct _Token *token;

};

typedef struct _CerverReport CerverReport;

extern void cerver_report_delete (void *ptr);

// serialized cerver report structure
typedef struct SCerverReport {

    bool use_ipv6;  
    Protocol protocol;
    u16 port; 

    char name[32];
    CerverType type;
    bool auth_required;

    bool uses_sessions;

} SCerverReport;

// handles cerver type packets
extern void client_cerver_packet_handler (struct _Packet *packet);

#endif