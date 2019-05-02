#ifndef _GNU_SOURCE
    #define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#include <poll.h>
#include <errno.h>

#include "types/myTypes.h"
#include "types/myString.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/game/game.h"

#include "utils/thpool.h"

#include "utils/vector.h"
#include "collections/avl.h" 

#include "utils/log.h"
#include "utils/config.h"
#include "utils/myUtils.h"
#include "utils/sha-256.h"

/*** VALUES ***/

// these 2 are used to manage the packets
ProtocolId PROTOCOL_ID = 0x4CA140FF; // randomly chosen
Version PROTOCOL_VERSION = { 1, 1 };

/*** SERIALIZATION ***/

#include <stddef.h>
#include <stdint.h>
#include <assert.h>

#pragma region SERIALIZATION 

typedef int32_t SRelativePtr;

void s_ptr_to_relative (void *relative_ptr, void *pointer) {

	SRelativePtr result = (char *) pointer - (char *) relative_ptr;
	memcpy (relative_ptr, &result, sizeof (result));

}

void *s_relative_to_ptr (void *relative_ptr) {

	SRelativePtr offset;
	memcpy (&offset, relative_ptr, sizeof (offset));
	return (char *) relative_ptr + offset;

}

bool s_relative_valid (void *relative_ptr, void *valid_begin, void *valid_end) {

	void *pointee = s_relative_to_ptr (relative_ptr);
	return pointee >= valid_begin && pointee < valid_end;
    
}

bool s_array_valid (void *array, size_t elem_size, void *valid_begin, void *valid_end) {

	void *begin = s_relative_to_ptr ((char *) array + offsetof (SArray, begin));

	SArray array_;
	memcpy (&array_, array, sizeof(array_));
	void *end = (char *) begin + elem_size * array_.n_elems;

	return array_.n_elems == 0
		|| (begin >= valid_begin && begin < valid_end &&
		    end >= valid_begin && end <= valid_end);

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

// FIXME: object pool!!
PacketInfo *newPacketInfo (Server *server, Client *client, i32 clientSock,
    char *packetData, size_t packetSize) {

    PacketInfo *p = (PacketInfo *) malloc (sizeof (PacketInfo));

    // if (server->packetPool) {
    //     if (POOL_SIZE (server->packetPool) > 0) {
    //         p = pool_pop (server->packetPool);
    //         if (!p) p = (PacketInfo *) malloc (sizeof (PacketInfo));
    //         else if (p->packetData) free (p->packetData);
    //     }
    // }

    // else {
    //     p = (PacketInfo *) malloc (sizeof (PacketInfo));
    //     p->packetData = NULL;
    // } 

    if (p) {
        p->packetData = NULL;

        p->server = server;

        if (!client) printf ("pack info - got a NULL client!\n");

        p->client = client;
        p->clientSock = clientSock;
        p->packetSize = packetSize;
        
        // copy the contents from the entry buffer to the packet info
        if (!p->packetData)
            p->packetData = (char *) calloc (MAX_UDP_PACKET_SIZE, sizeof (char));

        memcpy (p->packetData, packetData, MAX_UDP_PACKET_SIZE);
    }

    return p;

}

// used to destroy remaining packet info in the pools
void destroyPacketInfo (void *data) {

    if (data) {
        PacketInfo *packet = (PacketInfo *) data;
        packet->server= NULL;
        packet->client = NULL;
        if (packet->packetData) free (packet->packetData);
    }

}

// check for packets with bad size, protocol, version, etc
u8 checkPacket (size_t packetSize, char *packetData, PacketType expectedType) {

    if (packetSize < sizeof (PacketHeader)) {
        #ifdef CERVER_DEBUG
        logMsg (stderr, WARNING, PACKET, "Recieved a to small packet!");
        #endif
        return 1;
    } 

    // our clients must send us a packet with the cerver;s packet header to be valid
    PacketHeader *header = (PacketHeader *) packetData;

    if (header->protocolID != PROTOCOL_ID) {
        #ifdef CERVER_DEBUG
        logMsg (stdout, WARNING, PACKET, "Packet with unknown protocol ID.");
        #endif
        return 1;
    }

    // FIXME: error when sending auth packet from client!
    // Version version = header->protocolVersion;
    // if (version.major != PROTOCOL_VERSION.major) {
    //     #ifdef CERVER_DEBUG
    //     logMsg (stdout, WARNING, PACKET, "Packet with incompatible version.");
    //     #endif
    //     return 1;
    // }

    // compare the size we got from recv () against what is the expected packet size
    // that the client created 
    if ((u32) packetSize != header->packetSize) {
        #ifdef CERVER_DEBUG
        logMsg (stdout, WARNING, PACKET, "Recv packet size doesn't match header size.");
        logMsg (stdout, DEBUG_MSG, PACKET, 
            createString ("Recieved size: %i - Expected size %i", packetSize, header->packetSize));
        #endif
        return 1;
    } 

    if (expectedType != DONT_CHECK_TYPE) {
        // check if the packet is of the expected type
        if (header->packetType != expectedType) {
            #ifdef CERVER_DEBUG
            logMsg (stdout, WARNING, PACKET, "Packet doesn't match expected type.");
            #endif
            return 1;
        }
    } 

    return 0;   // packet is fine

}

// THIS IS ONLY FOR UDP
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

void initPacketHeader (void *header, PacketType type, u32 packetSize) {

    PacketHeader h;
    h.protocolID = PROTOCOL_ID;
    h.protocolVersion = PROTOCOL_VERSION;
    h.packetType = type;
    h.packetSize = packetSize;

    memcpy (header, &h, sizeof (PacketHeader));

}

// generates a generic packet with the specified packet type
void *generatePacket (PacketType packetType, size_t packetSize) {

    size_t packet_size;
    if (packetSize > 0) packet_size = packetSize;
    else packet_size = sizeof (PacketHeader);

    PacketHeader *header = (PacketHeader *) malloc (packet_size);
    initPacketHeader (header, packetType, packet_size); 

    return header;

}

// TODO: 06/11/2018 -- test this!
i8 udp_sendPacket (Server *server, const void *begin, size_t packetSize, 
    const struct sockaddr_storage address) {

    ssize_t sent;
    const void *p = begin;
    while (packetSize > 0) {
        sent = sendto (server->serverSock, begin, packetSize, 0, 
            (const struct sockaddr *) &address, sizeof (struct sockaddr_storage));
        if (sent <= 0) return -1;
        p += sent;
        packetSize -= sent;
    }

    return 0;

}

i8 tcp_sendPacket (i32 socket_fd, const void *begin, size_t packetSize, int flags) {

    ssize_t sent;
    const void *p = begin;
    while (packetSize > 0) {
        sent = send (socket_fd, p, packetSize, flags);
        if (sent <= 0) return -1;
        p += sent;
        packetSize -= sent;
    }

    return 0;

}

// handles the correct logic for sending a packet, depending on the supported protocols
u8 server_sendPacket (Server *server, i32 socket_fd, struct sockaddr_storage address, 
    const void *packet, size_t packetSize) {

    if (server) {
        switch (server->protocol) {
            case IPPROTO_TCP: 
                if (tcp_sendPacket (socket_fd, packet, packetSize, 0) >= 0)
                    return 0;
                break;
            case IPPROTO_UDP:
                if (udp_sendPacket (server, packet, packetSize, address) >= 0)
                    return 0;
                break;
            default: break;
        }
    }

    return 1;

}

// just creates an erro packet -> used directly when broadcasting to many players
void *generateErrorPacket (ErrorType type, const char *msg) {

    size_t packetSize = sizeof (PacketHeader) + sizeof (ErrorData);
    
    void *begin = generatePacket (ERROR_PACKET, packetSize);
    char *end = begin + sizeof (PacketHeader); 

    ErrorData *edata = (ErrorData *) end;
    edata->type = type;
    if (msg) {
        if (strlen (msg) > sizeof (edata->msg)) {
            // clamp the value to fit inside edata->msg
            u16 i = 0;
            while (i < sizeof (edata->msg) - 1) {
                edata->msg[i] = msg[i];
                i++;
            }

            edata->msg[i] = '\0';
        }

        else strcpy (edata->msg, msg);
    }

    return begin;

}

// creates and sends an error packet
u8 sendErrorPacket (Server *server, i32 sock_fd, struct sockaddr_storage address,
    ErrorType type, const char *msg) {

    size_t packetSize = sizeof (PacketHeader) + sizeof (ErrorData);
    void *errpacket = generateErrorPacket (type, msg);

    if (errpacket) {
        server_sendPacket (server, sock_fd, address, errpacket, packetSize);

        free (errpacket);
    }

    return 0;

}

u8 sendTestPacket (Server *server, i32 sock_fd, struct sockaddr_storage address) {

    if (server) {
        size_t packetSize = sizeof (PacketHeader);
        void *packet = generatePacket (TEST_PACKET, packetSize);
        if (packet) {
            u8 retval = server_sendPacket (server, sock_fd, address, packet, packetSize);
            free (packet);

            return retval;
        }
    }

    return 1;

}

void *createClientAuthReqPacket (void) {

    size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData);
    void *begin = generatePacket (AUTHENTICATION, packetSize);
    char *end = begin + sizeof (PacketHeader);

    RequestData *request = (RequestData *) end;
    request->type = REQ_AUTH_CLIENT;

    return begin;

}

