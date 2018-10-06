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
// these 2 are used to manage the packets
ProtocolId PROTOCOL_ID = 0x4CA140FF; // randomly chosen
Version PROTOCOL_VERSION = { 1, 1 };

// TODO: better id handling and management
u16 nextPlayerId = 0;

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
ssize_t lobbyPacketSize;
ssize_t updatedGamePacketSize;
ssize_t playerInputPacketSize;

// check for packets with bad size, protocol, version, etc
u8 checkPacket (ssize_t packetSize, unsigned char *packetData, PacketType type) {

    if ((unsigned) packetSize < sizeof (PacketHeader)) {
        logMsg (stderr, WARNING, PACKET, "Recieved a to small packet!");
        return 1;
    } 

    PacketHeader *header = (PacketHeader *) packetData;

    if (header->protocolID != PROTOCOL_ID) {
        logMsg (stderr, WARNING, PACKET, "Packet with unknown protocol ID.");
        return 1;
    }

    Version version = header->protocolVersion;
    if (version.major != PROTOCOL_VERSION.major) {
        logMsg (stderr, WARNING, PACKET, "Packet with incompatible version.");
        return 1;
    }

    switch (type) {
        case REQUEST: 
            if (packetSize < requestPacketSize) {
                logMsg (stderr, WARNING, PACKET, "Received a too small request packet.");
                return 1;
            } break;
        case GAME_UPDATE_TYPE: 
            if (packetSize < updatedGamePacketSize) {
                logMsg (stderr, WARNING, PACKET, "Received a too small game update packet.");
                return 1;
            } break;
        case PLAYER_INPUT_TYPE: 
            if (packetSize < playerInputPacketSize) {
                logMsg (stderr, WARNING, PACKET, "Received a too small player input packet.");
                return 1;
            } break;

        // TODO:
        case TEST_PACKET_TYPE: break;
        
        default: logMsg (stderr, WARNING, PACKET, "Got a pakcet of incompatible type."); return 1;
    }

    return 0;   // packet is fine

}

// FIXME: when the owner of the lobby has quit the game, we ned to stop listenning for that lobby
    // also destroy the lobby and all its mememory
