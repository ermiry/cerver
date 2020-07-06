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
#include "cerver/collections/pool.h"

#include "cerver/admin.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/handler.h"

#include "cerver/threads/thpool.h"

#include "cerver/game/game.h"

#define DEFAULT_CONNECTION_QUEUE            7

#define DEFAULT_TH_POOL_INIT                4

#define MAX_PORT_NUM                        65535
#define MAX_UDP_PACKET_SIZE                 65515

#define DEFAULT_POLL_TIMEOUT                2000
#define poll_n_fds                          100         // n of fds for the pollfd array

#define DEFAULT_SOCKETS_INIT                10

#define DEFAULT_MAX_INACTIVE_TIME           60
#define DEFAULT_CHECK_INACTIVE_INTERVAL     30

#define DEFAULT_UPDATE_TICKS                15
#define DEFAULT_UPDATE_INTERVAL_SECS        1

struct _Cerver;
struct _AdminCerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _PacketsPerType;
struct _Handler;

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

// sends the cerver info packet
// retuns 0 on success, 1 on error
extern u8 cerver_info_send_info_packet (struct _Cerver *cerver, 
    struct _Client *client, struct _Connection *connection);

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

    u64 current_active_client_connections;          // all of the current active connections for all current clients (active in main poll array)
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
    Thpool *thpool;

    // 29/05/2020
    // using this pool to avoid completely destroying connection's sockets
    // as another thread might be blocked by the socket's mutex
    unsigned int sockets_pool_init;
    Pool *sockets_pool;

    AVLTree *clients;                   // connected clients 
    Htab *client_sock_fd_map;           // direct indexing by sokcet fd as key
    // action to be performed when a new client connects
    Action on_client_connected;   

    // 17/06/2020 - ability to check for inactive clients
    // clients that have not been sent or received from a packet in x time
    // will be automatically dropped from the cerver
    bool inactive_clients;              // enable / disable checking
    u32 max_inactive_time;              // max secs allowed for a client to be inactive
    u32 check_inactive_interval;        // how often to check for inactive clients
    pthread_t inactive_thread_id;

    struct pollfd *fds;
    u32 max_n_fds;                      // current max n fds in pollfd
    u16 current_n_fds;                  // n of active fds in the pollfd array
    u32 poll_timeout;   
    pthread_mutex_t *poll_lock;        

    /*** auth ***/
    bool auth_required;                 // does the server requires authentication?
    struct _Packet *auth_packet;        // requests client authentication
    u8 max_auth_tries;                  // client's chances of auth before being dropped
    delegate authenticate;              // authentication function
     
    AVLTree *on_hold_connections;       // hold on the connections until they authenticate
    Htab *on_hold_connection_sock_fd_map;
    struct pollfd *hold_fds;
    u32 on_hold_poll_timeout;
    u32 max_on_hold_connections;
    u16 current_on_hold_nfds;
    pthread_t on_hold_poll_id;
    pthread_mutex_t *on_hold_poll_lock;

    // allow the clients to use sessions (have multiple connections)
    bool use_sessions;  
    // admin defined function to generate session ids, it takes a session data struct            
    void *(*session_id_generator) (const void *);

    // the admin can define a function to handle the recieve buffer if they are using a custom protocol
    // otherwise, it will be set to the default one
    Action handle_received_buffer;

    // 27/05/2020 - changed form Action to Handler
    // custom packet hanlders
    struct _Handler *app_packet_handler;
    struct _Handler *app_error_packet_handler;
    struct _Handler *custom_packet_handler;

    bool app_packet_handler_delete_packet;
    bool app_error_packet_handler_delete_packet;
    bool custom_packet_handler_delete_packet;

    // 10/05/2020
    bool multiple_handlers;
    // DoubleList *handlers;
    struct _Handler **handlers;
    unsigned int n_handlers;
    unsigned int num_handlers_alive;       // handlers currently alive
    unsigned int num_handlers_working;     // handlers currently working
    pthread_mutex_t *handlers_lock;
    // TODO: add ability to control handler execution
    // pthread_cond_t *handlers_wait;

    bool check_packets;                     // enable / disbale packet checking

    pthread_t update_thread_id;
    Action update;                          // method to be executed every tick
    void *update_args;                      // args to pass to custom update method
    u8 update_ticks;                        // like fps

    pthread_t update_interval_thread_id;
    Action update_interval;                 // the actual method to execute every x seconds
    void *update_interval_args;             // args to pass to the update method
    u32 update_interval_secs;               // the interval in seconds          

    struct _AdminCerver *admin;
    pthread_t admin_thread_id;

    CerverInfo *info;
    CerverStats *stats;

};