// broadcast a packet/msg to all clients inside a client's tree
void broadcastToAllClients (AVLNode *node, Server *server, void *packet, size_t packetSize) {

    if (node && server && packet && (packetSize > 0)) {
        broadcastToAllClients (node->right, server, packet, packetSize);

        // send packet to current client
        if (node->id) {
            Client *client = (Client *) node->id;

            // 02/12/2018 -- send packet to the first active connection
            if (client) 
                server_sendPacket (server, client->active_connections[0], client->address,
                    packet, packetSize);
        }

        broadcastToAllClients (node->left, server, packet, packetSize);
    }

}

void *generateServerInfoPacket (Server *server) {

    if (server) {
        size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData) + sizeof (SServer);
        void *packet = generatePacket (SERVER_PACKET, packetSize);
         (packet + sizeof (RequestData));
        if (packet) {
            char *end = packet + sizeof (PacketHeader);
            RequestData *reqdata = (RequestData *) end;
            reqdata->type = SERVER_INFO;

            end += sizeof (RequestData);

            SServer *sinfo = (SServer *) end;
            sinfo->authRequired = server->authRequired;
            sinfo->port = server->port;
            sinfo->protocol = server->protocol;
            sinfo->type = server->type;
            sinfo->useIpv6 = server->useIpv6;

            return packet;
        }
    }

    return NULL;

}

// send useful server info to the client
void sendServerInfo (Server *server, i32 sock_fd, struct sockaddr_storage address) {

    if (server) {
        if (server->serverInfo) {
            size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData) + sizeof (SServer);

            if (!server_sendPacket (server, sock_fd, address, server->serverInfo, packetSize)) {
                #ifdef CERVER_DEBUG
                    logMsg (stdout, DEBUG_MSG, SERVER, "Sent server info packet.");
                #endif
            }
        }

        else {
            #ifdef CERVER_DEBUG
            logMsg (stdout, ERROR, SERVER, "No server info to send to client!");
            #endif
        } 
    }

}

// FIXME:
// TODO: move this from here to the config section
// set an action to be executed when a client connects to the server
void server_set_send_server_info () {}

#pragma endregion

/*** SESSIONS ***/

#pragma region SESSIONS

// create a unique session id for each client based on connection values
char *session_default_generate_id (i32 fd, const struct sockaddr_storage address) {

    char *ipstr = sock_ip_to_string ((const struct sockaddr *) &address);
    u16 port = sock_ip_port ((const struct sockaddr *) &address);

    if (ipstr && (port > 0)) {
        #ifdef CERVER_DEBUG
            logMsg (stdout, DEBUG_MSG, CLIENT,
                createString ("Client connected form IP address: %s -- Port: %i", 
                ipstr, port));
        #endif

        // 24/11/2018 -- 22:14 -- testing a simple id - just ip + port
        char *connection_values = createString ("%s-%i", ipstr, port);

        uint8_t hash[32];
        char hash_string[65];

        sha_256_calc (hash, connection_values, strlen (connection_values));
        sha_256_hash_to_string (hash_string, hash);

        char *retval = createString ("%s", hash_string);

        return retval;
    }

    return NULL;

}

// the server admin can define a custom session id generator
void session_set_id_generator (Server *server, Action idGenerator) {

    if (server && idGenerator) {
        if (server->useSessions) 
            server->generateSessionID = idGenerator;

        else logMsg (stderr, ERROR, SERVER, "Server is not set to use sessions!");
    }

}

#pragma endregion

/*** CLIENT AUTHENTICATION ***/

#pragma region CLIENT AUTHENTICATION

i32 getOnHoldIdx (Server *server) {

    if (server && server->authRequired) 
        for (u32 i = 0; i < poll_n_fds; i++)
            if (server->hold_fds[i].fd == -1) return i;

    return -1;

}

// we remove any fd that was set to -1 for what ever reason
void compressHoldClients (Server *server) {

    if (server) {
        server->compress_hold_clients = false;
        
        for (u16 i = 0; i < server->hold_nfds; i++) {
            if (server->hold_fds[i].fd == -1) {
                for (u16 j = i; j < server->hold_nfds; j++) 
                    server->hold_fds[j].fd = server->hold_fds[j + 1].fd;

                i--;
                server->hold_nfds--;
            }
        }
    }

}

static void server_recieve (Server *server, i32 fd, bool onHold);

// handles packets from the on hold clients until they authenticate
u8 handleOnHoldClients (void *data) {

    if (!data) {
        logMsg (stderr, ERROR, SERVER, "Can't handle on hold clients on a NULL server!");
        return 1;
    }

    Server *server = (Server *) data;

    #ifdef CERVER_DEBUG
        logMsg (stdout, SUCCESS, SERVER, "On hold client poll has started!");
    #endif

    int poll_retval;
    while (server->holdingClients) {
        poll_retval = poll (server->hold_fds, poll_n_fds, server->pollTimeout);
        
        // poll failed
        if (poll_retval < 0) {
            logMsg (stderr, ERROR, SERVER, "On hold poll failed!");
            server->holdingClients = false;
            break;
        }

        // if poll has timed out, just continue to the next loop... 
        if (poll_retval == 0) {
            // #ifdef CERVER_DEBUG
            // logMsg (stdout, DEBUG_MSG, SERVER, "On hold poll timeout.");
            // #endif
            continue;
        }

        // one or more fd(s) are readable, need to determine which ones they are
        for (u16 i = 0; i < poll_n_fds; i++) {
            if (server->hold_fds[i].revents == 0) continue;
            if (server->hold_fds[i].revents != POLLIN) continue;

            // TODO: maybe later add this directly to the thread pool
            server_recieve (server, server->hold_fds[i].fd, true);
        }
    } 

    #ifdef CERVER_DEBUG
        logMsg (stdout, SERVER, NO_TYPE, "Server on hold poll has stopped!");
    #endif

}

