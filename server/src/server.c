#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "server.h"
#include "network.h"

#include "game.h"

#include <utils/log.h>
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

// FIXME:
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

void initPacketHeader (void *header, PacketType type) {

    PacketHeader h;
    h.protocolID = PROTOCOL_ID;
    h.protocolVersion = PROTOCOL_VERSION;
    h.packetType = type;

    memcpy (header, &h, sizeof (PacketHeader));

}

// sends a packet from a server to the client address
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
    // new->address = address;

    return new;

}

// 13/10/2018 - I think register a client only works for tcp? but i might be wrong...

// TODO: hanlde a max number of clients connected to a server at the same time?
// maybe this can be handled before this call by the load balancer
void registerClient (Server *server, Client *client) {

    vector_push (&server->clients, client);

}

// sync disconnection
// when a clients wants to disconnect, it should send us a request to disconnect
// so that we can clean the correct structures

// if there is an async disconnection from the client, we need to have a time out
// that automatically clean up the clients if we do not get any request or input from them

// FIXME: where do we want to disconnect the client?
// TODO: removes a client from the server 
void unregisterClient (Server *server, Client *client) {

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

	while ((clientSocket = accept (server->serverSock, (struct sockaddr *) &clientAddress, &sockLen))) {
        // register the client to correct server
        client = newClient (clientSocket, clientAddress);
        registerClient (server, client);

        // TODO: log to which server it connects and maybe from where
        logMsg (stdout, SERVER, NO_TYPE, "New client connected.");
	}

}

#pragma endregion

/*** SERVER ***/

// Here we manage the creation and destruction of servers 
#pragma region SERVER LOGIC

// FIXME: fix ip address

// TODO: set to non blocking mode for handling the game???
// if (!sock_setNonBlocking (server))
    // die ("\n[ERROR]: Failed to set server socket to non-blocking mode!\n");

const char welcome[64] = "Welcome to cerver!";

// init server type independent data structs
void initServerDS (Server *server, ServerType type) {

    // all the server types have a client's vector
    vector_init (&server->clients, sizeof (Client));

    switch (type) {
        case FILE_SERVER: break;
        case WEB_SERVER: break;
        case GAME_SERVER: {
            GameServerData *data = (GameServerData *) malloc (sizeof (GameServerData));
            vector_init (&data->players, sizeof (Player));
            vector_init (&data->lobbys, sizeof (Lobby));

            server->serverData = data;
        } break;
        default: break;
    }

}

// TODO: 19/10/2018 -- do we need to have the expected size of each server type
// inside the server structure?
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

