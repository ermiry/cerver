#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "server.h"
#include "network.h"

#include "game.h"

#include "utils/vector.h"
#include "utils/config.h"

/*** VALUES ***/

// TODO: maybe move these to the config file??
// FIXME: choose wisely...
// these 2 are used to manage the packets
ProtocolId PROTOCOL_ID = 0xEC3B5FA9; // Randomly chosen.
Version PROTOCOL_VERSION = { 1, 1 };

/*** SERIALIZATION ***/

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#pragma region SERIALIZATION 

typedef int32_t RelativePtr;

typedef struct SArray {

	u32 n_elems;
	RelativePtr begin;

} SArray;

void s_ptr_to_relative (void *relativePtr, void *ptr) {

	RelativePtr result = (char *) ptr - (char *) relativePtr;
	memcpy (relativePtr, &result, sizeof (RelativePtr));

}

void s_array_init (void *array, void *begin, size_t n_elems) {

	SArray result;
	result.n_elems = n_elems;
	memcpy (array, &result, sizeof (SArray));
	s_ptr_to_relative ((char *) array + offsetof (SArray, begin), begin);

}

#pragma endregion

/*** PACKETS ***/

#pragma region PACKETS

ssize_t packetHeaderSize;
ssize_t requestPacketSize;
ssize_t updatedGamePacketSize;
ssize_t playerInputPacketSize;

// FIXME:
void handlePlayerInputPacket (struct sockaddr_storage from, PlayerInputPacket *playerInput) {

    // TODO: maybe we can have a better way for searching players??
    // check if we have the player already registerd
    Player *player = NULL;
    for (size_t p = 0; p < players.elements; p++) {
        player = vector_get (&players, p);
        if (player != NULL) {
            if (sock_ip_equal ((struct sockaddr *) &player->address, ((struct sockaddr *) &from)))
                break;  // we found a match!
        }
    }
    
    // TODO: add players only from the game lobby before the game inits!!
    // if not, add it to the game

    // handle the player input
    // player->input = packet->input;   // FIXME:
    // player->inputSequenceNum = packet->sequenceNum;
    player->lastInputTime = getTimeSpec ();

}

// check for packets with bad size, protocol, version, etc
u8 checkPacket (ssize_t packetSize, unsigned char *packetData, PacketType type) {

    if ((unsigned) packetSize < sizeof (PacketHeader)) {
        fprintf (stderr, "[PACKET][ERROR]: Recieved a to small packet!\n");
        return 1;
    } 

    PacketHeader *header = (PacketHeader *) packetData;

    if (header->protocolID != PROTOCOL_ID) {
        fprintf (stderr, "[PACKET][ERROR]: Wrong protocol ID!\n");
        return 1;
    }

    Version version = header->protocolVersion;
    if (version.major != PROTOCOL_VERSION.major) {
        fprintf(stderr,
                "[PACKET][ERROR]: Received a packet with incompatible version: "
                "%d.%d (mine is %d.%d).\n",
                version.major, version.minor,
                PROTOCOL_VERSION.major, PROTOCOL_VERSION.minor);
        return 1;
    }

    switch (type) {
        case REQUEST: 
            if (packetSize < requestPacketSize)
                fprintf(stderr, "[WARNING]: Received a too small request packet.\n");
            break;
        case GAME_UPDATE_TYPE: 
            if (packetSize < updatedGamePacketSize)
                fprintf(stderr, "[WARNING]: Received a too small game update packet.\n");
            break;
        case PLAYER_INPUT_TYPE: 
            if (packetSize < playerInputPacketSize)
                fprintf(stderr, "[WARNING]: Received a too small player input packet.\n");
            break;
        default: fprintf (stderr, "[PACKET][WARNING]: Got a pakcet of incompatible type.\n"); break;
    }

    return 0;   // packet is fine or packet gets ignored for its size

}

// FIXME: when the owner of the lobby has quit the game, we ned to stop listenning for that lobby
    // also destroy the lobby and all its mememory
// Also check if there are no players left in the lobby for any reason
// this is used to recieve packets when in a lobby/game
void recievePackets (void) {

    unsigned char packetData[MAX_UDP_PACKET_SIZE];

    struct sockaddr_storage from;
    socklen_t fromSize = sizeof (struct sockaddr_storage);
    ssize_t packetSize;

    bool recieve = true;

    while (recieve) {
        packetSize = recvfrom (server, (char *) packetData, MAX_UDP_PACKET_SIZE, 
            0, (struct sockaddr *) &from, &fromSize);

        // no more packets to process
        if (packetSize < 0) recieve = false;

        // process packets
        else {
            // just continue to the next packet if we have a bad one...
            if (checkPacket (packetSize, packetData, PLAYER_INPUT_TYPE)) continue;

            // if the packet passes all the checks, we can use it safely
            else {
                PlayerInputPacket *playerInput = (PlayerInputPacket *) (packetData + sizeof (PacketHeader));
                handlePlayerInputPacket (from, playerInput);
            }

        }        
    }   

}

