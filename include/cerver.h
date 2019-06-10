#ifndef _CERVER_H_
#define _CERVER_H_

#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <poll.h>

#include "types/types.h"
#include "types/string.h"

#include "network.h"
#include "client.h"
#include "game/game.h"

#include "collections/dllist.h"
#include "collections/avl.h"

#include "utils/objectPool.h"
#include "utils/config.h"
#include "utils/thpool.h"

#define DEFAULT_USE_IPV6                0
#define DEFAULT_PORT                    7001
#define DEFAULT_CONNECTION_QUEUE        7
#define DEFAULT_POLL_TIMEOUT            180000      // 3 min in mili secs

#define DEFAULT_REQUIRE_AUTH            0   // by default, a server does not requires authentication
#define DEFAULT_AUTH_TRIES              3   // by default, a client can try 3 times to authenticate 
#define DEFAULT_AUTH_CODE               0x4CA140FF

#define DEFAULT_USE_SESSIONS            0   // by default, a server does not support client sessions

#define DEFAULT_TH_POOL_INIT            4

#define MAX_PORT_NUM            65535
#define MAX_UDP_PACKET_SIZE     65515

#define poll_n_fds      100           // n of fds for the pollfd array

/*** SEVER ***/

#pragma region SERVER

#ifdef RUN_FROM_MAKE
    #define SERVER_CFG          "./config/server.cfg"

#elif RUN_FROM_BIN
    #define SERVER_CFG          "../config/server.cfg" 

#else
    #define SERVER_CFG          ""
#endif  

typedef enum ServerType {

    FILE_SERVER = 1,
    WEB_SERVER, 
    GAME_SERVER

} ServerType;

// info for the server to perfom a correct client authentication
typedef struct Auth {

    void *reqAuthPacket;
    size_t authPacketSize;
    u8 maxAuthTries;                // client's chances of auth before being dropped
    delegate authenticate;          // authentication function

} Auth;

// this is the generic server struct, used to create different server types
struct _Server {

    i32 serverSock;         // server socket
    u8 useIpv6;  
    u8 protocol;            // we only support either tcp or udp
    u16 port; 
    u16 connectionQueue;    // each server can handle connection differently

    bool isRunning;         // he server is recieving and/or sending packetss
    bool blocking;          // sokcet fd is blocking?

    ServerType type;
    void *serverData;
    Action destroyServerData;

    // do web servers need this?
    AVLTree *clients;                   // connected clients
    Pool *clientsPool;       

    // TODO: option to make this dynamic
    struct pollfd fds[poll_n_fds];
    u16 nfds;                           // n of active fds in the pollfd array
    bool compress_clients;              // compress the fds array?
    u32 pollTimeout;           

    bool authRequired;      // does the server requires authentication?
    Auth auth;              // server auth info

    // on hold clients         
    AVLTree *onHoldClients;                 // hold on the clients until they authenticate
    // TODO: option to make this dynamic
    struct pollfd hold_fds[poll_n_fds];
    u16 hold_nfds;
    bool compress_hold_clients;              // compress the hold fds array?
    bool holdingClients;

    Pool *packetPool;

    threadpool thpool;

    void *serverInfo;           // useful info that we can send to clients 
    Action sendServerInfo;      // method to send server into to the client              

    // allow the clients to use sessions (have multiple connections)
    bool useSessions;  
    // admin defined function to generate session ids bassed on usernames, etc             
    Action generateSessionID; 

    // the admin can define a function to handle the recieve buffer if they are using a custom protocol
    // otherwise, it will be set to the default one
    Action handle_recieved_buffer;

    // server info/stats
    // TODO: use this in the thpool names
    String *name;
    u32 connectedClients;
    u32 n_hold_clients;

};

typedef struct _Server Server;

/*** Cerver Configuration ***/

extern void cerver_set_auth_method (Server *server, delegate authMethod);

extern void cerver_set_handler_received_buffer (Server *server, Action handler);

extern void session_set_id_generator (Server *server, Action idGenerator);
extern char *session_default_generate_id (i32 fd, const struct sockaddr_storage address);

/*** Cerver Methods ***/

// cerver constructor, with option to init with some values
extern Server *cerver_new (Server *cerver);
extern void cerver_delete (Server *cerver);

// creates a new cerver of the specified type and with option for a custom name
// also has the option to take another cerver as a paramater
// if no cerver is passed, configuration will be read from config/server.cfg
extern Server *cerver_create (ServerType type, const char *name, Server *cerver);

// teardowns the cerver and creates a fresh new one with the same parameters
extern Server *cerver_restart (Server *server);

// starts the cerver
extern u8 cerver_start (Server *server);

// disable socket I/O in both ways and stop any ongoing job
extern u8 cerver_shutdown (Server *server);

// teardown a server -> stop the server and clean all of its data
extern u8 cerver_teardown (Server *server);

// auxiliary struct for handle_recieved_buffer Action
typedef struct RecvdBufferData {

    Server *server; 
    i32 sock_fd;
    char *buffer; 
    size_t total_size; 
    bool onHold;

} RecvdBufferData;

extern void rcvd_buffer_data_delete (RecvdBufferData *data);

#pragma endregion

/*** LOAD BALANCER ***/

#pragma region LOAD BALANCER