// Also check if there are no players left in the lobby for any reason
// this is used to recieve packets when in a lobby/game
void recievePackets (void) {

    /*unsigned char packetData[MAX_UDP_PACKET_SIZE];

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
    } */ 

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
void sendPacket (Server *server, void *begin, size_t packetSize, struct sockaddr_storage address) {

    ssize_t sentBytes = sendto (server->serverSock, (const char *) begin, packetSize, 0,
		       (struct sockaddr *) &address, sizeof (struct sockaddr_storage));

	if (sentBytes < 0 || (unsigned) sentBytes != packetSize)
        logMsg (stderr, ERROR, NO_TYPE, "Failed to send packet!") ;

}

#pragma endregion

/*** REQUESTS ***/

// All clients can make any request, but if they make a game request, 
// they are now treated as players...

#pragma region REQUESTS


#pragma endregion

/*** CLIENTS ***/

#pragma region CLIENTS

// FIXME: we can't have infinite ids!!
u32 nextClientID = 0;

Client *newClient (i32 sock, struct sockaddr_storage address) {

    Client *new = (Client *) malloc (sizeof (Client));
    
    new->clientID = nextClientID;
    new->clientSock = sock;
    new->address = address;

    return new;

}

// TODO: hanlde a max number of clients connected to a server at the same time?
// maybe this can be handled before this call by the load balancer
void registerClient (Server *server, Client *client) {

    vector_push (&server->clients, client);

}

// FIXME: where do we want to disconnect the client?
// TODO: removes a client from the server 
void unregisterClient (Server *server, Client *client) {

}

#pragma endregion

/*** SERVER ***/

// Here we manage the creation and destruction of servers 
#pragma region SERVER LOGIC

// FIXME: add a cfg value to set the protocol
// FIXME: fix ip address

// TODO: set to non blocking mode for handling the game???
// if (!sock_setNonBlocking (server))
    // die ("\n[ERROR]: Failed to set server socket to non-blocking mode!\n");

const char welcome[64] = "Welcome to cerver!";

// init server type independent data structs
void initServerDS (Server *server, ServerType type) {

    switch (type) {
        case FILE_SERVER: break;
        case WEB_SERVER: break;
        case GAME_SERVER: {
            vector_init (&server->clients, sizeof (Client));

            GameServerData *data = (GameServerData *) server->serverData;
            vector_init (&data->players, sizeof (Player));
            vector_init (&data->lobbys, sizeof (Lobby));
        } break;
        default: break;
    }

}

// depending on the type of server, we need to init some const values
void initServerValues (Server *server, ServerType type) {

    // this are used in all the types
    packetHeaderSize = sizeof (PacketHeader);
    requestPacketSize = sizeof (RequestData);

    switch (type) {
        case FILE_SERVER: break;
        case WEB_SERVER: break;
        case GAME_SERVER: {
            GameServerData *data = (GameServerData *) server->serverData;

            data->lobbyPacketSize = sizeof (lobbyPacketSize);
            data->updatedGamePacketSize = sizeof (UpdatedGamePacket);
            data->playerInputPacketSize = sizeof (PlayerInputPacket);
        } break;
        default: break;
    }

}

// init a server of a given type
i8 initServer (Server *server, Config *cfg, ServerType type) {

	ConfigEntity *cfgEntity = getEntityWithId (cfg, type);
	if (!cfgEntity) {
        logMsg (stderr, ERROR, SERVER, "Problems with server config!");
        return -1;
    } 

	server->useIpv6 = atoi (getEntityValue (cfgEntity, "ipv6"));
    server->serverSock = socket ((server->useIpv6 == 1 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
    if (server->serverSock < 0) {
        logMsg (stderr, ERROR, SERVER, "Failed to create server socket!");
        return -1;
    }

    struct sockaddr_storage address;
	memset(&address, 0, sizeof (struct sockaddr_storage));

	server->port = atoi (getEntityValue (cfgEntity, "port"));
	if (server->useIpv6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &address;
		addr->sin6_family = AF_INET6;
		addr->sin6_addr = in6addr_any;
		addr->sin6_port = htons (server->port);
	} 

    else {
		struct sockaddr_in *addr = (struct sockaddr_in *) &address;
		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = INADDR_ANY;
		addr->sin_port = htons (server->port);
	}

    if (bind (server, (const struct sockaddr *) &address, sizeof (struct sockaddr)) < 0) {
        logMsg (stderr, ERROR, SERVER, "Failed to bind server socket!");
        return -1;
    }   

    server->connectionQueue = atoi (getEntityValue (cfgEntity, "queue"));

    initServerDS (server, type);
    initServerValues (server, type);

    server->type = type;

	return 0;   // success

}

Server *newServer (void) {

    Server *new = (Server *) malloc (sizeof (Server));
    return new;

}

// TODO: maybe we can pass other parameters and if we don't have any, use the default
// configuration from the cfg file
Server *createServer (ServerType type) {

    Server *s = newServer ();

    Config *serverConfig = parseConfigFile ("./config/server.cfg");
    if (!serverConfig) {
        logMsg (stderr, ERROR, NO_TYPE, "Problems loading server config!\n");
        return NULL;
    } 

    else {
        if (!initServer (s, serverConfig, type)) {
            logMsg (stdout, SUCCESS, SERVER, "\nCreated a new server!\n");

            // we don't need the server config anymore
            clearConfig (serverConfig);

            return s;
        }
    }

}

// FIXME: clean up all the game structs
void destroyGameServer (Server *server) {
}

// FIXME: disconnect any remainning client from the server
// FIXME: delete the common struct between servers
// close the server
u8 teardown (Server *server) {

    if (!server) {
        logMsg (stdout, ERROR, SERVER, "Can't destroy a NULL server!");
        return 1;
    }

    logMsg (stdout, SERVER, NO_TYPE, "Closing server...");

    // clean up the server, it depends on its type
    switch (server->type) {
        case FILE_SERVER: break;
        case WEB_SERVER: break;
        case GAME_SERVER: destroyGameServer (server); break;
        default: break;
    }

    // close the connection
    if (close (server->serverSock) < 0) {
        logMsg (stdout, ERROR, SERVER, "Failed to close server socket!");
        free (server);
        return 1;
    }

    free (server);

    return 0;   // teardown was successfull

}

// destroys the server and create a new one of the same type
Server *restartServer (Server *server) {

    if (server != NULL) {
        ServerType type = server->type;

        if (!teardown (server)) {
            Config *serverConfig = parseConfigFile ("./config/server.cfg");
            if (!serverConfig) logMsg (stderr, ERROR, SERVER, "Problems loading server config!");
            else {
                Server *retServer = newServer ();

                // init our server as a game server
                if (!initServer (retServer, serverConfig, type)) {
                    logMsg (stdout, SUCCESS, SERVER, "\nServer has restarted!\n");

                    // we don't need the server config anymore
                    clearConfig (serverConfig);

                    return retServer;
                }

                else return NULL;
            }
        }
    }

    return NULL;

}

#pragma endregion

// Here goes the logic for handling new connections and manage client requests
// also here we manage some server logic and server responses
#pragma region CONNECTION HANDLER

// TODO: check again the recieve packets function --> do we need also to pass
    // the sockaddr? or hanlde just the socket?

// FIXME: don't forget to check that tha packet type is a request!!!
// TODO: how can we handle other parameters for requests?
/* void connectionHandler (Server *server, i32 client, struct sockaddr_storage clientAddres) {

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
                case REQ_GET_FILE: logMsg (stdout, REQ, FILE_REQ, "Requested a file."); break;
                case POST_SEND_FILE: logMsg (stdout, REQ, FILE_REQ, "Client wants to send a file."); break;

                case REQ_CREATE_LOBBY: createLobby (server, client, clientAddres); break;

                case REQ_TEST:  logMsg (stdout, TEST, NO_TYPE, "Packet recieved correctly!!"); break;

                default: logMsg (stderr, ERROR, REQ, "Invalid request type!");
            }
        }
            
        else logMsg (stderr, ERROR, REQ, "Recieved an invalid request packet from the client!");
    }

    // FIXME: better error handling I guess...
    else logMsg (stderr, ERROR, REQ, "No client request!");

}*/

// this a multiplexor to handle data transmissios to clients
void *connectionHandler (void *data) {

    Server *server = (Server *) data;

    if (server->clients.elements > 0) {
        // TODO: handle client requests

        switch (server->type) {
            case FILE_SERVER: break;
            case WEB_SERVER: break;
            case GAME_SERVER: {
                GameServerData *data = (GameServerData *) server->serverData;

                // TODO: update each game lobby
                if (data->lobbys.elements > 0) {

                }
            } break;

            default: break;
        }
    } 

}

// 04/10/2018 -- 22:56
// TODO: maybe a way to handle multiple clients, is having an array of clients, and each time
// we get a new connection we add the client to the struct and assign a new connection handler to it
// NOTE: not all clients are players, so we can handle anyone that connects as a client, 
// and if they make a game request, we can now treat them as players...

// FIXME: try a similar like in the recieve packets function --> maybe with that,
// we can handle different client requests at the same time
// TODO: handle ipv6 configuration
// TODO: do we need to hanlde time here?
void listenForConnections (Server *server) {

	listen (server->serverSock, server->connectionQueue);

    Client *client = NULL;
    i32 clientSocket;
	struct sockaddr_storage clientAddress;
	memset (&clientAddress, 0, sizeof (struct sockaddr_storage));
    socklen_t sockLen = sizeof (struct sockaddr_storage);

	while ((clientSocket = accept (server, (struct sockaddr *) &clientAddress, &sockLen))) {
        // register the client to correct server
        client = newClient (clientSocket, clientAddress);
        registerClient (server, client);

        // TODO: log to which server it connects and maybe from where
        logMsg (stdout, SERVER, NO_TYPE, "New client connected.");
	}

}

#pragma endregion

/*** GAME SERVER ***/

#pragma region GAME SERVER

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
    logMsg (stdout, SERVER, PLAYER, createString ("New player connected from ip: %s @ port: %d.\n", 
        addrStr, sock_ip_port ((struct sockaddr *) &address)));

    // TODO: init other necessarry game values
    // add the new player to the game
    // Player newPlayer;
    // newPlayer.id = nextPlayerId;
    // newPlayer.address = address;

    // vector_push (&players, &newPlayer);

    // FIXME: this is temporary
    // spawnPlayer (&newPlayer);

}

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

// FIXME:
// TODO: maybe we can clean and refactor this?
// creates and sends game packets
void sendGamePackets (Server *server, int to) {

    Player *destPlayer = vector_get (&players, to);

    // first we need to prepare the packet...

    // TODO: clean this a little, but don't forget this can be dynamic!!
	size_t packetSize = packetHeaderSize + updatedGamePacketSize +
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
    end += updatedGamePacketSize;
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
    sendPacket (server, begin, packetSize, destPlayer->address);

}

// TODO: maybe each server has its warray of lobbys and it uses it to handle new lobby requests and
// to manage lobby data
// we need to be sure that a player is not the owner of more than one lobby at the time and 
// somethings like that

// TODO: get the owner and the type of lobby
void createLobby (Server *server, i32 client, struct sockaddr_storage clientAddres) {

    // TODO:
    // we need to check if we are available to create a new lobby, to prevent 
    // a million requests to create a lobby and we get destroyed

    // TODO: if the client is not registered yet as an active player, 
    // create a new player struct

    Player *owner = (Player *) malloc (sizeof (Player));
    owner->id = nextPlayerId;
    nextPlayerId++;
    owner->address = clientAddres;

    Lobby *lobby = newLobby (owner);
    if (lobby != NULL) {
        logMsg (stdout, GAME, NO_TYPE, "New lobby created.");

        // send the lobby info to the owner
        size_t packetSize = packetHeaderSize + lobbyPacketSize;
        
        void *packetBuffer = malloc (packetSize);
        void *begin = packetBuffer;
        char *end = begin; 

        // packet header
        PacketHeader *header = (PacketHeader *) end;
        end += sizeof (PacketHeader);
        initPacketHeader (header, CREATE_GAME);

        // 04/10/2018 -- 22:28
        // TODO: do we need to serialize this data?
        // TODO: does ptrs work? or do we have to send data without ptrs??
        // lobby data
        Lobby *lobbyData = (Lobby *) end;
        end += lobbyPacketSize;

        lobbyData->settings = lobby->settings;
        // FIXME: how do we get this owner?
        lobbyData->owner = NULL;

        // after the pakcet has been prepare, send it to the dest player...
        sendPacket (server, begin, packetSize, lobby->owner->address);

        // TODO: do we want to do this using a request?
        // FIXME: we need to wait for an ack of the ownwer and then we can do this...
        // the ack is when the player is ready in its lobby screen, and only then we can
        // handle requests from other players to join

        // TODO: we can now wait for more players to join the lobby...

        // TODO: make sure that all the players have the same game settings, 
        // so send to them that infofprintf (stdout, "[GAME]: !\n"); when they connect!!
    }

    else {
        logMsg (stderr, ERROR, GAME, "Failed to create a new game lobby.");

        // FIXME: send an error to the player to hanlde it
    } 

}

#pragma endregion