// if the server requires authentication, we send the newly connected clients to an on hold
// structure until they authenticate, if not, they are just dropped by the server
void onHoldClient (Server *server, Client *client, i32 fd) {

    // add the client to the on hold structres -> avl and poll
    if (server && client) {
        if (server->onHoldClients) {
            i32 idx = getOnHoldIdx (server);

            if (idx >= 0) {
                server->hold_fds[idx].fd = fd;
                server->hold_fds[idx].events = POLLIN;
                server->hold_nfds++; 

                server->n_hold_clients++;

                avl_insertNode (server->onHoldClients, client);

                if (server->holdingClients == false) {
                    thpool_add_work (server->thpool, (void *) handleOnHoldClients, server);
                    server->holdingClients = true;
                }          

                #ifdef CERVER_DEBUG
                    logMsg (stdout, DEBUG_MSG, SERVER, 
                        "Added a new client to the on hold structures.");
                #endif

                #ifdef CERVER_STATS
                    logMsg (stdout, SERVER, NO_TYPE, 
                        createString ("Current on hold clients: %i.", server->n_hold_clients));
                #endif
            }

            // FIXME: better handle this error!
            else {
                #ifdef CERVER_DEBUG
                logMsg (stderr, ERROR, SERVER, "New on hold idx = -1. Is the server full?");
                #endif
            }
        }
    }

}

// FIXME: when do we want to use this?
// FIXME: make available the on hold idx!!
// drops a client from the on hold structure because he was unable to authenticate
void dropClient (Server *server, Client *client) {

    if (server && client) {
        // destroy client should unregister the socket from the client
        // and from the on hold poll structure
        avl_removeNode (server->onHoldClients, client);

        // server->compress_hold_clients = true;

        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, 
            "Client removed from on hold structure. Failed to authenticate");
        #endif
    }   

}

Client *removeOnHoldClient (Server *server, Client *client, i32 socket_fd) {

    if (server) {
        Client *retval = NULL;

        if (client) {
            // remove the sock fd from the on hold structure
            for (u16 i = 0; i < poll_n_fds; i++) {
                if (server->hold_fds[i].fd == socket_fd) {
                    server->hold_fds[i].fd = -1;
                    server->hold_nfds--;
                }
            }

            retval = avl_removeNode (server->onHoldClients, client);

            server->n_hold_clients--;
        }

        else {
            #ifdef CERVER_DEBUG
                logMsg (stderr, ERROR, CLIENT, "Could not find client associated with socket!");
            #endif

            // this migth be an unusual error
            server->n_hold_clients--;
        }

        // if we don't have on hold clients, end on hold tread
        if (server->n_hold_clients <= 0) server->holdingClients = false;

        #ifdef CERVER_STATS
            logMsg (stdout, SERVER, NO_TYPE, 
            createString ("On hold clients: %i.", server->n_hold_clients));
        #endif

        return retval;
    }

}

// cerver default client authentication method
// the session id is the same as the token that we requiere to access it
u8 defaultAuthMethod (void *data) {

    if (data) {
        PacketInfo *pack_info = (PacketInfo *) data;

        //check if the server supports sessions
        if (pack_info->server->useSessions) {
            // check if we recieve a token or auth values
            bool isToken;
            if (pack_info->packetSize > 
                (sizeof (PacketHeader) + sizeof (RequestData) + sizeof (DefAuthData)))
                    isToken = true;

            if (isToken) {
                char *end = pack_info->packetData;
                Token *tokenData = (Token *) (end + 
                    sizeof (PacketHeader) + sizeof (RequestData));

                // verify the token and search for a client with the session ID
                Client *c = getClientBySession (pack_info->server->clients, tokenData->token);
                // if we found a client, auth is success
                if (c) {
                    client_set_sessionID (pack_info->client, tokenData->token);
                    return 0;
                } 

                else {
                    #ifdef CERVER_DEBUG
                    logMsg (stderr, ERROR, CLIENT, "Wrong session id provided by client!");
                    #endif
                    return 1;      // the session id is wrong -> error!
                } 
            }

            // we recieve auth values, so validate the credentials
            else {
                char *end = pack_info->packetData;
                DefAuthData *authData = (DefAuthData *) (end + 
                    sizeof (PacketHeader) + sizeof (RequestData));

                // credentials are good
                if (authData->code == DEFAULT_AUTH_CODE) {
                    #ifdef CERVER_DEBUG
                    logMsg (stdout, DEBUG_MSG, NO_TYPE, 
                        createString ("Default auth: client provided code: %i.", authData->code));
                    #endif

                    // FIXME: 19/april/2019 -- 19:08 -- dont we need to use server->generateSessionID?
                    char *sessionID = session_default_generate_id (pack_info->clientSock,
                        pack_info->client->address);

                    if (sessionID) {
                        #ifdef CERVER_DEBUG
                        logMsg (stdout, DEBUG_MSG, CLIENT, 
                            createString ("Generated client session id: %s", sessionID));
                        #endif

                        client_set_sessionID (pack_info->client, sessionID);

                        return 0;
                    }
                    
                    else logMsg (stderr, ERROR, CLIENT, "Failed to generate session id!");
                } 
                // wrong credentials
                else {
                    #ifdef CERVER_DEBUG
                    logMsg (stderr, ERROR, NO_TYPE, 
                        createString ("Default auth: %i is a wrong autentication code!", 
                        authData->code));
                    #endif
                    return 1;
                }     
            }
        }

        // if not, just check for client credentials
        else {
            DefAuthData *authData = (DefAuthData *) (pack_info->packetData + 
                sizeof (PacketHeader) + sizeof (RequestData));
            
            // credentials are good
            if (authData->code == DEFAULT_AUTH_CODE) {
                #ifdef CERVER_DEBUG
                logMsg (stdout, DEBUG_MSG, NO_TYPE, 
                    createString ("Default auth: client provided code: %i.", authData->code));
                #endif
                return 0;
            } 
            // wrong credentials
            else {
                #ifdef CERVER_DEBUG
                logMsg (stderr, ERROR, NO_TYPE, 
                    createString ("Default auth: %i is a wrong autentication code!", 
                    authData->code));
                #endif
            }     
        }
    }

    return 1;

}

