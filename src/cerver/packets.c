#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/cerver.h"
#include "cerver/client.h"

static ProtocolID protocol_id = 0;
static ProtocolVersion protocol_version = { 0, 0 };

void packets_set_protocol_id (ProtocolID proto_id) { protocol_id = proto_id; }

void packets_set_protocol_version (ProtocolVersion version) { protocol_version = version; }

static PacketHeader *packet_header_new (PacketType packet_type, size_t packet_size) {

    PacketHeader *header = (PacketHeader *) malloc (sizeof (PacketHeader));
    if (header) {
        memset (header, 0, sizeof (PacketHeader));
        header->protocol_id = protocol_id;
        header->protocol_version = protocol_version;
        header->packet_type = packet_type;
        header->packet_size = packet_size;
    }

    return header;

}

static inline void packet_header_delete (PacketHeader *header) { if (header) free (header); }

u8 packet_append_data (Packet *packet, void *data, size_t data_size);

Packet *packet_new (void) {

    Packet *packet = (Packet *) malloc (sizeof (Packet));
    if (packet) {
        memset (packet, 0, sizeof (Packet));
        packet->cerver = NULL;
        packet->client = NULL;

        packet->custom_type = NULL;

        packet->data = NULL;
        packet->data_end = NULL;

        packet->header = NULL;  
        packet->packet = NULL;
    }

    return packet;

}

// create a new packet with the option to pass values directly
// data is copied into packet buffer and can be safely freed
Packet *packet_create (PacketType type, void *data, size_t data_size) {

    Packet *packet = packet_new ();
    if (packet) {
        packet->packet_type = type;
        if (data) packet_append_data (packet, data, data_size);
    }

    return packet;

}

void packet_delete (void *ptr) {

    if (ptr) {
        Packet *packet = (Packet *) ptr;

        packet->cerver = NULL;
        packet->client = NULL;

        str_delete (packet->custom_type);
        if (packet->data) free (packet->data);
        packet_header_delete (packet->header);
        if (packet->packet) free (packet->packet);

        free (packet);
    }

}

// sets the pakcet destinatary is directed to and the protocol to use
void packet_set_network_values (Packet *packet, const i32 sock_fd, const Protocol protocol) {

    if (packet) {
        packet->sock_fd = sock_fd;
        packet->protocol = protocol;
    }

}

// appends the data to the end if the packet already has data
// if the packet is empty, creates a new buffer
// it creates a new copy of the data and the original can be safely freed
u8 packet_append_data (Packet *packet, void *data, size_t data_size) {

    u8 retval = 1;

    if (packet && data) {
        // append the data to the end if the packet already has data
        if (packet->data) {
            size_t new_size = packet->data_size + data_size;
            void *new_data = malloc (new_size);
            if (new_data) {
                // copy the last buffer to new one
                memcpy (new_data, packet->data, packet->data_size);
                packet->data_end = new_data;
                packet->data_end += packet->data_size;

                // copy the new buffer
                memcpy (packet->data_end, data, data_size);
                packet->data_end += data_size;

                free (packet->data);
                packet->data = new_data;
                packet->data_size = new_size;

                retval = 0;
            }
        }

        // if the packet is empty, create a new buffer
        else {
            packet->data_size = data_size;
            packet->data = malloc (packet->data_size);
            if (packet->data) {
                // copy the data to the packet data buffer
                memcpy (packet->data, data, data_size);
                packet->data_end = packet->data;
                packet->data_end += packet->data_size;

                retval = 0;
            }
        }
    }

    return retval;

}

// prepares the packet to be ready to be sent
// returns 0 on sucess, 1 on error
u8 packet_generate (Packet *packet) {

    u8 retval = 0;

    if (packet) {
        if (packet->packet) {
            free (packet->packet);
            packet->packet = NULL;
            packet->packet_size = 0;
        }   

        packet->packet_size = sizeof (PacketHeader) + packet->data_size;
        packet->header = packet_header_new (packet->packet_type, packet->packet_size);

        // create the packet buffer to be sent
        packet->packet = malloc (packet->packet_size);
        if (packet->packet) {
            char *end = packet->packet;
            memcpy (end, packet->header, sizeof (PacketHeader));
            end += sizeof (PacketHeader);
            memcpy (end, packet->data, packet->data_size);

            retval = 0;
        }
    }

    return retval;

}