typedef struct _Cerver Cerver;

#pragma region main

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
// this will enable the cerver's ability to handle received packets using multiple threads
// usefull if you want the best concurrency and effiency
// but we aware that you need to make your structures and data thread safe, as they might be accessed 
// from multiple threads at the same time
// by default, all received packets will be handle only in one thread
extern void cerver_set_thpool_n_threads (Cerver *cerver, u16 n_threads);

// sets the initial number of sockets to be created in the cerver's sockets pool
// the defauult value is 10
extern void cerver_set_sockets_pool_init (Cerver *cerver, unsigned int n_sockets);

// sets an action to be performed by the cerver when a new client connects
extern void cerver_set_on_client_connected  (Cerver *cerver, Action on_client_connected);

// 17/06/2020
// enables the ability to check for inactive clients - clients that have not been sent or received from a packet in x time
// will be automatically dropped from the cerver
// max_inactive_time - max secs allowed for a client to be inactive, 0 for default
// check_inactive_interval - how often to check for inactive clients in secs, 0 for default
extern void cerver_set_inactive_clients (Cerver *cerver, 
    u32 max_inactive_time, u32 check_inactive_interval);

// sets the cerver poll timeout in ms
extern void cerver_set_poll_time_out (Cerver *cerver, const u32 poll_timeout);

// enables cerver's built in authentication methods
// cerver requires client authentication upon new client connections
// max_auth_tries is the number of failed auth allowed for each new client connection
// authenticate is a user defined method that takes a AuthPacket ptr that should NOT be free
// and must return 0 on a success authentication & 1 on any error
// retuns 0 on success, 1 on error
extern u8 cerver_set_auth (Cerver *cerver, u8 max_auth_tries, delegate authenticate);

// sets the max auth tries a new connection is allowed to have before it is dropped due to failure
extern void cerver_set_auth_max_tries (Cerver *cerver, u8 max_auth_tries);

// sets the method to be used for client authentication
// must return 0 on success authentication
extern void cerver_set_auth_method (Cerver *cerver, delegate authenticate);

// sets the cerver on poll timeout in ms
extern void cerver_set_on_hold_poll_timeout (Cerver *cerver, u32 on_hold_poll_timeout);

// configures the cerver to use client sessions
// This will allow for multiple connections from the same client, 
// or you can use it to allow different connections from different devices using a token
// you can pass your own session generator method that must take a SessionData as the argument
// and must return a char * representing the session id (token) for the client
// or use the default one
// retuns 0 on success, 1 on error
extern u8 cerver_set_sessions (Cerver *cerver, void *(*session_id_generator) (const void *));

// sets a custom method to handle the raw received buffer from the socket
extern void cerver_set_handle_recieved_buffer (Cerver *cerver, Action handle_received_buffer);

// 27/05/2020 - changed form Action to Handler
// sets customs APP_PACKET and APP_ERROR_PACKET packet types handlers
extern void cerver_set_app_handlers (Cerver *cerver, 
    struct _Handler *app_handler, struct _Handler *app_error_handler);

// sets option to automatically delete APP_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
extern void cerver_set_app_handler_delete (Cerver *cerver, bool delete_packet);

// sets option to automatically delete APP_ERROR_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
extern void cerver_set_app_error_handler_delete (Cerver *cerver, bool delete_packet);

