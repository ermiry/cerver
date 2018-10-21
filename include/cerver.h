#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdint.h>

#include "utils/vector.h"
#include "utils/config.h"
#include "utils/myUtils.h"

#define EXIT_FAILURE    1

#define THREAD_OK       0

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef unsigned char asciiChar;

typedef void (*Func)(void *);   // 18/10/2018 -- testing a type of func like in c#

/*** SEVER ***/

#define DEFAULT_PROTOCOL                IPPROTO_TCP
#define DEFAULT_PORT                    7001
#define DEFAULT_CONNECTION_QUEUE        7

#define MAX_PORT_NUM            65535
#define MAX_UDP_PACKET_SIZE     65515

typedef enum ServerType {

    FILE_SERVER = 1,
    WEB_SERVER, 
    GAME_SERVER

} ServerType;

// game server data
typedef struct GameServerData {

    // const packet sizes
    size_t lobbyPacketSize;
    size_t updatedGamePacketSize;
    size_t playerInputPacketSize;

    Vector lobbys;
    Vector players;

} GameServerData;

// TODO: what other info do we need to store?
// anyone that connects to the server
typedef struct Client {

    u32 clientID;
    i32 clientSock;
    // struct sockaddr_storage address;

} Client;

// this is the generic server struct, used to create different server types
typedef struct Server {

    i32 serverSock;         // server socket
    u8 useIpv6;  
    u8 protocol;            // 12/10/2018 - we only support either tcp or udp
    u16 port; 
    u16 connectionQueue;    // each server can handle connection differently

    bool isRunning;           // 19/10/2018 - the server is recieving and/or sending packetss

    ServerType type;
    void *serverData;
    void (*destroyServerdata) (void *data);
    // TODO: maybe we can add more delegates here such as how packets need to be send, or what packets does the
    // server expect, how to hanlde player input... all of that to make a more dynamic framework in the end...

    // TODO: 14/10/2018 - maybe we can have listen and handle connections as generir functions, also a generic function
    // to recieve packets and specific functions to cast the packet to the type that we need?

    // do web servers need this?
    Vector clients;     // connected clients

    // 20/10/2018 -- i dont like this...
    Vector holdClients;     // hold on the clients until they authenticate

} Server;

/*** SERVER FUNCS ***/

extern Server *cerver_createServer (Server *, ServerType, void (*destroyServerdata) (void *data));

extern u8 cerver_startServer (Server *);

// TODO: do we need this to be public?
extern void *connectionHandler (void *);
extern void listenForConnections (Server *);

extern void cerver_shutdownServer (Server *);
extern u8 cerver_teardown (Server *);
extern Server *cerver_restartServer (Server *);

/*** REQUESTS ***/

typedef enum RequestType {

    REQ_GET_FILE = 1,
    POST_SEND_FILE,
    
    REQ_CREATE_LOBBY = 3,

    REQ_TEST = 100,

} RequestType;

// here we can add things like file names or game types
typedef struct RequestData {

    RequestType type;

} RequestData;

/*** PACKETS ***/

typedef u32 ProtocolId;

typedef struct Version {

	u16 major;
	u16 minor;
	
} Version;

extern ProtocolId PROTOCOL_ID;
extern Version PROTOCOL_VERSION;

// TODO: I think we can use this for manu more applications?
// maybe something like our request type?
typedef enum PacketType {

	REQUEST = 1,
    CREATE_GAME,
	GAME_UPDATE_TYPE,
	PLAYER_INPUT_TYPE,

    TEST_PACKET_TYPE = 100

} PacketType;

typedef struct PacketHeader {

	ProtocolId protocolID;
	Version protocolVersion;
	PacketType packetType;

} PacketHeader;

/*** GAME SERVER ***/

extern void recievePackets (void);  // FIXME: is this a game server specific?

extern void checkTimeouts (void);
extern void sendGamePackets (Server *server, int to) ;

extern void destroyGameServer (void *data);

#endif