// the admin is able to set a ptr to a custom authentication method
// handles the authentication data that the client sent
void authenticateClient (void *data) {

    if (data) {
        PacketInfo *pack_info = (PacketInfo *) data;

        if (pack_info->server->auth.authenticate) {
            // we expect the function to return us a 0 on success
            if (!pack_info->server->auth.authenticate (pack_info)) {
                logMsg (stdout, SUCCESS, CLIENT, "Client authenticated successfully!");

                if (pack_info->server->useSessions) {
                    // search for a client with a session id generated by the new credentials
                    Client *found_client = getClientBySession (pack_info->server->clients, 
                        pack_info->client->sessionID);

                    // if we found one, register the new connection to him
                    if (found_client) {
                        if (client_registerNewConnection (found_client, pack_info->clientSock))
                            logMsg (stderr, ERROR, CLIENT, "Failed to register new connection to client!");

                        i32 idx = getFreePollSpot (pack_info->server);
                        if (idx > 0) {
                            pack_info->server->fds[idx].fd = pack_info->clientSock;
                            pack_info->server->fds[idx].events = POLLIN;
                            pack_info->server->nfds++;
                        }
                        
                        // FIXME: how to better handle this error?
                        else logMsg (stderr, ERROR, NO_TYPE, "Failed to get new main poll idx!");

                        client_delete_data (removeOnHoldClient (pack_info->server, 
                            pack_info->client, pack_info->clientSock));

                        #ifdef CERVER_DEBUG
                        logMsg (stdout, DEBUG_MSG, CLIENT, 
                            createString ("Registered a new connection to client with session id: %s",
                            pack_info->client->sessionID));
                        printf ("sessionId: %s\n", pack_info->client->sessionID);
                        #endif
                    }

                    // we have a new client
                    else {
                        Client *got_client = removeOnHoldClient (pack_info->server, 
                            pack_info->client, pack_info->clientSock);

                        // FIXME: better handle this error!
                        if (!got_client) {
                            logMsg (stderr, ERROR, SERVER, "Failed to get client avl node data!");
                            return;
                        }

                        else {
                            #ifdef CERVER_DEBUG
                            logMsg (stdout, DEBUG_MSG, SERVER, 
                                "Got client data when removing an on hold avl node!");
                            #endif
                        }

                        client_registerToServer (pack_info->server, got_client, pack_info->clientSock);

                        // send back the session id to the client
                        size_t packet_size = sizeof (PacketHeader) + sizeof (RequestData) + sizeof (Token);
                        void *session_packet = generatePacket (AUTHENTICATION, packet_size);
                        if (session_packet) {
                            char *end = session_packet;
                            RequestData *reqdata = (RequestData *) (end += sizeof (PacketHeader));
                            reqdata->type = CLIENT_AUTH_DATA;

                            Token *tokenData = (Token *) (end += sizeof (RequestData));
                            strcpy (tokenData->token, pack_info->client->sessionID);

                            if (server_sendPacket (pack_info->server, 
                                pack_info->clientSock, pack_info->client->address,
                                session_packet, packet_size))
                                    logMsg (stderr, ERROR, PACKET, "Failed to send session token!");

                            free (session_packet);
                        }
                    } 
                }

                // if not, just register the new client to the server and use the
                // connection values as the client's id
                else client_registerToServer (pack_info->server, pack_info->client,
                    pack_info->clientSock);

                // send a success authentication packet to the client as feedback
                if (pack_info->server->type != WEB_SERVER) {
                    size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData);
                    void *successPacket = generatePacket (AUTHENTICATION, packetSize);
                    if (successPacket) {
                        char *end = successPacket + sizeof (PacketHeader);
                        RequestData *reqdata = (RequestData *) end;
                        reqdata->type = SUCCESS_AUTH;

                        server_sendPacket (pack_info->server, pack_info->clientSock, 
                            pack_info->client->address, successPacket, packetSize);

                        free (successPacket);
                    }
                }
            }

            // failed to authenticate
            else {
                #ifdef CERVER_DEBUG
                logMsg (stderr, ERROR, CLIENT, "Client failed to authenticate!");
                #endif

                // FIXME: this should only be used when using default authentication
                // sendErrorPacket (pack_info->server, pack_info->clientSock, 
                //     pack_info->client->address, ERR_FAILED_AUTH, "Wrong credentials!");

                pack_info->client->authTries--;

                if (pack_info->client->authTries <= 0)
                    dropClient (pack_info->server, pack_info->client);
            }
        }

        // no authentication method -- clients are not able to interact to the server!
        else {
            logMsg (stderr, ERROR, SERVER, "Server doesn't have an authenticate method!");
            logMsg (stderr, ERROR, SERVER, "Clients are unable to interact with the server!");

            // FIXME: correctly drop client and send error packet to the client!
            dropClient (pack_info->server, pack_info->client);
        }
    }

}

// we don't want on hold clients to make any other request
void handleOnHoldPacket (void *data) {

    if (data) {
        PacketInfo *pack_info = (PacketInfo *) data;

        if (!checkPacket (pack_info->packetSize, pack_info->packetData, DONT_CHECK_TYPE))  {
            PacketHeader *header = (PacketHeader *) pack_info->packetData;

            switch (header->packetType) {
                // handles an error from the client
                case ERROR_PACKET: break;

                // a client is trying to authenticate himself
                case AUTHENTICATION: {
                    RequestData *reqdata = (RequestData *) (pack_info->packetData + sizeof (PacketHeader));

                    switch (reqdata->type) {
                        case CLIENT_AUTH_DATA: authenticateClient (pack_info); break;
                        default: break;
                    }
                } break;

                case TEST_PACKET: 
                    logMsg (stdout, TEST, NO_TYPE, "Got a successful test packet!"); 
                    if (!sendTestPacket (pack_info->server, pack_info->clientSock, pack_info->client->address))
                        logMsg (stdout, TEST, PACKET, "Success answering the test packet.");

                    else logMsg (stderr, ERROR, PACKET, "Failed to answer test packet!");
                    break;

                default: 
                    logMsg (stderr, WARNING, PACKET, "Got a packet of incompatible type."); 
                    break;
            }
        }

        // FIXME: send the packet to the pool
        destroyPacketInfo (pack_info);
    }

}

void cerver_set_auth_method (Server *server, delegate authMethod) {

    if (server) server->auth.authenticate = authMethod;

}

#pragma endregion

/*** CONNECTION HANDLER ***/

#pragma region CONNECTION HANDLER

static RecvdBufferData *rcvd_buffer_data_new (Server *server, i32 socket_fd, char *packet_buffer, size_t total_size, bool on_hold) {

    RecvdBufferData *data = (RecvdBufferData *) malloc (sizeof (RecvdBufferData));
    if (data) {
        data->server = server;
        data->sock_fd = socket_fd;

        data->buffer = (char *) calloc (total_size, sizeof (char));
        memcpy (data->buffer, packet_buffer, total_size);
        data->total_size = total_size;

        data->onHold = on_hold;
    }

    return data;

}

void rcvd_buffer_data_delete (RecvdBufferData *data) {

    if (data) {
        if (data->buffer) free (data->buffer);
        free (data);
    }

}

i32 getFreePollSpot (Server *server) {

    if (server) 
        for (u32 i = 0; i < poll_n_fds; i++)
            if (server->fds[i].fd == -1) return i;

    return -1;

}

// we remove any fd that was set to -1 for what ever reason
static void compressClients (Server *server) {

    if (server) {
        server->compress_clients = false;

        for (u16 i = 0; i < server->nfds; i++) {
            if (server->fds[i].fd == -1) {
                for (u16 j = i; j < server->nfds; j++) 
                    server->fds[j].fd = server->fds[j + 1].fd;

                i--;
                server->nfds--;
            }
        }
    }  

}

// called with the th pool to handle a new packet
void handlePacket (void *data) {

    if (data) {
        PacketInfo *packet = (PacketInfo *) data;

        if (!checkPacket (packet->packetSize, packet->packetData, DONT_CHECK_TYPE))  {
            PacketHeader *header = (PacketHeader *) packet->packetData;

            switch (header->packetType) {
                case CLIENT_PACKET: {
                    RequestData *reqdata = (RequestData *) (packet->packetData + sizeof (PacketHeader));
                    
                    switch (reqdata->type) {
                        /* case CLIENT_DISCONNET: 
                            logMsg (stdout, DEBUG_MSG, CLIENT, "Ending client connection - client_disconnect ()");
                            client_closeConnection (packet->server, packet->client); 
                            break; */
                        default: break;
                    }
                }

                // handles an error from the client
                case ERROR_PACKET: break;

                // a client is trying to authenticate himself
                case AUTHENTICATION: break;

                // handles a request made from the client
                case REQUEST: break;

                // FIXME:
                // handle a game packet sent from a client
                // case GAME_PACKET: gs_handlePacket (packet); break;

                case TEST_PACKET: 
                    logMsg (stdout, TEST, NO_TYPE, "Got a successful test packet!"); 
                    // send a test packet back to the client
                    if (!sendTestPacket (packet->server, packet->clientSock, packet->client->address))
                        logMsg (stdout, DEBUG_MSG, PACKET, "Success answering the test packet.");

                    else logMsg (stderr, ERROR, PACKET, "Failed to answer test packet!");
                    break;

                default: 
                    logMsg (stderr, WARNING, PACKET, "Got a packet of incompatible type."); 
                    break;
            }
        }

        // FIXME: 22/11/2018 - error with packet pool!!! seg fault always the second time 
        // a client sends a packet!!
        // no matter what, we send the packet to the pool after using it
        // pool_push (packet->server->packetPool, data);
        destroyPacketInfo (packet);
    }

}