// generates a simple request packet of the requested type reday to be sent, 
// and with option to pass some data
Packet *packet_generate_request (PacketType packet_type, u32 req_type, 
    void *data, size_t data_size) {

    Packet *packet = packet_new ();
    if (packet) {
        packet->packet_type = packet_type;

        // generate the request
        packet->data = malloc (sizeof (RequestData));
        ((RequestData *) packet->data)->type = req_type;

        packet->data_size = sizeof (RequestData);
        packet->data_end = packet->data;
        packet->data_end += sizeof (RequestData);

        // if there is data, append it to the packet data buffer
        if (data) packet_append_data (packet, data, data_size);

        // make the packet ready to be sent, and check for errors
        if (packet_generate (packet)) {
            packet_delete (packet);
            packet = NULL;
        }
    }

    return packet;

}

// TODO: check for errno appropierly
// sends a packet using the tcp protocol and the packet sock fd
// returns 0 on success, 1 on error
u8 packet_send_tcp (const Packet *packet, int flags) {

    if (packet) {
        ssize_t sent;
        const void *p = packet->packet;
        size_t packet_size = packet->packet_size;

        while (packet_size > 0) {
            sent = send (packet->sock_fd, p, packet_size, flags);
            if (sent < 0) return 1;
            p += sent;
            packet_size -= sent;
        }

        return 0;
    }

    return 1;

}

// FIXME: correctly send an udp packet!!
u8 packet_send_udp (const void *packet, size_t packet_size) {

    ssize_t sent;
    const void *p = packet;
    // while (packet_size > 0) {
    //     sent = sendto (server->serverSock, begin, packetSize, 0, 
    //         (const struct sockaddr *) &address, sizeof (struct sockaddr_storage));
    //     if (sent <= 0) return -1;
    //     p += sent;
    //     packetSize -= sent;
    // }

    return 0;

}

// sends a packet using its network values
u8 packet_send (const Packet *packet, int flags) {

    u8 retval = 1;

    if (packet) {
        switch (packet->protocol) {
            case PROTOCOL_TCP: 
                retval = packet_send_tcp (packet, flags);
                break;
            case PROTOCOL_UDP:
                break;

            default: break;
        }
    }

    return retval;

}

// FIXME:
// check for packets with bad size, protocol, version, etc
u8 packet_check (Packet *packet) {

    /*if (packetSize < sizeof (PacketHeader)) {
        #ifdef CLIENT_DEBUG
        cerver_log_msg (stderr, LOG_WARNING, LOG_NO_TYPE, "Recieved a to small packet!");
        #endif
        return 1;
    } 

    PacketHeader *header = (PacketHeader *) packetData;

    if (header->protocolID != PROTOCOL_ID) {
        #ifdef CLIENT_DEBUG
        logMsg (stdout, LOG_WARNING, LOG_PACKET, "Packet with unknown protocol ID.");
        #endif
        return 1;
    }

    Version version = header->protocolVersion;
    if (version.major != PROTOCOL_VERSION.major) {
        #ifdef CLIENT_DEBUG
        logMsg (stdout, LOG_WARNING, LOG_PACKET, "Packet with incompatible version.");
        #endif
        return 1;
    }

    // compare the size we got from recv () against what is the expected packet size
    // that the client created 
    if (packetSize != header->packetSize) {
        #ifdef CLIENT_DEBUG
        logMsg (stdout, LOG_WARNING, LOG_PACKET, "Recv packet size doesn't match header size.");
        #endif
        return 1;
    } 

    if (expectedType != DONT_CHECK_TYPE) {
        // check if the packet is of the expected type
        if (header->packetType != expectedType) {
            #ifdef CLIENT_DEBUG
            logMsg (stdout, LOG_WARNING, LOG_PACKET, "Packet doesn't match expected type.");
            #endif
            return 1;
        }
    }

    return 0;   // packet is fine */

}

/*** OLD PACKETS CODE ***/

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

// FIXME:
// // broadcast a packet/msg to all clients inside a client's tree
// void broadcastToAllClients (AVLNode *node, Server *server, void *packet, size_t packetSize) {

//     if (node && server && packet && (packetSize > 0)) {
//         broadcastToAllClients (node->right, server, packet, packetSize);

//         // send packet to current client
//         if (node->id) {
//             Client *client = (Client *) node->id;

//             // 02/12/2018 -- send packet to the first active connection
//             if (client) 
//                 server_sendPacket (server, client->active_connections[0], client->address,
//                     packet, packetSize);
//         }

//         broadcastToAllClients (node->left, server, packet, packetSize);
//     }

// }