// TODO: maybe this can open many possibilities in the future?
// can we use this for request types?
void initPacketHeader (void *header, PacketType type) {

    PacketHeader h;
    h.protocolID = PROTOCOL_ID;
    h.protocolVersion = PROTOCOL_VERSION;
    h.packetType = type;

    memcpy (header, &h, sizeof (PacketHeader));

}

// TODO: can we use this to send any kind of packet?
// if we are handling different servers, don't forget to add the correct sokcet...
void sendPacket (void *begin, size_t packetSize, struct sockaddr_storage address) {

    ssize_t sentBytes = sendto (server, (const char *) begin, packetSize, 0,
		       (struct sockaddr *) &address, sizeof (struct sockaddr_storage));

	if (sentBytes < 0 || (unsigned) sentBytes != packetSize) 
        fprintf (stderr, "[ERROR]: Failed to send packet!\n");

}

// FIXME:
// TODO: maybe we can clean and refactor this?
// creates and sends game packets
void sendGamePackets (int to) {

    Player *destPlayer = vector_get (&players, to);

    // first we need to prepare the packet...

    // TODO: clean this a little, but don't forget this can be dynamic!!
	size_t packetSize = sizeof (PacketHeader) + sizeof (UpdatedGamePacket) +
		players.elements * sizeof (Player);

	// buffer for packets, extend if necessary...
	static void *packetBuffer = NULL;
	static size_t packetBufferSize = 0;
	if (packetSize > packetBufferSize) {
		packetBufferSize = packetSize;
		free (packetBuffer);
		packetBuffer = malloc (packetBufferSize);
	}

	void *begin = packetBuffer;
	char *end = begin; 

	// packet header
	PacketHeader *header = (PacketHeader *) end;
    end += sizeof (PacketHeader);
    initPacketHeader (header, GAME_UPDATE_TYPE);

    // TODO: do we need to send the game settings each time?
	// game settings and other non-array data
    UpdatedGamePacket *gameUpdate = (UpdatedGamePacket *) end;
    end += sizeof (UpdatedGamePacket);
    // gameUpdate->sequenceNum = currentTick;  // FIXME:
	// tick_packet->ack_input_sequence_num = dest_player->input_sequence_num;  // FIXME:
    gameUpdate->playerId = destPlayer->id;
    gameUpdate->gameSettings.playerTimeout = 30;    // FIXME:
    gameUpdate->gameSettings.fps = 20;  // FIXME:

    // TODO: maybe add here some map elements, dropped items and enemies??
    // such as the players or explosions?
	// arrays headers --- 01/10/2018 -- we only have the players array
    // void *playersArray = (char *) gameUpdate + offsetof (UpdatedGamePacket, players);
    // FIXME:
	// void *playersArray = (char *) gameUpdate + offsetof (gameUpdate, players);

	// send player info ---> TODO: what player do we want to send?
    /* s_array_init (playersArray, end, players.elements);

    Player *player = NULL;
    for (size_t p = 0; p < players.elements; p++) {
        player = vector_get (&players, p);

        // FIXME: do we really need to serialize the packets??
        // FIXME: do we really need to have serializable data structs?

		// SPlayer *s_player = (SPlayer *) packet_end;
		// packet_end += sizeof(*s_player);
		// s_player->id = player->id;
		// s_player->alive = !!player->alive;
		// s_player->position = player->position;
		// s_player->heading = player->heading;
		// s_player->score = player->score;
		// s_player->color.red = player->color.red;
		// s_player->color.green = player->color.green;
		// s_player->color.blue = player->color.blue;
    }

    // FIXME: better eroror handling!!
    // assert (end == (char *) begin + packetSize); */

    // after the pakcet has been prepare, send it to the dest player...
    sendPacket (begin, packetSize, destPlayer->address);

}

#pragma endregion

/*** REQUESTS ***/

#pragma region REQUESTS

// TODO: get the owner and the type of lobby
void createLobby (void) {

    Lobby *lobby = newLobby ();
    if (lobby != NULL) {
        fprintf (stdout, "[GAME]: New lobby created!\n");

        // FIXME: send the lobby info to the owner

        // TODO: we can now wait for more players to join the lobby...

        // TODO: make sure that all the players have the same game settings, 
        // so send to them that info when they connect!!
    }

    else {
        fprintf (stderr, "[ERROR]: Failed to create a new game lobby!");

        // FIXME: send an error to the player to hanlde it
    } 

}

#pragma endregion

/*** SERVER ***/

#pragma region SERVER LOGIC

// FIXME: add the option to restart the server to load a new cfg file
// FIXME: add a cfg value to set the protocol
// FIXME: fix ip address

// TODO: set to non blocking mode for handling the game???
// if (!sock_setNonBlocking (server))
    // die ("\n[ERROR]: Failed to set server socket to non-blocking mode!\n");

i32 server;

const char welcome[256] = "You have reached the Multiplayer Server!";

