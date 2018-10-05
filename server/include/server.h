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

extern void die (char *msg);

/*** SEVER ***/

// TODO: maybe load this from a cfg file, it can be different for each type of server?
#define CONNECTION_QUEUE        7

#define MAX_UDP_PACKET_SIZE     65515

typedef enum ServerType {

    FILE_SERVER = 1,
    WEB_SERVER, 
    GAME_SERVER

} ServerType;

// TODO: what other info do we need to store?
// anyone that connects to the server
typedef struct Client {

    u32 clientID;
    i32 clientSock;
    struct sockaddr_storage address;

} Client;

// TODO: create different server types, like for a game or a web server
typedef struct Server {

    i32 serverSock;         // server socket
    u16 port;
    u8 useIpv6;   
    u16 connectionQueue;    // each server can handle connection differently
    // TODO: handle upd or tcp connection

    ServerType type;

    // does web servers need this?
    Vector clients;     // connected clients

    // TODO: 05/10/2018 -- this should be temporary
    // these will only be if we are a game server
    Vector lobbys;
    Vector players;

} Server;

/*** SERVER FUNCS ***/

extern u32 initServer (Server *, Config *, ServerType);

extern void *connectionHandler (void *);
extern void listenForConnections (Server *);

extern u8 teardown (Server *);

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

/*** MULTIPLAYER ***/

extern void recievePackets (void);
extern void checkTimeouts (void);
extern void sendGamePackets (int destPlayer);

/*** LOG ***/

// TODO: add more types here...
typedef enum LogMsgType {

	NO_TYPE = 0,

    ERROR = 1,
    WARNING,
    DEBUG,
    TEST,

    REQ = 10,
    PACKET,
    FILE_REQ,
    GAME,
    PLAYER,

    SERVER = 100

} LogMsgType;

void logMsg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
    const char *msg);

#endif