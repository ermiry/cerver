#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

#include "game.h"   // for type defs

/*** REQUESTS ***/

typedef enum RequestType {

    REQ_GET_FILE = 1,
    POST_SEND_FILE,
    
    REQ_CREATE_LOBBY = 3,

    REQ_TEST = 100,

} RequestType;

typedef struct RequestData {

    RequestType type;

} RequestData;

#define CLIENT_REQ_TYPE_SIZE     8

extern int makeRequest (RequestType requestType) ;

/*** CONNECTION ***/

extern bool connected;

extern int connectToServer (void);
extern void disconnectFromServer (void);

/*** PACKETS ***/

#define MAX_UDP_PACKET_SIZE     65515

typedef u32 ProtocolId;

typedef struct Version {

	u16 major;
	u16 minor;
	
} Version;

extern ProtocolId PROTOCOL_ID;
extern Version PROTOCOL_VERSION;

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

#endif