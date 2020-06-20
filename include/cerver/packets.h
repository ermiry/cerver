#ifndef _CERVER_PACKETS_H_
#define _CERVER_PACKETS_H_

#include <stdlib.h>
#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/network.h"
#include "cerver/cerver.h"
#include "cerver/client.h"

#include "cerver/game/lobby.h"

struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;

typedef u32 ProtocolID;

// gets the current protocol id set in your application
extern ProtocolID packets_get_protocol_id (void);

// Sets the protocol id that this cerver will use for its packets. 
// The protocol id is a unique number that you can set to only accept packets that are comming from your application
// If the protocol id coming from the cerver don't match your application's, it will be considered a bad packet
// This value is only cheked if you enable packet checking for your cerver
extern void packets_set_protocol_id (ProtocolID protocol_id);

typedef struct ProtocolVersion {

	u16 major;
	u16 minor;
	
} ProtocolVersion;


// gets the current version set in your application
extern ProtocolVersion packets_get_protocol_version (void);

// Sets the protocol version for the cerver. 
// The version is an identifier to help you manage different versions of your deployed applications
// If the versions of your packet don't match, it will be considered a bad packet
// This value is only cheked if you enable packet checking for your cerver
extern void packets_set_protocol_version (ProtocolVersion version);

// these indicate what type of packet we are sending/recieving
typedef enum PacketType {

    CERVER_PACKET       = 0,
    CLIENT_PACKET       = 1,

    ERROR_PACKET        = 2,

	REQUEST_PACKET      = 3,
    AUTH_PACKET         = 4,
    GAME_PACKET         = 5,

    APP_PACKET          = 6,
    APP_ERROR_PACKET    = 7,

    CUSTOM_PACKET       = 70,

    TEST_PACKET         = 100,
    DONT_CHECK_TYPE     = 101,

} PacketType;

struct _PacketsPerType {

	u64 n_error_packets;
	u64 n_auth_packets;
	u64 n_request_packets;
	u64 n_game_packets;
	u64 n_app_packets;
	u64 n_app_error_packets;
	u64 n_custom_packets;
	u64 n_test_packets;
	u64 n_unknown_packets;

	u64 n_bad_packets;

};

typedef struct _PacketsPerType PacketsPerType;

extern PacketsPerType *packets_per_type_new (void);

extern void packets_per_type_delete (void *ptr);

extern void packets_per_type_print (PacketsPerType *packets_per_type);

struct _PacketHeader {

	ProtocolID protocol_id;
	ProtocolVersion protocol_version;
	PacketType packet_type;
	size_t packet_size;

	// 10/05/2020 -- select which app packet handler to use
	u8 handler_id;

};

typedef struct _PacketHeader PacketHeader;

// prints an already existing PacketHeader. Mostly used for debugging
extern void packet_header_print (PacketHeader *header);

// allocates space for the dest packet header and copies the data from source
// returns 0 on success, 1 on error
extern u8 packet_header_copy (PacketHeader **dest, PacketHeader *source);

typedef enum RequestType {

    REQ_GET_FILE                = 1,
    POST_SEND_FILE              = 2,
    
} RequestType;

typedef enum CerverPacketType {

	CERVER_INFO                 = 0,
	CERVER_TEARDOWN             = 1,

	CERVER_INFO_STATS           = 2,
	CERVER_GAME_STATS           = 3

} CerverPacketType;

typedef enum ClientPacketType {

	CLIENT_CLOSE_CONNECTION     = 1,
	CLIENT_DISCONNET            = 2,

} ClientPacketType;

typedef enum AuthPacketType {

    REQ_AUTH_CLIENT             = 1,

    CLIENT_AUTH_DATA            = 2,
    SUCCESS_AUTH                = 3,

} AuthPacketType;

typedef enum GamePacketType {

	GAME_LOBBY_CREATE           = 0,
	GAME_LOBBY_JOIN             = 1,
	GAME_LOBBY_LEAVE            = 2,
	GAME_LOBBY_UPDATE           = 3,
	GAME_LOBBY_DESTROY          = 4,

	GAME_INIT                   = 5,   // prepares the game structures
	GAME_START                  = 6,   // strat running the game
	GAME_INPUT_UPDATE           = 7,
	GAME_SEND_MSG               = 8,

} GamePacketType;

struct _RequestData {

	u32 type;

};

typedef struct _RequestData RequestData;

struct _Packet {

	// the cerver and client the packet is from
	struct _Cerver *cerver;
	struct _Client *client;
	struct _Connection *connection;
	struct _Lobby *lobby;

	PacketType packet_type;
	estring *custom_type;

	// serilized data
	size_t data_size;
	void *data;
	char *data_end;
	bool data_ref;

	// the actual packet to be sent
	PacketHeader *header;
	size_t packet_size;
	void *packet;
	bool packet_ref;

};

typedef struct _Packet Packet;

// allocates a new empty packet
extern Packet *packet_new (void);

// correctly deletes a packet and all of its data
extern void packet_delete (void *ptr);

// creates a new packet with the option to pass values directly
// data is copied into packet buffer and can be safely freed
extern Packet *packet_create (PacketType type, void *data, size_t data_size);

// sets the packet destinatary to whom this packet is going to be sent
extern void packet_set_network_values (Packet *packet, struct _Cerver *cerver, 
	struct _Client *client, struct _Connection *connection, struct _Lobby *lobby);

// sets the data of the packet -> copies the data into the packet
// if the packet had data before it is deleted and replaced with the new one
// returns 0 on success, 1 on error
extern u8 packet_set_data (Packet *packet, void *data, size_t data_size);

// appends the data to the end if the packet already has data
// if the packet is empty, creates a new buffer
// it creates a new copy of the data and the original can be safely freed
// this does not work if the data has been set using a reference
// returns 0 on success, 1 on error
extern u8 packet_append_data (Packet *packet, void *data, size_t data_size);

// sets a reference to a data buffer to send
// data will not be copied into the packet and will not be freed after use
// this method is usefull for example if you just want to send a raw json packet to a non-cerver
// use this method with packet_send () with the raw flag on
// returns 0 on success, 1 on error
extern u8 packet_set_data_ref (Packet *packet, void *data, size_t data_size);

// sets a packet's packet by copying the passed data, so you will be able to free your data
// this data is expected to already contain a header, otherwise, send with raw flag
// deletes the previuos packet's packet
// returns 0 on succes, 1 on error
extern u8 packet_set_packet (Packet *packet, void *data, size_t data_size);

// sets a reference to a data buffer to send as the packet
// data will not be copied into the packet and will not be freed after use
// usefull when you need to generate your own cerver type packet by hand
// returns 0 on success, 1 on error
extern u8 packet_set_packet_ref (Packet *packet, void *data, size_t packet_size);

// prepares the packet to be ready to be sent
// WARNING: dont call this method if you have set the packet directly
// returns 0 on success, 1 on error
extern u8 packet_generate (Packet *packet);

// generates a simple request packet of the requested type reday to be sent, 
// and with option to pass some data
// returns a newly allocated packet that should be deleted after use
extern Packet *packet_generate_request (PacketType packet_type, u32 req_type, 
	void *data, size_t data_size);

// sends a packet using its network values
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
extern u8 packet_send (const Packet *packet, int flags, size_t *total_sent, bool raw);

// sends a packet directly to the socket
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
extern u8 packet_send_to_sock_fd (const Packet *packet, const i32 sock_fd, 
    int flags, size_t *total_sent, bool raw);

// check if packet has a compatible protocol id and a version
// returns false on a bad packet
extern bool packet_check (Packet *packet);

#endif