// this is the generic load balancer struct, used to configure a load balancer
typedef struct LoadBalancer {

    i32 lbsock;             // lb socket
    u8 useIpv6;  
    u8 protocol;            // 12/10/2018 - we only support either tcp or udp
    u16 port; 
    u16 connectionQueue;    // number of queue connections

    bool isRunning;         // 22/10/2018 - the lb is handling the traffic

    DoubleList *servers;          // list of servers managed by the load balancer

    Pool *clientPool;       // does the load balancer handles clients directly??

    void (*destroyLoadBalancer) (void *data);   // ptr to a custom functipn to destroy the lb?

    // TODO: 14/10/2018 - maybe we can have listen and handle connections as generic functions, also a generic function
    // to recieve packets and specific functions to cast the packet to the type that we need?

} LoadBalancer;

#pragma endregion

/*** PACKETS ***/

#pragma region PACKETS

// info from a recieved packet to be handle
struct _PacketInfo {

    Server *server;
    Client *client;
    i32 clientSock; 
    char *packetData;
    size_t packetSize;

};

typedef struct _PacketInfo PacketInfo;

typedef u32 ProtocolId;

typedef struct Version {

	u16 major;
	u16 minor;
	
} Version;

extern ProtocolId PROTOCOL_ID;
extern Version PROTOCOL_VERSION;

// this indicates what type of packet we are sending/recieving
typedef enum PacketType {

    SERVER_PACKET = 0,
    CLIENT_PACKET,
    ERROR_PACKET,
	REQUEST,
    AUTHENTICATION,
    GAME_PACKET,

    APP_ERROR_PACKET,
    APP_PACKET,

    TEST_PACKET = 100,
    DONT_CHECK_TYPE,

} PacketType;

typedef struct PacketHeader {

    ProtocolId protocolID;
	Version protocolVersion;
	PacketType packetType;
    size_t packetSize;             // expected packet size

} PacketHeader;

// this indicates the data and more info about the packet type
typedef enum RequestType {

    SERVER_INFO = 0,
    SERVER_TEARDOWN,

    CLIENT_DISCONNET,

    REQ_GET_FILE,
    POST_SEND_FILE,
    
    REQ_AUTH_CLIENT,
    CLIENT_AUTH_DATA,
    SUCCESS_AUTH,

    LOBBY_CREATE,
    LOBBY_JOIN,
    LOBBY_LEAVE,
    LOBBY_UPDATE,
    LOBBY_NEW_OWNER,
    LOBBY_DESTROY,

    GAME_INIT,      // prepares the game structures
    GAME_START,     // strat running the game
    GAME_INPUT_UPDATE,
    GAME_SEND_MSG,

} RequestType;
 
// here we can add things like file names or game types
typedef struct RequestData {

    RequestType type;

} RequestData;

typedef enum ErrorType {

    ERR_SERVER_ERROR = 0,   // internal server error, like no memory

    ERR_CREATE_LOBBY = 1,
    ERR_JOIN_LOBBY,
    ERR_LEAVE_LOBBY,
    ERR_FIND_LOBBY,

    ERR_GAME_INIT,

    ERR_FAILED_AUTH,

} ErrorType;

typedef struct ErrorData {

    ErrorType type;
    char msg[256];

} ErrorData;

extern void initPacketHeader (void *header, PacketType type, u32 packetSize);
extern void *generatePacket (PacketType packetType, size_t packetSize);
extern u8 checkPacket (size_t packetSize, char *packetData, PacketType expectedType);

extern PacketInfo *newPacketInfo (Server *server, Client *client, i32 clientSock,
    char *packetData, size_t packetSize);

extern i8 tcp_sendPacket (i32 socket_fd, const void *begin, size_t packetSize, int flags);
extern i8 udp_sendPacket (Server *server, const void *begin, size_t packetSize, 
    const struct sockaddr_storage address);
extern u8 server_sendPacket (Server *server, i32 socket_fd, struct sockaddr_storage address, 
    const void *packet, size_t packetSize);

extern void *generateErrorPacket (ErrorType type, const char *msg);
extern u8 sendErrorPacket (Server *server, i32 sock_fd, struct sockaddr_storage address,
    ErrorType type, const char *msg);

#pragma endregion

/*** GAME SERVER ***/

#pragma region GAME SERVER

struct _GameSettings;
struct _Lobby;

extern u16 nextPlayerId;

#pragma endregion

/*** MISC SERVER FUNCS ***/

#pragma region MISC

extern i32 getFreePollSpot (Server *server);

extern void dropClient (Server *server, Client *client);

#pragma endregion

/*** SERIALIZATION ***/

#pragma region SERIALIZATION

// 24/10/2018 -- lets try how this works --> our goal with serialization is to send 
// a packet without our data structures but whitout any ptrs

// 17/11/2018 - send useful server info to the client trying to connect
typedef struct SServer {

    u8 useIpv6;  
    u8 protocol;            // we only support either tcp or udp
    u16 port; 

    ServerType type;
    bool authRequired;      // authentication required by the server

} SServer;

// default auth data to use by default auth function
typedef struct DefAuthData {

    u32 code;

} DefAuthData;

typedef int32_t SRelativePtr;

struct _SArray {

    i32 n_elems;
    SRelativePtr begin;

};

typedef struct _SArray SArray;

// session id - token
typedef struct Token {

    char token[65];

} Token;

#pragma endregion

#endif