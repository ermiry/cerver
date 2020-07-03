#ifndef _CERVER_CONNECTION_H_
#define _CERVER_CONNECTION_H_

#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/socket.h"
#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/packets.h"
#include "cerver/handler.h"

#include "cerver/threads/thread.h"

// used for connection with exponential backoff (secs)
#define DEFAULT_CONNECTION_MAX_SLEEP                60
#define DEFAULT_CONNECTION_PROTOCOL                 PROTOCOL_TCP

#define DEFAULT_CONNECTION_UPDATE_SLEEP             200000

struct _Socket;
struct _Cerver;
struct _CerverReport;
struct _Client;
struct _Connection;
struct _PacketsPerType;
struct _SockReceive;
struct _AdminCerver;

struct _ConnectionStats {
    
    time_t threshold_time;                  // every time we want to reset the connection's stats

    u64 n_receives_done;                    // n calls to recv ()

    u64 total_bytes_received;               // total amount of bytes received from this connection
    u64 total_bytes_sent;                   // total amount of bytes that have been sent to the connection

    u64 n_packets_received;                 // total number of packets received from this connection (packet header + data)
    u64 n_packets_sent;                     // total number of packets sent to this connection

    struct _PacketsPerType *received_packets;
    struct _PacketsPerType *sent_packets;

};

typedef struct _ConnectionStats ConnectionStats;

extern ConnectionStats *connection_stats_new (void);

extern void connection_stats_print (struct _Connection *connection);

// a connection from a client
struct _Connection {

    // connection values
    // i32 sock_fd;
    struct _Socket *socket;
    bool use_ipv6;
    Protocol protocol;
    u16 port;

    estring *ip;
    struct sockaddr_storage address;

    time_t connected_timestamp;         // when the connection started

    u32 max_sleep;
    bool connected_to_cerver;

    u8 auth_tries;                          // remaining attemps to authenticate

    bool active;

    u32 receive_packet_buffer_size;         // 01/01/2020 - read packets into a buffer of this size in client_receive ()
    struct _CerverReport *cerver_report;    // 01/01/2020 - info about the cerver we are connecting to
    struct _SockReceive *sock_receive;      // 01/01/2020 - used for inter-cerver communications

    // 16/06/2020 - used for direct requests to cerver
    bool full_packet;

    // 01/01/2020 - a place to safely store the request response, like when using client_connection_request_to_cerver ()
    void *received_data;                    
    size_t received_data_size;
    Action received_data_delete;

    pthread_t update_thread_id;
    u32 update_sleep;

    bool receive_packets;                   // set if the connection will receive packets or not (default true)
    delegate custom_receive;                // custom receive method to handle incomming packets in the connection
    void *custom_receive_args;              // arguments to be passed to the custom receive method

    ConnectionStats *stats;

};

typedef struct _Connection Connection;

extern Connection *connection_new (void);

extern void connection_delete (void *ptr);

extern Connection *connection_create_empty (void);

// creates a new client connection with the specified values
extern Connection *connection_create (const i32 sock_fd, const struct sockaddr_storage address,
    Protocol protocol);

// compare two connections by their socket fds
extern int connection_comparator (const void *a, const void *b);

// get from where the client is connecting
extern void connection_get_values (Connection *connection);

// sets the connection's newtwork values
extern void connection_set_values (Connection *connection,
    const char *ip_address, u16 port, Protocol protocol, bool use_ipv6);

// sets the connection max sleep (wait time) to try to connect to the cerver
extern void connection_set_max_sleep (Connection *connection, u32 max_sleep);

// sets if the connection will receive packets or not (default true)
// if true, a new thread is created that handled incoming packets
extern void connection_set_receive (Connection *connection, bool receive);

// read packets into a buffer of this size in client_receive ()
// by default the value RECEIVE_PACKET_BUFFER_SIZE is used
extern void connection_set_receive_buffer_size (Connection *connection, u32 size);

// sets the connection received data
// 01/01/2020 - a place to safely store the request response, like when using client_connection_request_to_cerver ()
extern void connection_set_received_data (Connection *connection, void *data, size_t data_size, Action data_delete);

// 17/06/2020
// sets the waiting time (sleep) between each call to recv () in connection_update () thread
extern void connection_set_update_sleep (Connection *connection, u32 sleep);

typedef struct ConnectionCustomReceiveData {

    struct _Client *client;
    struct _Connection *connection;
    void *args;

} ConnectionCustomReceiveData;

// sets a custom receive method to handle incomming packets in the connection
// a reference to the client and connection will be passed to the action as ClientConnection structure
// alongside the arguments passed to this method
// the method must return 0 on success & 1 on error
extern void connection_set_custom_receive (Connection *connection, delegate custom_receive, void *args);

// sets up the new connection values
extern u8 connection_init (Connection *connection);

// starts a connection -> connects to the specified ip and port
// returns 0 on success, 1 on error
extern int connection_connect (Connection *connection);

// ends a client connection
extern void connection_end (Connection *connection);

// gets the connection from the on hold connections map in cerver
extern Connection *connection_get_by_sock_fd_from_on_hold (struct _Cerver *cerver, const i32 sock_fd);

// gets the connection from the client by its sock fd
extern Connection *connection_get_by_sock_fd_from_client (struct _Client *client, const i32 sock_fd);

// gets the connection from the admin cerver by its sock fd
extern Connection *connection_get_by_sock_fd_from_admin (struct _AdminCerver *admin_cerver, const i32 sock_fd);

// checks if the connection belongs to the client
extern bool connection_check_owner (struct _Client *client, Connection *connection);

// registers a new connection to a client without adding it to the cerver poll
// returns 0 on success, 1 on error
extern u8 connection_register_to_client (struct _Client *client, Connection *connection);

// registers the client connection to the cerver's strcutures (like maps)
// returns 0 on success, 1 on error
extern u8 connection_register_to_cerver (struct _Cerver *cerver, 
    struct _Client *client, Connection *connection);

// unregister the client connection from the cerver's structures (like maps)
// returns 0 on success, 1 on error
extern u8 connection_unregister_from_cerver (struct _Cerver *cerver, Connection *connection);

// wrapper function for easy access
// registers a client connection to the cerver poll array
// returns 0 on success, 1 on error
extern u8 connection_register_to_cerver_poll (struct _Cerver *cerver, Connection *connection);

// wrapper function for easy access
// unregisters a client connection from the cerver poll array
// returns 0 on success, 1 on error
extern u8 connection_unregister_from_cerver_poll (struct _Cerver *cerver, Connection *connection);

// first adds the client connection to the cerver's poll array, and upon success,
// adds the connection to the cerver's structures
// this method is equivalent to call connection_register_to_cerver_poll () & connection_register_to_cerver
// returns 0 on success, 1 on error
extern u8 connection_add_to_cerver (struct _Cerver *cerver, struct _Client *client, Connection *connection);

// removes the connection's sock fd from the cerver's poll array and then removes the connection
// from the cerver's structures
// this method is equivalent to call connection_unregister_from_cerver_poll () & connection_unregister_from_cerver ()
// returns 0 on success, 1 on error
extern u8 connection_remove_from_cerver (struct _Cerver *cerver, Connection *connection);

// starts listening and receiving data in the connection sock
extern void connection_update (void *ptr);

#endif