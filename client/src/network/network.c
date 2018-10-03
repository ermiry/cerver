#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netinet/in.h>

#include <errno.h>

#include "game.h"   // for types defs

#include "network/network.h"

#include "utils/myUtils.h"

// TODO: maybe load them from a cfg file???
// these must me the same that in the server!!!
// these 2 are used to manage the packets
ProtocolId PROTOCOL_ID = 0xEC3B5FA9; // Randomly chosen.
Version PROTOCOL_VERSION = { 1, 1 };

struct sockaddr_storage serverAddress;

#define PORT    9001

// FIXME:
// #define SERVER_ADDRESS  "192.168.1.7

// TODO: add the ability to read from a config file

// FIXME: hanlde errors in a message log in the main menu!!!

bool connected = false;

int clientSocket;

const char filepath[64] = "foo.txt";

/*** PACKETS ***/

// Most of the following functions are the same as in the server, but with some tweaks
// for example: as of 03/10/2018 -- we only send packets to the server and no to other players

#pragma region PACKETS

// FIXME:
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

    // FIXME: check for the appropiate type
    // if (header->packetType != PLAYER_INPUT_TYPE) {
    //     printf("[WARNING]: Ignoring a packet of unexpected type.\n");
    //     return 1;
    // }

    // FIXME: check for the appropiate type
    // if ((unsigned) packetSize < sizeof (PacketHeader) + sizeof (PlayerInputPacket)) {
    //     fprintf(stderr, "[WARNING]: Received a too small player input packet.\n");
    //     return 1;
    // }

    return 0;   // packet is fine

}

void recievePackets (void) {

}

void initPacketHeader (void *header, PacketType type) {

    PacketHeader h;
    h.protocolID = PROTOCOL_ID;
    h.protocolVersion = PROTOCOL_VERSION;
    h.packetType = type;

    memcpy (header, &h, sizeof (PacketHeader));

}

void sendPacket (void *begin, size_t packetSize, struct sockaddr_storage address) {

    ssize_t sentBytes = sendto (clientSocket, (const char *) begin, packetSize, 0,
		       (struct sockaddr *) &address, sizeof (struct sockaddr_storage));

	if (sentBytes < 0 || (unsigned) sentBytes != packetSize) 
        fprintf (stderr, "[ERROR]: Failed to send packet!\n");

}

// FIXME: to addrress
void sendGamePackets (int to) {

}

#pragma endregion

/*** REQUESTS ***/

#pragma region REQUESTS

int recieveFile (char *request) {

    // FIXME: make sure that we create a new file if it does not exists
    // prepare the file where we are copying to
    FILE *file = fopen (filepath, "w");
    if (file == NULL) {
        fprintf (stderr, "%s\n", strerror (errno));
        return 1;
    }

    if (write (clientSocket, request, strlen (request)) < 0) {
        fprintf (stderr, "Error on writing!\n\n");
        return 1; 
    }

    size_t len;
    char buffer[BUFSIZ];
    int fileSize;
    int remainData = 0;

    //fprintf(stdout, "\nFile size : %d\n", file_size);
    recv (clientSocket, buffer, BUFSIZ, 0);
    fileSize = atoi (buffer);
    fprintf (stdout, "\nRecieved file size : %d\n", fileSize);

    remainData = fileSize;

    // get the file data
    while (((len = recv (clientSocket, buffer, BUFSIZ, 0)) > 0) && (remainData > 0)) {
            fwrite (buffer, sizeof (char), len, file);
            remainData -= len;
            fprintf (stdout, "Received %ld bytes and we hope %d more bytes\n", len, remainData);
    }

    fprintf (stdout, "Done!");

    fclose (file);
    return 0;

}

// TODO: how can we handle other parameters for requests?
int makeRequest (RequestType requestType) {

    // 03/10/2018 -- old way for creating a request
    {
        /* char buffer[CLIENT_REQ_TYPE_SIZE];
        bzero (buffer, sizeof (buffer));

        char *request = itoa (requestType, buffer);

        fprintf (stdout, "Request: %s\n", request);
        if (write (clientSocket, request, strlen (request)) < 0) {
            fprintf (stderr, "Error on writing!\n\n");
            return 1; 
        } */
    }

    // 03/10/2018 -- new way for making a request
    // FIXME: refactor this code --> split it into diffrent functions
    // we know that a request packet is always of this size
    size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData);

    // buffer for packets
	void *packetBuffer = malloc (packetSize);

    void *begin = packetBuffer;
	char *end = begin; 

    // packet header
	PacketHeader *header = (PacketHeader *) end;
    end += sizeof (PacketHeader);
    initPacketHeader (header, REQUEST);

    // request data
    RequestData *data = (RequestData *) end;
    end += sizeof (RequestData);
    data->type = requestType;

    // TODO: maybe better error handling here?
    // check the packet
    // assert (end == (char *) begin + packetSize);

    // send the request to the server
    sendPacket (begin, packetSize, serverAddress);

    // wait for a response

    // TODO: handle the server response -- request type
    // switch (requestType) {
    //     case REQ_GET_FILE: break;
    //     case POST_SEND_FILE: break;

    //     case REQ_CREATE_LOBBY: 
    //         // we expect a response from the server with our new lobby
    //         break;

    //     default: fprintf (stderr, "Invalid request!\n"); break;
    // }

    return 0;   // success

}

#pragma endregion

/*** CLIENT ***/

#pragma region CLIENT LOGIC

// TODO: load settings form a cfg file
int initClient (void) {

    // create client socket
    int client = socket (AF_INET, SOCK_STREAM, 0);
    if (client < 0) fprintf (stderr, "[Error] Failed to create client socket!\n");

    return client;

}

// FIXME: do we need to set the socket to no blocking??
// TODO: maybe e can also load some settings from a cfg file
// the same way as we do it on the server
// TODO: log the process to the message log...
int connectToServer (void) {

    clientSocket = initClient ();

	memset (&serverAddress, 0, sizeof (struct sockaddr_storage));

    // 03/10/2018 -- new way
    // not use ipv6
    struct sockaddr_in *addr = (struct sockaddr_in *) &serverAddress;
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons (PORT);

    // old way
    // TODO: add ipv6 config option
    // TODO: add config options
    // serverAddress.sin_family = AF_INET;
    // // FIXME:
    // // inet_pton(AF_INET, SERVER_ADDRESS, &(remote_addr.sin_addr));
    // serverAddress.sin_addr.s_addr = INADDR_ANY;
    // serverAddress.sin_port = htons (PORT);

    // try to connect to the server
    // FIXME: add the ability to try multiple times

    if (connect (clientSocket, (struct sockaddr *) &serverAddress, sizeof (struct sockaddr)) < 0) {
        fprintf (stderr, "%s\n", strerror (errno));
        fprintf (stderr, "[ERROR]: Failed to connect to server!\n");
    }

    // we expect a welcome message from the server...
    char serverResponse [256];
    recv (clientSocket, &serverResponse, sizeof (serverResponse), 0);
    printf ("\n%s\n", serverResponse);

    connected = true;

    // return 0 on success
    return 0;

}

// TODO: wrap all necessary things before clossing connection
void disconnectFromServer (void) {

    close (clientSocket);

}

#pragma endregion