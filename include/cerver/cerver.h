#ifndef _CERVER_CERVER_H_
#define _CERVER_CERVER_H_

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/auth.h"

#include "cerver/threads/thpool.h"

#include "cerver/game/game.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

#include "cerver/utils/config.h"

#define DEFAULT_USE_IPV6                0
#define DEFAULT_PORT                    7001
#define DEFAULT_CONNECTION_QUEUE        7
#define DEFAULT_POLL_TIMEOUT            180000      // 3 min in mili secs

#define DEFAULT_TH_POOL_INIT            8

#define MAX_PORT_NUM            65535
#define MAX_UDP_PACKET_SIZE     65515

#define poll_n_fds              100           // n of fds for the pollfd array

typedef enum CerverType {

    CUSTOM_CERVER = 0,
    FILE_CERVER,
    GAME_CERVER,
    WEB_CERVER, 

} CerverType;

// this is the generic server struct, used to create different server types
struct _Cerver {

    i32 sock;                           // server socket
    u16 port; 
    Protocol protocol;                  // we only support either tcp or udp
    bool use_ipv6;  
    u16 connection_queue;               // each server can handle connection differently

    bool isRunning;                     // the server is recieving and/or sending packetss
    bool blocking;                      // sokcet fd is blocking?

    CerverType type;
    void *cerver_data;
    Action delete_cerver_data;

    threadpool *thpool;

    AVLTree *clients;                   // connected clients 
    Htab *client_sock_fd_map;           // direct indexing by sokcet fd as key
    // action to be performed when a new client connects
    Action on_client_connected;   
    void *on_client_connected_data;
    Action delete_on_client_connected_data;

    struct pollfd *fds;
    u32 max_n_fds;                      // current max n fds in pollfd
    u16 current_n_fds;                  // n of active fds in the pollfd array
    bool compress_clients;              // compress the fds array?
    u32 poll_timeout;           

    /*** auth ***/
    bool auth_required;                 // does the server requires authentication?
    Auth *auth;                         // server auth info
     
    AVLTree *on_hold_connections;       // hold on the connections until they authenticate
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
    // Action handle_recieved_buffer;

    // custom packet hanlders
    Action app_packet_handler;
    Action app_error_packet_handler;
    Action custom_packet_handler;

    /*** server info/stats ***/
    String *name;
    String *welcome_msg;                 // this msg is sent to the client when it first connects
    struct _Packet *cerver_info_packet;          // useful info that we can send to clients 
    // TODO: 26/06/2019 -- we are not doing nothing with these!!
    u32 n_connected_clients;
    u32 n_hold_clients;

    struct tm *time_started;
    u64 uptime;

    Action cerver_update;                  
    u8 ticks;                              // like fps

};

typedef struct _Cerver Cerver;

/*** Cerver Methods ***/

extern Cerver *cerver_new (void);
extern void cerver_delete (void *ptr);

// sets the cerver main network values
extern void cerver_set_network_values (Cerver *cerver, const u16 port, const Protocol protocol,
    bool use_ipv6, const u16 connection_queue);

// sets the cerver's data and a way to free it
extern void cerver_set_cerver_data (Cerver *cerver, void *data, Action delete_data);

// sets an action to be performed by the cerver when a new client connects
extern u8 cerver_set_on_client_connected  (Cerver *cerver, 
    Action on_client_connected, void *data, Action delete_data);

// sets the cerver poll timeout
extern void cerver_set_poll_time_out (Cerver *cerver, const u32 poll_timeout);

// configures the cerver to require client authentication upon new client connections
// retuns 0 on success, 1 on error
extern u8 cerver_set_auth (Cerver *cerver, u8 max_auth_tries, delegate authenticate);

// configures the cerver to use client sessions
// retuns 0 on success, 1 on error
extern u8 cerver_set_sessions (Cerver *cerver, void *(*session_id_generator) (const void *));

// sets a cutom app packet hanlder and a custom app error packet handler
extern void cerver_set_app_handlers (Cerver *cerver, Action app_handler, Action app_error_handler);

// sets a custom packet handler
extern void cerver_set_custom_handler (Cerver *cerver, Action custom_handler);

// sets the cerver msg to be sent when a client connects
// retuns 0 on success, 1 on error
extern u8 cerver_set_welcome_msg (Cerver *cerver, const char *msg);

// sets a custom cerver update function to be executed every n ticks
extern void cerver_set_update (Cerver *cerver, Action update, const u8 ticks);

// returns a new cerver with the specified parameters
extern Cerver *cerver_create (const CerverType type, const char *name, 
    const u16 port, const Protocol protocol, bool use_ipv6,
    u16 connection_queue, u32 poll_timeout);

// teardowns the cerver and creates a fresh new one with the same parameters
extern Cerver *cerver_restart (Cerver *cerver);

// starts the cerver
extern u8 cerver_start (Cerver *cerver);

// disable socket I/O in both ways and stop any ongoing job
extern u8 cerver_shutdown (Cerver *cerver);

// teardown a server -> stop the server and clean all of its data
extern u8 cerver_teardown (Cerver *cerver);

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

#endif