// TODO: move this from here! -> maybe create a server configuration section
void cerver_set_handler_received_buffer (Server *server, Action handler) {

    if (server) server->handle_recieved_buffer = handler;

}

// split the entry buffer in packets of the correct size
void default_handle_recieved_buffer (void *rcvd_buffer_data) {

    if (rcvd_buffer_data) {
        RecvdBufferData *data = (RecvdBufferData *) rcvd_buffer_data;

        if (data->buffer && (data->total_size > 0)) {
            u32 buffer_idx = 0;
            char *end = data->buffer;

            PacketHeader *header = NULL;
            u32 packet_size;
            char *packet_data = NULL;

            PacketInfo *info = NULL;

            while (buffer_idx < data->total_size) {
                header = (PacketHeader *) end;

                // check the packet size
                packet_size = header->packetSize;
                if (packet_size > 0) {
                    // copy the content of the packet from the buffer
                    packet_data = (char *) calloc (packet_size, sizeof (char));
                    for (u32 i = 0; i < packet_size; i++, buffer_idx++) 
                        packet_data[i] = data->buffer[buffer_idx];

                    Client *c = data->onHold ? getClientBySocket (data->server->onHoldClients->root, data->sock_fd) :
                        getClientBySocket (data->server->clients->root, data->sock_fd);

                    if (!c) {
                        logMsg (stderr, ERROR, CLIENT, "Failed to get client by socket!");
                        return;
                    }

                    info = newPacketInfo (data->server, c, data->sock_fd, packet_data, packet_size);

                    if (info)
                        thpool_add_work (data->server->thpool, data->onHold ?
                            (void *) handleOnHoldPacket : (void *) handlePacket, info);

                    else {
                        #ifdef CERVER_DEBUG
                        logMsg (stderr, ERROR, PACKET, "Failed to create packet info!");
                        #endif
                    }

                    end += packet_size;
                }

                else break;
            }
        }
    }

}

// TODO: add support for handling large files transmissions
// what happens if my buffer isn't enough, for example a larger file?
// recive all incoming data from the socket
static void server_recieve (Server *server, i32 socket_fd, bool onHold) {

    // if (onHold) logMsg (stdout, SUCCESS, PACKET, "server_recieve () - on hold client!");
    // else logMsg (stdout, SUCCESS, PACKET, "server_recieve () - normal client!");

    ssize_t rc;
    char packetBuffer[MAX_UDP_PACKET_SIZE];
    memset (packetBuffer, 0, MAX_UDP_PACKET_SIZE);

    // do {
        rc = recv (socket_fd, packetBuffer, sizeof (packetBuffer), 0);
        
        if (rc < 0) {
            if (errno != EWOULDBLOCK) {     // no more data to read 
                // as of 02/11/2018 -- we juts close the socket and if the client is hanging
                // it will be removed with the client timeout function 
                // this is to prevent an extra client_count -= 1
                logMsg (stdout, DEBUG_MSG, CLIENT, "server_recieve () - rc < 0");

                close (socket_fd);  // close the client socket
            }

            // break;
        }

        else if (rc == 0) {
            // man recv -> steam socket perfomed an orderly shutdown
            // but in dgram it might mean something?
            perror ("Error:");
            logMsg (stdout, DEBUG_MSG, CLIENT, 
                    "Ending client connection - server_recieve () - rc == 0");

            close (socket_fd);  // close the client socket

            if (onHold) 
                 removeOnHoldClient (server, 
                    getClientBySocket (server->onHoldClients->root, socket_fd), socket_fd);  

            else {
                // remove the socket from the main poll
                for (u32 i = 0; i < poll_n_fds; i++) {
                    if (server->fds[i].fd == socket_fd) {
                        server->fds[i].fd = -1;
                        server->fds[i].events = -1;
                    }
                } 

                Client *c = getClientBySocket (server->clients->root, socket_fd);
                if (c) 
                    if (c->n_active_cons <= 1) 
                        client_closeConnection (server, c);

                else {
                    #ifdef CERVER_DEBUG
                    logMsg (stderr, ERROR, CLIENT, 
                        "Couldn't find an active client with the requested socket!");
                    #endif
                }
            }

            // break;
        }

        else {
            // pass the necessary data to the server buffer handler
            RecvdBufferData *data = rcvd_buffer_data_new (server, socket_fd, packetBuffer, rc, onHold);
            server->handle_recieved_buffer (data);
        }
    // } while (true);

}

// FIXME: move this from here!!

static i32 server_accept (Server *server) {

    Client *client = NULL;

	struct sockaddr_storage clientAddress;
	memset (&clientAddress, 0, sizeof (struct sockaddr_storage));
    socklen_t socklen = sizeof (struct sockaddr_storage);

    i32 newfd = accept (server->serverSock, (struct sockaddr *) &clientAddress, &socklen);

    if (newfd < 0) {
        // if we get EWOULDBLOCK, we have accepted all connections
        if (errno != EWOULDBLOCK) logMsg (stderr, ERROR, SERVER, "Accept failed!");
        return -1;
    }

    #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, CLIENT, "Accepted a new client connection.");
    #endif

    // get client values to use as default id in avls
    char *connection_values = client_getConnectionValues (newfd, clientAddress);
    if (!connection_values) {
        logMsg (stderr, ERROR, CLIENT, "Failed to get client connection values.");
        close (newfd);
        return -1;
    }

    else {
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, CLIENT,
            createString ("Connection values: %s", connection_values));
        #endif
    } 

    client = newClient (server, newfd, clientAddress, connection_values);

    // if we requiere authentication, we send the client to on hold structure
    if (server->authRequired) onHoldClient (server, client, newfd);

    // if not, just register to the server as new client
    else {
        // if we need to generate session ids...
        if (server->useSessions) {
            // FIXME: change to the custom generate session id action in server->generateSessionID
            char *session_id = session_default_generate_id (newfd, clientAddress);
            if (session_id) {
                #ifdef CERVER_DEBUG
                logMsg (stdout, DEBUG_MSG, CLIENT, 
                    createString ("Generated client session id: %s", session_id));
                #endif

                client_set_sessionID (client, session_id);
            }
            
            else logMsg (stderr, ERROR, CLIENT, "Failed to generate session id!");
        }

        client_registerToServer (server, client, newfd);
    } 

    // FIXME: add the option to select which connection values to send!!!
    // if (server->type != WEB_SERVER) sendServerInfo (server, newfd, clientAddress);
   
    #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, "A new client connected to the server!");
    #endif

    return newfd;

}