// 27/05/2020 - changed form Action to Handler
// sets a CUSTOM_PACKET packet type handler
extern void cerver_set_custom_handler (Cerver *cerver, struct _Handler *custom_handler);

// sets option to automatically delete CUSTOM_PACKET packets after use
// if set to false, user must delete the packets manualy 
// by the default, packets are deleted by cerver
extern void cerver_set_custom_handler_delete (Cerver *cerver, bool delete_packet);

// enables the ability of the cerver to have multiple app handlers
// returns 0 on success, 1 on error
extern int cerver_set_multiple_handlers (Cerver *cerver, unsigned int n_handlers);

// set whether to check or not incoming packets
// check packet's header protocol id & version compatibility
// if packets do not pass the checks, won't be handled and will be inmediately destroyed
// packets size must be cheked in individual methods (handlers)
// by default, this option is turned off
extern void cerver_set_check_packets (Cerver *cerver, bool check_packets);

// sets a custom cerver update function to be executed every n ticks
// a new thread will be created that will call your method each tick
// the update args will be passed to your method as a CerverUpdate & won't be deleted 
extern void cerver_set_update (Cerver *cerver, Action update, void *update_args, const u8 fps);

// sets a custom cerver update method to be executed every x seconds (in intervals)
// a new thread will be created that will call your method every x seconds
// the update args will be passed to your method as a CerverUpdate & won't be deleted 
extern void cerver_set_update_interval (Cerver *cerver, Action update, void *update_args, const u32 interval);

// enables admin connections to cerver
// returns 0 on success, 1 on error
extern u8 cerver_set_admin_enable (Cerver *cerver);

#pragma endregion

#pragma region sockets

extern int cerver_sockets_pool_push (Cerver *cerver, struct _Socket *socket);

extern struct _Socket *cerver_sockets_pool_pop (Cerver *cerver);

#pragma endregion

#pragma region handlers

// prints info about current handlers
extern void cerver_handlers_print_info (Cerver *cerver);

// adds a new handler to the cerver handlers array
// is the responsability of the user to provide a unique handler id, which must be < cerver->n_handlers
// returns 0 on success, 1 on error
extern u8 cerver_handlers_add (Cerver *cerver, struct _Handler *handler);

// returns the current number of app handlers (multiple handlers option)
extern unsigned int cerver_get_n_handlers (Cerver *cerver);

// returns the total number of handlers currently alive (ready to handle packets)
extern unsigned int cerver_get_n_handlers_alive (Cerver *cerver);

// returns the total number of handlers currently working (handling a packet)
extern unsigned int cerver_get_n_handlers_working (Cerver *cerver);

#pragma endregion

#pragma region start

// returns a new cerver with the specified parameters
extern Cerver *cerver_create (const CerverType type, const char *name, 
    const u16 port, const Protocol protocol, bool use_ipv6,
    u16 connection_queue, u32 poll_timeout);

// teardowns the cerver and creates a fresh new one with the same parameters
// returns 0 on success, 1 on error
extern u8 cerver_restart (Cerver *cerver);

// tell the cerver to start listening for connections and packets
// initializes cerver's structures like thpool (if any) 
// and any other processes that have been configured before
// returns 0 on success, 1 on error
extern u8 cerver_start (Cerver *cerver);

#pragma endregion

#pragma region end

// disable socket I/O in both ways and stop any ongoing job
// returns 0 on success, 1 on error
extern u8 cerver_shutdown (Cerver *cerver);

// teardown a server -> stop the server and clean all of its data
// returns 0 on success, 1 on error
extern u8 cerver_teardown (Cerver *cerver);

#pragma endregion

// aux structure for cerver update methods
struct _CerverUpdate {

    Cerver *cerver;
    void *args;

};

typedef struct _CerverUpdate CerverUpdate;

extern CerverUpdate *cerver_update_new (Cerver *cerver, void *args);

extern void cerver_update_delete (void *cerver_update_ptr);

#pragma region serialization

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

#pragma endregion

#pragma region report

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

#pragma endregion

#endif