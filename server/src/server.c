#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "server.h"
#include "network.h"

#include "utils/config.h"

/*** VALUES ***/

// TODO: maybe move these to the config file??
// FIXME: choose wisely...
// these 2 are used to manage the packets
ProtocolId PROTOCOL_ID = 0xEC3B5FA9; // Randomly chosen.
Version PROTOCOL_VERSION = {7, 0};

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

	// return the port we are listening to upon success
	return port;

}

void connectionHandler (i32 client) {

	// send welcome message
	send (client, welcome, sizeof (welcome), 0);

	// handle client request type
	i16 readSize = 0;
	char clientReq[CLIENT_REQ_TYPE_SIZE];

	// FIXME: 29/09/2018 -- we can only hanlde ONE request each connection
	if (readSize = recv (client, clientReq, CLIENT_REQ_TYPE_SIZE, 0) > 0) {
		u8 request = atoi (clientReq);

		switch (request) {
			case REQ_GET_FILE: fprintf (stdout, "[REQ]: Get File.\n"); break;
			case POST_SEND_FILE: fprintf (stdout, "[POST]: Send File.\n"); break;

			case REQ_CREATE_LOBBY: fprintf (stdout, "[REQ]: Create new game lobby.\n"); break;

			// TODO: send an error to the client
			default: fprintf (stderr, "[WARNING]: Invalid request type: %i", request); break;
		}
	}

	else fprintf (stdout, "No client request.\n");

	close (client);

}

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

#include "game.h"

// TODO:
void handlePlayerInputPacket (struct sockaddr_storage from, PlayerInputPacket *playerInput) {

    // TODO: add players only from the game lobby before the game inits!!
    // check if we have the player already registerd
    // if not, add it to the game

    // handle the player input

}

// check for packets with bad size, protocol, version, etc
u8 checkPacket (ssize_t packetSize, unsigned char packetData[MAX_UDP_PACKET_SIZE]) {

    if ((unsigned) packetSize < sizeof (PacketHeader)) return 1;

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

#pragma endregion