// server poll loop to handle events in the registered socket's fds
static u8 server_poll (Server *server) {

    if (!server) {
        logMsg (stderr, ERROR, SERVER, "Can't listen for connections on a NULL server!");
        return 1;
    }

    int poll_retval;

    logMsg (stdout, SUCCESS, SERVER, "Server has started!");
    logMsg (stdout, DEBUG_MSG, SERVER, "Waiting for connections...");

    while (server->isRunning) {
        poll_retval = poll (server->fds, poll_n_fds, server->pollTimeout);

        // poll failed
        if (poll_retval < 0) {
            logMsg (stderr, ERROR, SERVER, "Main server poll failed!");
            perror ("Error");
            server->isRunning = false;
            break;
        }

        // if poll has timed out, just continue to the next loop... 
        if (poll_retval == 0) {
            // #ifdef DEBUG
            // logMsg (stdout, DEBUG_MSG, SERVER, "Poll timeout.");
            // #endif
            continue;
        }

        // one or more fd(s) are readable, need to determine which ones they are
        for (u8 i = 0; i < poll_n_fds; i++) {
            if (server->fds[i].revents == 0) continue;
            if (server->fds[i].revents != POLLIN) continue;

            // accept incoming connections that are queued
            if (server->fds[i].fd == server->serverSock) {
                if (server_accept (server)) {
                    #ifdef CERVER_DEBUG
                    logMsg (stdout, SUCCESS, CLIENT, "Success accepting a new client!");
                    #endif
                }
                else {
                    #ifdef CERVER_DEBUG
                    logMsg (stderr, ERROR, CLIENT, "Failed to accept a new client!");
                    #endif
                } 
            }

            // TODO: maybe later add this directly to the thread pool
            // not the server socket, so a connection fd must be readable
            else server_recieve (server, server->fds[i].fd, false);
        }

        // if (server->compress_clients) compressClients (server);
    }

    #ifdef CERVER_DEBUG
        logMsg (stdout, SERVER, NO_TYPE, "Server main poll has stopped!");
    #endif

}

#pragma endregion

/*** SERVER ***/

// TODO: handle ipv6 configuration
// TODO: create some helpful timestamps
// TODO: add the option to log our output to a .log file

// Here we manage the creation and destruction of servers 
#pragma region SERVER LOGIC

const char welcome[64] = "Welcome to cerver!";

// init server's data structures
u8 initServerDS (Server *server, ServerType type) {

    if (server->useSessions) server->clients = avl_init (client_comparator_sessionID, destroyClient);
    else server->clients = avl_init (client_comparator_clientID, destroyClient);

    server->clientsPool = pool_init (destroyClient);

    server->packetPool = pool_init (destroyPacketInfo);
    // by default init the packet pool with some members
    PacketInfo *info = NULL;
    for (u8 i = 0; i < 3; i++) {
        info = (PacketInfo *) malloc (sizeof (PacketInfo));
        info->packetData = NULL;
        pool_push (server->packetPool, info);
    } 

    // initialize pollfd structures
    memset (server->fds, 0, sizeof (server->fds));
    server->nfds = 0;
    server->compress_clients = false;

    // set all fds as available spaces
    for (u16 i = 0; i < poll_n_fds; i++) server->fds[i].fd = -1;

    if (server->authRequired) {
        server->onHoldClients = avl_init (client_comparator_clientID, destroyClient);

        memset (server->hold_fds, 0, sizeof (server->hold_fds));
        server->hold_nfds = 0;
        server->compress_hold_clients = false;

        server->n_hold_clients = 0;

        for (u16 i = 0; i < poll_n_fds; i++) server->hold_fds[i].fd = -1;
    }

    // initialize server's own thread pool
    server->thpool = thpool_init (DEFAULT_TH_POOL_INIT);
    if (!server->thpool) {
        logMsg (stderr, ERROR, SERVER, "Failed to init server's thread pool!");
        return 1;
    } 

    switch (type) {
        case FILE_SERVER: break;
        case WEB_SERVER: break;
        case GAME_SERVER: {
            // FIXME: correctly init the game server!!
            // GameServerData *gameData = (GameServerData *) malloc (sizeof (GameServerData));

            // // init the lobbys with n inactive in the pool
            // if (game_init_lobbys (gameData, GS_LOBBY_POOL_INIT)) {
            //     logMsg (stderr, ERROR, NO_TYPE, "Failed to init server lobbys!");
            //     return 1;
            // }

            // // init the players with n inactive in the pool
            // if (game_init_players (gameData, GS_PLAYER_POOL_INT)) {
            //     logMsg (stderr, ERROR, NO_TYPE, "Failed to init server players!");
            //     return 1;
            // }

            // server->serverData = gameData;
        } break;
        default: break;
    }

    return 0;

}

// depending on the type of server, we need to init some const values
void initServerValues (Server *server, ServerType type) {

    server->handle_recieved_buffer = default_handle_recieved_buffer;

    if (server->authRequired) {
        server->auth.reqAuthPacket = createClientAuthReqPacket ();
        server->auth.authPacketSize = sizeof (PacketHeader) + sizeof (RequestData);

        server->auth.authenticate = defaultAuthMethod;
    }

    else {
        server->auth.reqAuthPacket = NULL;
        server->auth.authPacketSize = 0;
    }

    if (server->useSessions) 
        server->generateSessionID = (void *) session_default_generate_id;

    server->serverInfo = generateServerInfoPacket (server);

    switch (type) {
        case FILE_SERVER: break;
        case WEB_SERVER: break;
        case GAME_SERVER: {
            GameServerData *data = (GameServerData *) server->serverData;

            // FIXME:
            // get game modes info from a config file
            // data->gameSettingsConfig = config_parse_file (GS_GAME_SETTINGS_CFG);
            // if (!data->gameSettingsConfig) 
            //     logMsg (stderr, ERROR, GAME, "Problems loading game settings config!");

            // data->n_gameInits = 0;
            // data->gameInitFuncs = NULL;
            // data->loadGameData = NULL;
        } break;
        default: break;
    }

    server->connectedClients = 0;

}

