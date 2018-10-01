#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <sys/time.h>
#include <time.h>

#include "utils/myTime.h"

#include "server.h"
#include "game.h"

#include "utils/vector.h"

/*** DATA STRUCTURES ***/

Vector players;

// TODO: maybe we will like to move this from here?
/*** MULTIPLAYER ***/

// TODO:
void handlePlayerInputPacket (struct sockaddr_storage from, PlayerInputPacket *playerInput) {

    // TODO: add players only from the game lobby before the game inits!!
    // check if we have the player already registerd
    // if not, add it to the game

    // handle the player input

}

// check for packets with bad size, protocol, version, etc
u8 checkPacket (ssize_t packetSize, unsigned char packetData[MAX_PACKET_SIZE]) {

    if ((unsigned) packetSize < sizeof (SPacketHeader)) return 1;

    PacketHeader *header = (PacketHeader *) packetData;

    if (header->protocolID != PROTOCOL_ID) return 1;

    Version version = header->protocolVersion;
    if (version.major != PROTOCOL_VERSION.major) {
        fprintf(stderr,
                "[WARNING]: Received a packet with incompatible version: "
                "%d.%d (mine is %d.%d).\n",
                version.major, version.minor,
                PROTOCOL_VERSION.major, PROTOCOL_VERSION.minor);
        return 1;
    }

    if (header->packetType != S_PT_PLAYER_INPUT) {
        printf("[WARNING]: Ignoring a packet of unexpected type.\n");
        return 1;
    }

    if ((unsigned) packetSize < sizeof (PacketHeader) + sizeof (PlayerInputPacket)) {
        fprintf(stderr, "[WARNING]: Received a too small player input packet.\n");
        return 1;
    }

    return 0;   // packet is fine

}

void recievePackets (void) {

    unsigned char packetData[MAX_PACKET_SIZE];

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
            if (checkPacket (packetSize, packetData)) continue;

            // if the packet passes all the checks, we can use it safely
            else {
                PlayerInputPacket *playerInput = (PlayerInputPacket *) (packetData + sizeof (PacketHeader));
                handlePlayerInputPacket (from, playerInput);
            }

        }        
    }   

}

// this is used to clean disconnected players
void checkTimeouts (void) {

}

void sendGamePackets (int destPlayer) {

}

const double tickInterval = 1.0 / FPS;
double sleepTime = 0;
TimeSpec lastIterTime;

bool inGame = false;

void updateGame (void);

void gameLoop (void) {

    sleepTime = 0;
	lastIterTime = getTimeSpec ();

	while (inGame) {
        // get packets from players in the game lobby
        recievePackets ();

        // check for disconnected players
		checkTimeouts ();

        // update the game
		updateGame ();

        // send packets to connected players
		for (size_t p = 0; p < players.elements; p++) sendGamePackets (p);

		// self-adjusting sleep that makes the loop contents execute every TICK_INTERVAL seconds
		TimeSpec thisIterTime = getTimeSpec ();
		double timeSinceLastIter = timeElapsed (&lastIterTime, &thisIterTime);
		lastIterTime = thisIterTime;
		sleepTime += tickInterval - timeSinceLastIter;
		if (sleepTime > 0) sleepFor (sleepTime);

	}

}

// TODO: add support for multiple lobbys at the smae time
    // --> maybe create a different socket for each lobby?
u8 createLobby (void) {

    fprintf (stdout, "Creatting a new lobby...\n");

    // TODO: what about a new connection in a new socket??

    // TODO: set the server socket to no blocking and make sure we have a udp
    // connection!!

    // TODO: better manage who created the game lobby --> better players/clients management

    // TODO: don't forget that for creating many lobbys, we need individual game structs 
    // for each one!!!
    // init the necessary game structures
    vector_init (&players, sizeof (Player));

    // TODO: init the map and other game structures

    inGame = true;

    // TODO: maybe we want to get some game settings first??

    // start the game
    gameLoop ();

}

/*** THE ACTUAL GAME ***/

void updateGame (void) {

}