void initServerValues (void) {

    packetHeaderSize = sizeof (PacketHeader);
    requestPacketSize = packetHeaderSize + sizeof (RequestData);
    updatedGamePacketSize = packetHeaderSize + sizeof (UpdatedGamePacket);
    playerInputPacketSize = packetHeaderSize + sizeof (PlayerInputPacket) ;

}

u32 initServer (Config *cfg, u8 type) {

	ConfigEntity *cfgEntity = getEntityWithId (cfg, type);
	if (!cfgEntity) die ("\n[ERROR]: Problems with server config!\n");

	u8 use_ipv6 = atoi (getEntityValue (cfgEntity, "ipv6"));
    server = socket ((use_ipv6 == 1 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
    if (server < 0) die ("\n[ERROR]: Failed to create server socket!\n");

    struct sockaddr_storage address;
	memset(&address, 0, sizeof (struct sockaddr_storage));

	u32 port = atoi (getEntityValue (cfgEntity, "port"));
	if (use_ipv6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &address;
		addr->sin6_family = AF_INET6;
		addr->sin6_addr = in6addr_any;
		addr->sin6_port = htons (port);
	} 

    else {
		struct sockaddr_in *addr = (struct sockaddr_in *) &address;
		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = INADDR_ANY;
		addr->sin_port = htons (port);
	}

    if (bind (server, (const struct sockaddr *) &address, sizeof (struct sockaddr)) < 0) 
        die ("\n[ERROR]: Failed to bind server socket!");

    initServerValues ();

	// return the port we are listening to upon success
	return port;

}

// TODO: check again the recieve packets function --> do we need also to pass
    // the sockaddr? or hanlde just the socket?

// FIXME: don't forget to check that tha packet type is a request!!!
// TODO: how can we handle other parameters for requests?
void connectionHandler (i32 client) {

	// send welcome message
	send (client, welcome, sizeof (welcome), 0);

    // recieving packets from a client
    unsigned char packetData[MAX_UDP_PACKET_SIZE];
    ssize_t packetSize;
        
    if ((packetSize = recv (client, packetData, sizeof (packetData), 0)) > 0) {
        logMsg (stdout, TEST, PACKET, createString ("Recieved request pakcet size: %ld.", packetSize));
        // check the packet
        if (!checkPacket (packetSize, packetData, REQUEST)) {
            // handle the request
            RequestData *reqData = (RequestData *) (packetData + sizeof (PacketHeader));
            switch (reqData->type) {
                case REQ_TEST: 
                    fprintf (stdout, "[TEST]: Packet recieved correctly!!\n");
                    break;

                default: fprintf (stderr, "[REQ][ERROR]: Invalid request type!\n");
            }
        }
            
        else fprintf (stderr, "[REQ][ERROR]: Recieved an invalid request packet from the client!\n");
    }

    // FIXME: better error handling I guess...
    else fprintf (stderr, "[REQ][ERROR]: No client request!\n");

}

// FIXME: try a similar like in the recieve packets function --> maybe with that,
// we can handle different client requests at the same time
// TODO: handle ipv6 configuration
// TODO: do we need to hanlde time here?
void listenForConnections (void) {

	listen (server, 5);

	socklen_t sockLen = sizeof (struct sockaddr_in);
	struct sockaddr_in clientAddress;
	memset (&clientAddress, 0, sizeof (struct sockaddr_in));
	int clientSocket;

	// FIXME: 29/09/2018 -- 10:40 -- we only hanlde one client connected at a time
	while ((clientSocket = accept (server, (struct sockaddr *) &clientAddress, &sockLen))) {
		fprintf (stdout, "Client connected: %s.\n", inet_ntoa (clientAddress.sin_addr));

		connectionHandler (clientSocket);
	}

}

// TODO:
// wrap things up and exit
u8 teardown (void) {

    fprintf (stdout, "\nClosing server...\n");

	// TODO: disconnect any remainning clients

    close (server);

    return 0;   // teardown was successfull

}

#pragma endregion

/*** MULTIPLAYER ***/

#pragma region MULTIPLAYER LOGIC

// TODO: better id handling and management
u16 nextPlayerId = 0;

// TODO: maybe we can get this value form a cfg?
const float PLAYER_TIMEOUT = 30;    // in seconds

// TODO:
// this is used to clean disconnected players
void checkTimeouts (void) {

}

// FIXME: handle a limit of players!!
void addPlayer (struct sockaddr_storage address) {

    // TODO: handle ipv6 ips
    char addrStr[IP_TO_STR_LEN];
    sock_ip_to_string ((struct sockaddr *) &address, addrStr, sizeof (addrStr));
    fprintf (stdout, "[PLAYER]: New player connected from ip: %s @ port: %d.\n", addrStr,
        sock_ip_port ((struct sockaddr *) &address));

    // TODO: init other necessarry game values
    // add the new player to the game
    Player newPlayer;
    newPlayer.id = nextPlayerId;
    newPlayer.address = address;

    vector_push (&players, &newPlayer);

    // FIXME: this is temporary
    spawnPlayer (&newPlayer);

}

#pragma endregion