u8 getServerCfgValues (Server *server, ConfigEntity *cfgEntity) {

    char *ipv6 = config_get_entity_value (cfgEntity, "ipv6");
    if (ipv6) {
        server->useIpv6 = atoi (ipv6);
        // if we have got an invalid value, the default is not to use ipv6
        if (server->useIpv6 != 0 || server->useIpv6 != 1) server->useIpv6 = 0;

        free (ipv6);
    } 
    // if we do not have a value, use the default
    else server->useIpv6 = DEFAULT_USE_IPV6;

    #ifdef CERVER_DEBUG
    logMsg (stdout, DEBUG_MSG, SERVER, createString ("Use IPv6: %i", server->useIpv6));
    #endif

    char *tcp = config_get_entity_value (cfgEntity, "tcp");
    if (tcp) {
        u8 usetcp = atoi (tcp);
        if (usetcp < 0 || usetcp > 1) {
            logMsg (stdout, WARNING, SERVER, "Unknown protocol. Using default: tcp protocol");
            usetcp = 1;
        }

        if (usetcp) server->protocol = IPPROTO_TCP;
        else server->protocol = IPPROTO_UDP;

        free (tcp);

    }
    // set to default (tcp) if we don't found a value
    else {
        server->protocol = IPPROTO_TCP;
        logMsg (stdout, WARNING, SERVER, "No protocol found. Using default: tcp protocol");
    }

    char *port = config_get_entity_value (cfgEntity, "port");
    if (port) {
        server->port = atoi (port);
        // check that we have a valid range, if not, set to default port
        if (server->port <= 0 || server->port >= MAX_PORT_NUM) {
            logMsg (stdout, WARNING, SERVER, 
                createString ("Invalid port number. Setting port to default value: %i", DEFAULT_PORT));
            server->port = DEFAULT_PORT;
        }

        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, createString ("Listening on port: %i", server->port));
        #endif
        free (port);
    }
    // set to default port
    else {
        server->port = DEFAULT_PORT;
        logMsg (stdout, WARNING, SERVER, 
            createString ("No port found. Setting port to default value: %i", DEFAULT_PORT));
    } 

    char *queue = config_get_entity_value (cfgEntity, "queue");
    if (queue) {
        server->connectionQueue = atoi (queue);
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, 
            createString ("Connection queue: %i", server->connectionQueue));
        #endif
        free (queue);
    } 
    else {
        server->connectionQueue = DEFAULT_CONNECTION_QUEUE;
        logMsg (stdout, WARNING, SERVER, 
            createString ("Connection queue no specified. Setting it to default: %i", 
                DEFAULT_CONNECTION_QUEUE));
    }

    char *timeout = config_get_entity_value (cfgEntity, "timeout");
    if (timeout) {
        server->pollTimeout = atoi (timeout);
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, 
            createString ("Server poll timeout: %i", server->pollTimeout));
        #endif
        free (timeout);
    }
    else {
        server->pollTimeout = DEFAULT_POLL_TIMEOUT;
        logMsg (stdout, WARNING, SERVER, 
            createString ("Poll timeout no specified. Setting it to default: %i", 
                DEFAULT_POLL_TIMEOUT));
    }

    char *auth = config_get_entity_value (cfgEntity, "authentication");
    if (auth) {
        server->authRequired = atoi (auth);
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, server->authRequired == 1 ? 
            "Server requires client authentication" : "Server does not requires client authentication");
        #endif
        free (auth);
    }
    else {
        server->authRequired = DEFAULT_REQUIRE_AUTH;
        logMsg (stdout, WARNING, SERVER, 
            "No auth option found. No authentication required by default.");
    }

    if (server->authRequired) {
        char *tries = config_get_entity_value (cfgEntity, "authTries");
        if (tries) {
            server->auth.maxAuthTries = atoi (tries);
            #ifdef CERVER_DEBUG
            logMsg (stdout, DEBUG_MSG, SERVER, 
                createString ("Max auth tries set to: %i.", server->auth.maxAuthTries));
            #endif
            free (tries);
        }
        else {
            server->auth.maxAuthTries = DEFAULT_AUTH_TRIES;
            logMsg (stdout, WARNING, SERVER, 
                createString ("Max auth tries set to default: %i.", DEFAULT_AUTH_TRIES));
        }
    }

    char *sessions = config_get_entity_value (cfgEntity, "sessions");
    if (sessions) {
        server->useSessions = atoi (sessions);
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, server->useSessions == 1 ? 
            "Server supports client sessions." : "Server does not support client sessions.");
        #endif
        free (sessions);
    }
    else {
        server->useSessions = DEFAULT_USE_SESSIONS;
        logMsg (stdout, WARNING, SERVER, 
            "No sessions option found. No support for client sessions by default.");
    }

    return 0;

}

// inits a server of a given type
static u8 cerver_init (Server *server, Config *cfg, ServerType type) {

    int retval = 1;

    if (server) {
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, "Initializing server...");
        #endif

        if (cfg) {
            ConfigEntity *cfgEntity = config_get_entity_with_id (cfg, type);
            if (!cfgEntity) {
                logMsg (stderr, ERROR, SERVER, "Problems with server config!");
                return 1;
            } 

            #ifdef CERVER_DEBUG
            logMsg (stdout, DEBUG_MSG, SERVER, "Using config entity to set server values...");
            #endif

            if (!getServerCfgValues (server, cfgEntity)) 
                logMsg (stdout, SUCCESS, SERVER, "Done getting cfg server values");
        }

        // log server values
        else {
            #ifdef CERVER_DEBUG
            logMsg (stdout, DEBUG_MSG, SERVER, createString ("Use IPv6: %i", server->useIpv6));
            logMsg (stdout, DEBUG_MSG, SERVER, createString ("Listening on port: %i", server->port));
            logMsg (stdout, DEBUG_MSG, SERVER, createString ("Connection queue: %i", server->connectionQueue));
            logMsg (stdout, DEBUG_MSG, SERVER, createString ("Server poll timeout: %i", server->pollTimeout));
            logMsg (stdout, DEBUG_MSG, SERVER, server->authRequired == 1 ? 
                "Server requires client authentication" : "Server does not requires client authentication");
            if (server->authRequired) 
                logMsg (stdout, DEBUG_MSG, SERVER, 
                createString ("Max auth tries set to: %i.", server->auth.maxAuthTries));
            logMsg (stdout, DEBUG_MSG, SERVER, server->useSessions == 1 ? 
                "Server supports client sessions." : "Server does not support client sessions.");
            #endif
        }

        // init the server with the selected protocol
        switch (server->protocol) {
            case IPPROTO_TCP: 
                server->serverSock = socket ((server->useIpv6 == 1 ? AF_INET6 : AF_INET), SOCK_STREAM, 0);
                break;
            case IPPROTO_UDP:
                server->serverSock = socket ((server->useIpv6 == 1 ? AF_INET6 : AF_INET), SOCK_DGRAM, 0);
                break;

            default: logMsg (stderr, ERROR, SERVER, "Unkonw protocol type!"); return 1;
        }
        
        if (server->serverSock < 0) {
            logMsg (stderr, ERROR, SERVER, "Failed to create server socket!");
            return 1;
        }

        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, "Created server socket");
        #endif

        // set the socket to non blocking mode
        if (!sock_setBlocking (server->serverSock, server->blocking)) {
            logMsg (stderr, ERROR, SERVER, "Failed to set server socket to non blocking mode!");
            close (server->serverSock);
            return 1;
        }

        else {
            server->blocking = false;
            #ifdef CERVER_DEBUG
            logMsg (stdout, DEBUG_MSG, SERVER, "Server socket set to non blocking mode.");
            #endif
        }

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

        if (initServerDS (server, type))  {
            logMsg (stderr, ERROR, NO_TYPE, "Failed to init server data structures!");
            return 1;
        }

        server->type = type;
        initServerValues (server, server->type);
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, "Done creating server data structures...");
        #endif
    }

    return retval;

}

// cerver constructor, with option to init with some values
Server *cerver_new (Server *cerver) {

    Server *c = (Server *) malloc (sizeof (Server));
    if (c) {
        memset (c, 0, sizeof (Server));

        // init with some values
        if (cerver) {
            c->useIpv6 = cerver->useIpv6;
            c->protocol = cerver->protocol;
            c->port = cerver->port;
            c->connectionQueue = cerver->connectionQueue;
            c->pollTimeout = cerver->pollTimeout;
            c->authRequired = cerver->authRequired;
            c->type = cerver->type;
        }

        c->name = NULL;
        c->destroyServerData = NULL;

        // by default the socket is assumed to be a blocking socket
        c->blocking = true;

        c->authRequired = DEFAULT_REQUIRE_AUTH;
        c->useSessions = DEFAULT_USE_SESSIONS;
        
        c->isRunning = false;
    }

    return c;

}

void cerver_delete (Server *cerver) {

    if (cerver) {
        // TODO: what else do we want to delete here?
        str_delete (cerver->name);

        free (cerver);
    }

}

// creates a new server of the specified type and with option for a custom name
// also has the option to take another cerver as a paramater
// if no cerver is passed, configuration will be read from config/server.cfg
Server *cerver_create (ServerType type, const char *name, Server *cerver) {

    Server *c = NULL;

    // create a server with the requested parameters
    if (cerver) {
        c = cerver_new (cerver);
        if (!cerver_init (c, NULL, type)) {
            if (name) c->name = str_new (name);
            log_newServer (c);
        }

        else {
            logMsg (stderr, ERROR, SERVER, "Failed to init the server!");
            cerver_delete (c);   // delete the failed server...
        }
    }

    // create the server from the default config file
    else {
        Config *serverConfig = config_parse_file (SERVER_CFG);
        if (serverConfig) {
            c = cerver_new (NULL);
            if (!cerver_init (c, serverConfig, type)) {
                if (name) c->name = str_new (name);
                log_newServer (c);
                config_destroy (serverConfig);
            }

            else {
                logMsg (stderr, ERROR, SERVER, "Failed to init the server!");
                config_destroy (serverConfig);
                cerver_delete (c); 
            }
        } 

        else logMsg (stderr, ERROR, NO_TYPE, "Problems loading server config!\n");
    }

    return c;

}

