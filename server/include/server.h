#ifndef SERVER_H
#define SERVER_H

#include <stdio.h>
#include <stdint.h>

#include "utils/myUtils.h"

#define EXIT_FAILURE    1

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

/*** SEVER VALUES ***/

// TODO: maybe load this from a cfg file, it can be different for each type of server?
#define CONNECTION_QUEUE        7

#define MAX_UDP_PACKET_SIZE     65515

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

#define CLIENT_REQ_TYPE_SIZE     8

extern i32 server;

/*** SERVER FUNCS ***/

#include "utils/config.h"

extern u32 initServer (Config *, u8);
extern void listenForConnections (void);
extern u8 teardown (void);

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