// TODO: option to toggle debug information...
// init a server of a given type
u8 initServer (Server *server, Config *cfg, ServerType type) {

    if (!server) {
        logMsg (stderr, ERROR, SERVER, "Can't init a NULL server!");
        return 1;
    }

    logMsg (stdout, DEBUG, SERVER, "Init server...");

    if (cfg) {
        ConfigEntity *cfgEntity = getEntityWithId (cfg, type);
        if (!cfgEntity) {
            logMsg (stderr, ERROR, SERVER, "Problems with server config!");
            return 1;
        } 

        logMsg (stdout, DEBUG, SERVER, "Using config entity to set server values...");

        char *ipv6 = getEntityValue (cfgEntity, "ipv6");
        if (ipv6) {
            server->useIpv6 = atoi (ipv6);
            // if we have got an invalid value, the default is not to use ipv6
            if (server->useIpv6 != 0 || server->useIpv6 != 1) server->useIpv6 = 0;
        } 
        // if we do not have a value, use the default
        else server->useIpv6 = 0;

        logMsg (stdout, DEBUG, SERVER, createString ("Use IPv6: %i", server->useIpv6));

        char *tcp = getEntityValue (cfgEntity, "tcp");
        if (tcp) {
            u8 usetcp = atoi (tcp);
            if (usetcp != 0 || usetcp != 1) {
                logMsg (stdout, WARNING, SERVER, "Unknown protocol. Using default: tcp protocol");
                usetcp = 1;
            }

            if (usetcp) server->protocol = IPPROTO_TCP;
            else server->protocol = IPPROTO_UDP;

        }
        // set to default (tcp) if we don't found a value
        else {
            logMsg (stdout, WARNING, SERVER, "No protocol found. Using default: tcp protocol");
            server->protocol = IPPROTO_TCP;
        }

        char *port = getEntityValue (cfgEntity, "port");
        if (port) {
            server->port = atoi (port);
            // check that we have a valid range, if not, set to default port
            if (server->port <= 0 || server->port >= MAX_PORT_NUM) {
                logMsg (stdout, WARNING, SERVER, 
                    createString ("Invalid port number. Setting port to default value: %i", DEFAULT_PORT));
                server->port = DEFAULT_PORT;
            }

            logMsg (stdout, DEBUG, SERVER, createString ("Listening on port: %i", server->port));
        }
        // set to default port
        else {
            logMsg (stdout, WARNING, SERVER, 
                createString ("No port found. Setting port to default value: %i", DEFAULT_PORT));
            server->port = DEFAULT_PORT;
        } 

        char *queue = getEntityValue (cfgEntity, "queue");
        if (queue) {
            server->connectionQueue = atoi (queue);
            logMsg (stdout, DEBUG, SERVER, createString ("Connection queue: %i", server->connectionQueue));
        } 
        else {
            logMsg (stdout, WARNING, SERVER, 
                createString ("Connection queue no specified. Setting it to default: %i", DEFAULT_CONNECTION_QUEUE));
            server->connectionQueue = DEFAULT_CONNECTION_QUEUE;
        }
    }

    // logMsg (stdout, DEBUG, SERVER, "Done loading server values. Creating socket...");

    // init the server
    switch (server->protocol) {
        case IPPROTO_TCP: 
            server->serverSock = socket ((server->useIpv6 == 1 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
            break;
        case IPPROTO_UDP:
            server->serverSock = socket ((server->useIpv6 == 1 ? AF_INET6 : AF_INET), SOCK_DGRAM, 0);
            break;

        default:
            logMsg (stderr, ERROR, SERVER, "Unkonw protocol type!"); return 1;
    }
    
    if (server->serverSock < 0) {
        logMsg (stderr, ERROR, SERVER, "Failed to create server socket!");
        return 1;
    }

    logMsg (stdout, DEBUG, SERVER, "Created server socket");

    struct sockaddr_storage address;
	memset (&address, 0, sizeof (struct sockaddr_storage));

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

    if ((bind (server->serverSock, (const struct sockaddr *) &address, sizeof (struct sockaddr_storage))) < 0) {
        logMsg (stderr, ERROR, SERVER, "Failed to bind server socket!");
        return 1;
    }   

    initServerDS (server, type);
    initServerValues (server, type);
    logMsg (stdout, DEBUG, SERVER, "Done creating server data structures...");

    server->type = type;

	return 0;   // success

}

// this is like a constructor
Server *newServer (Server *server) {

    Server *new = (Server *) malloc (sizeof (Server));

    // init with some values
    if (server) {
        new->useIpv6 = server->useIpv6;
        new->protocol = server->protocol;
        new->port = server->port;
        new->connectionQueue = server->connectionQueue;
    }
    
    return new;

}

Server *createServer (Server *server, ServerType type, void (*destroyServerdata) (void *data)) {

    // create a server with the request parameters
    if (server) {
        Server *s = newServer (server);
        if (!initServer (s, NULL, type)) {
            s->destroyServerdata = destroyServerdata;
            logMsg (stdout, SUCCESS, SERVER, "\nCreated a new server!\n");
            return s;
        }

        else {
            logMsg (stderr, ERROR, SERVER, "Failed to init the server!");
            free (s);   // delete the failed server...
            return NULL;
        }
    }

    // create the server from the default config file
    else {
        Config *serverConfig = parseConfigFile ("./config/server.cfg");
        if (!serverConfig) {
            logMsg (stderr, ERROR, NO_TYPE, "Problems loading server config!\n");
            return NULL;
        } 

        else {
            Server *s = newServer (NULL);
            if (!initServer (s, serverConfig, type)) {
                s->destroyServerdata = destroyServerdata;
                logMsg (stdout, SUCCESS, SERVER, "Created a new server!\n");
                // we don't need the server config anymore
                clearConfig (serverConfig);
                return s;
            }

            else {
                logMsg (stderr, ERROR, SERVER, "Failed to init the server!");
                clearConfig (serverConfig);
                free (s);   // delete the failed server...
                return NULL;
            }
        }
    }

}

// teardowns the server and creates a fresh new one with the same parameters
Server *restartServer (Server *server) {

    if (server) {
        Server temp = { 
            .useIpv6 = server->useIpv6, .protocol = server->protocol, .port = server->port,
            .connectionQueue = server->connectionQueue, .type = server->type };

        if (!teardown (server)) logMsg (stdout, SUCCESS, SERVER, "Done with server teardown");
        else logMsg (stderr, ERROR, SERVER, "Failed to teardown the server!");

        // what ever the output, create a new server --> restart
        Server *retServer = newServer (&temp);
        if (!initServer (retServer, NULL, temp.type)) {
            logMsg (stdout, SUCCESS, SERVER, "\nServer has restarted!\n");
            return retServer;
        }
        else {
            logMsg (stderr, ERROR, SERVER, "Unable to retstart the server!");
            return NULL;
        }
    }

    else {
        logMsg (stdout, WARNING, SERVER, "Can't restart a NULL server!");
        return NULL;
    }

}

// TODO: dont forget to put server->running = true;
// TODO: 13/10/2018 -- we can only handle a tcp server
// depending on the protocol, the logic of each server might change...
void startServer (Server *server) {

    switch (server->protocol) {
        case IPPROTO_TCP: 
            // create a thread to listen for connections
            // the main thread will handle the in server logic, it depends on the server type
            break;
        case IPPROTO_UDP: break;

        default: break;
    }

}

// TODO: what other logic will we need to handle? -> how to handle players / clients timeouts?
// what happens with the current lobbys or on going games??
// disable socket I/O in both ways
void shutdownServer (Server *server ) {

    if (server->running) {
        if (shutdown (server->serverSock, SHUT_RDWR) < 0) 
            logMsg (stderr, ERROR, SERVER, "Failed to shutdown the server!");

        else server->running = false;
    }

}

// FIXME: 
// cleans up all the game structs like lobbys and in game structures like maps
// if there are players connected, it sends a req to disconnect 
void destroyGameServer (void *data) {

    GameServerData *gameData = (GameServerData *) data;

    // clean any on going games...
    if (gameData->lobbys.elements > 0) {
        Lobby *lobby = NULL;
        for (size_t i_lobby = 0; i_lobby < gameData->lobbys.elements; i_lobby++) {
            lobby = vector_get (&gameData->lobbys, i_lobby);
            // TODO: check if we have players inside the lobby
            // if so... send them a msg to disconnect so that they can handle the disconnect
            // logic on their side, and disconnect each client
            for (size_t i_player = 0; i_player < lobby->players.elements; i_player++) {

            }

            // the clients are disconnected, so delete each client struct
            while (lobby->players.elements > 0) {

            }

            // TODO: for each lobby check if we have an in game going
            // if so... clean up the necessary data structures
            if (lobby->inGame) {

            }
        }

        // we can now safely delete each lobby structure
        while (gameData->lobbys.elements > 0) {

        }
    }

}

// FIXME:
// cleans up the client's structure in the current server
// if ther are clients connected, it sends a req to disconnect
void cleanUpClients (Server *server) {

    if (server->clients.elements > 0) {
        Client *client = NULL;
        
        for (size_t i_client = 0; i_client < server->clients.elements; i_client++) {
            // TODO: correctly disconnect every client
        }

        // TODO: delete the client structs
        while (server->clients.elements > 0) {
            
        }
    }

    // TODO: make sure destroy the client vector

}

// FIXME: we need to join the ongoing threads... 
// teardown a server
u8 teardown (Server *server) {

    if (!server) {
        logMsg (stdout, ERROR, SERVER, "Can't destroy a NULL server!");
        return 1;
    }

    logMsg (stdout, SERVER, NO_TYPE, "Init server teardown...");

    // disable socket I/O in both ways
    shutdownServer (server);

    // TODO: join the on going threads?? -> just the listen ones?? or also the ones handling the lobby?

    // clean common server structs
    cleanUpClients (server);

    // clean independent server type structs
    if (server->destroyServerdata) server->destroyServerdata (server->serverData);

    // close the server socket
    if (close (server->serverSock) < 0) {
        logMsg (stdout, ERROR, SERVER, "Failed to close server socket!");
        free (server);
        return 1;
    }

    free (server);

    logMsg (stdout, SUCCESS, NO_TYPE, "Server teardown was successfull!");

    return 0;   // teardown was successfull

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