// teardowns the server and creates a fresh new one with the same parameters
Server *cerver_restart (Server *server) {

    if (server) {
        logMsg (stdout, SERVER, NO_TYPE, "Restarting the server...");

        Server temp = { 
            .useIpv6 = server->useIpv6, .protocol = server->protocol, .port = server->port,
            .connectionQueue = server->connectionQueue, .type = server->type,
            .pollTimeout = server->pollTimeout, .authRequired = server->authRequired,
            .useSessions = server->useSessions };

        temp.name = str_new (server->name->str);

        if (!cerver_teardown (server)) logMsg (stdout, SUCCESS, SERVER, "Done with server teardown");
        else logMsg (stderr, ERROR, SERVER, "Failed to teardown the server!");

        // what ever the output, create a new server --> restart
        Server *retServer = cerver_new (&temp);
        if (!cerver_init (retServer, NULL, temp.type)) {
            logMsg (stdout, SUCCESS, SERVER, "Server has restarted!");
            return retServer;
        }

        else logMsg (stderr, ERROR, SERVER, "Unable to retstart the server!");
    }

    else logMsg (stdout, WARNING, SERVER, "Can't restart a NULL server!");
    
    return NULL;

}

// TODO: handle logic when we have a load balancer --> that will be the one in charge to listen for
// connections and accept them -> then it will send them to the correct server
// TODO: 13/10/2018 -- we can only handle a tcp server
// depending on the protocol, the logic of each server might change...
u8 cerver_start (Server *server) {

    if (server->isRunning) {
        logMsg (stdout, WARNING, SERVER, "The server is already running!");
        return 1;
    }

    // one time only inits
    // if we have a game server, we might wanna load game data -> set by server admin
    if (server->type == GAME_SERVER) {
        GameServerData *game_data = (GameServerData *) server->serverData;
        if (game_data && game_data->load_game_data) {
            game_data->load_game_data (NULL);
        }

        else logMsg (stdout, WARNING, GAME, "Game server doesn't have a reference to a game data!");
    }

    u8 retval = 1;
    switch (server->protocol) {
        case IPPROTO_TCP: {
            if (server->blocking == false) {
                if (!listen (server->serverSock, server->connectionQueue)) {
                    // set up the initial listening socket     
                    server->fds[server->nfds].fd = server->serverSock;
                    server->fds[server->nfds].events = POLLIN;
                    server->nfds++;

                    server->isRunning = true;

                    if (server->authRequired) server->holdingClients = false;

                    server_poll (server);

                    retval = 0;
                }

                else {
                    logMsg (stderr, ERROR, SERVER, "Failed to listen in server socket!");
                    close (server->serverSock);
                    retval = 1;
                }
            }

            else {
                logMsg (stderr, ERROR, SERVER, "Server socket is not set to non blocking!");
                retval = 1;
            }
            
        } break;

        case IPPROTO_UDP: /* TODO: */ break;

        default: 
            logMsg (stderr, ERROR, SERVER, "Cant't start server! Unknown protocol!");
            retval = 1;
            break;
    }

    return retval;

}

// disable socket I/O in both ways and stop any ongoing job
u8 cerver_shutdown (Server *server) {

    if (server->isRunning) {
        server->isRunning = false; 

        // close the server socket
        if (!close (server->serverSock)) {
            #ifdef CERVER_DEBUG
                logMsg (stdout, DEBUG_MSG, SERVER, "The server socket has been closed.");
            #endif

            return 0;
        }

        else logMsg (stdout, ERROR, SERVER, "Failed to close server socket!");
    } 

    return 1;

}

// cleans up the client's structures in the current server
// if ther are clients connected, we send a server teardown packet
static void cerver_destroy_clients (Server *server) {

    // create server teardown packet
    size_t packetSize = sizeof (PacketHeader);
    void *packet = generatePacket (SERVER_TEARDOWN, packetSize);

    // send a packet to any active client
    broadcastToAllClients (server->clients->root, server, packet, packetSize);
    
    // clear the client's poll  -> to stop any connection
    // this may change to a for if we have a dynamic poll structure
    memset (server->fds, 0, sizeof (server->fds));

    // destroy the active clients tree
    avl_clearTree (&server->clients->root, server->clients->destroy);

    if (server->authRequired) {
        // send a packet to on hold clients
        broadcastToAllClients (server->onHoldClients->root, server, packet, packetSize);
        // destroy the tree
        avl_clearTree (&server->onHoldClients->root, server->onHoldClients->destroy);

        // clear the on hold client's poll
        // this may change to a for if we have a dynamic poll structure
        memset (server->hold_fds, 0, sizeof (server->fds));
    }

    // the pool has only "empty" clients
    pool_clear (server->clientsPool);

    free (packet);

}

// teardown a server -> stop the server and clean all of its data
u8 cerver_teardown (Server *server) {

    if (!server) {
        logMsg (stdout, ERROR, SERVER, "Can't destroy a NULL server!");
        return 1;
    }

    #ifdef CERVER_DEBUG
        logMsg (stdout, SERVER, NO_TYPE, "Init server teardown...");
    #endif

    // TODO: what happens if we have a custom auth method?
    // clean server auth data
    if (server->authRequired) 
        if (server->auth.reqAuthPacket) free (server->auth.reqAuthPacket);

    // clean independent server type structs
    // if the server admin added a destroy server data action...
    if (server->destroyServerData) server->destroyServerData (server);
    else {
        // FIXME:
        // we use the default destroy server data function
        // switch (server->type) {
        //     case GAME_SERVER: 
        //         if (!destroyGameServer (server))
        //             logMsg (stdout, SUCCESS, SERVER, "Done clearing game server data!"); 
        //         break;
        //     case FILE_SERVER: break;
        //     case WEB_SERVER: break;
        //     default: break; 
        // }
    }

    // clean common server structs
    cerver_destroy_clients (server);
    #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, "Done cleaning up clients.");
    #endif

    // disable socket I/O in both ways and stop any ongoing job
    if (!cerver_shutdown (server))
        logMsg (stdout, SUCCESS, SERVER, "Server has been shutted down.");

    else logMsg (stderr, ERROR, SERVER, "Failed to shutdown server!");
    
    if (server->thpool) {
        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, 
            createString ("Server active thpool threads: %i", 
            thpool_num_threads_working (server->thpool)));
        #endif

        // FIXME: 04/12/2018 - 15:02 -- getting a segfault
        thpool_destroy (server->thpool);

        #ifdef CERVER_DEBUG
        logMsg (stdout, DEBUG_MSG, SERVER, "Destroyed server thpool!");
        #endif
    } 

    // destroy any other server data
    if (server->packetPool) pool_clear (server->packetPool);
    if (server->serverInfo) free (server->serverInfo);

    if (server->name) free (server->name);

    free (server);

    logMsg (stdout, SUCCESS, NO_TYPE, "Server teardown was successfull!");

    return 0;

}

#pragma endregion

/*** LOAD BALANCER ***/

#pragma region LOAD BALANCER

// TODO:

#pragma endregion