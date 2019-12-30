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

extern void packets_set_protocol_id (ProtocolID protocol_id);

typedef struct ProtocolVersion {

	u16 major;
	u16 minor;
	
} ProtocolVersion;

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

};

typedef struct _PacketHeader PacketHeader;

// allocates space for the dest packet header and copies the data from source
// returns 0 on success, 1 on error
extern u8 packet_header_copy (PacketHeader **dest, PacketHeader *source);

// these indicate the data and more info about the packet type
typedef enum RequestType {

	CLIENT_CLOSE_CONNECTION     = 2,
	CLIENT_DISCONNET            = 3,

	REQ_GET_FILE                = 4,
	POST_SEND_FILE              = 5,
	
	REQ_AUTH_CLIENT             = 6,
	CLIENT_AUTH_DATA            = 7,
	SUCCESS_AUTH                = 8,

} RequestType;

typedef enum CerverPacket {

	CERVER_INFO                 = 0,
	CERVER_TEARDOWN             = 1,

	CERVER_INFO_STATS           = 2,
	CERVER_GAME_STATS           = 3

} CerverPacket;

typedef enum GamePacket {

	GAME_LOBBY_CREATE           = 0,
	GAME_LOBBY_JOIN             = 1,
	GAME_LOBBY_LEAVE            = 2,
	GAME_LOBBY_UPDATE           = 3,
	GAME_LOBBY_DESTROY          = 4,

	GAME_INIT                   = 5,   // prepares the game structures
	GAME_START                  = 6,   // strat running the game
	GAME_INPUT_UPDATE           = 7,
	GAME_SEND_MSG               = 8,

} GamePacket;

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

	// the actual packet to be sent
	PacketHeader *header;
	size_t packet_size;
	void *packet;

};

typedef struct _Packet Packet;

extern Packet *packet_new (void);

extern void packet_delete (void *ptr);

// create a new packet with the option to pass values directly
// data is copied into packet buffer and can be safely freed
extern Packet *packet_create (PacketType type, void *data, size_t data_size);

// sets the pakcet destinatary is directed to and the protocol to use
extern void packet_set_network_values (Packet *packet, struct _Cerver *cerver, 
	struct _Client *client, struct _Connection *connection, struct _Lobby *lobby);

// sets the data of the packet -> copies the data into the packet
// if the packet had data before it is deleted and replaced with the new one
// returns 0 on success, 1 on error
extern u8 packet_set_data (Packet *packet, void *data, size_t data_size);

// appends the data to the end if the packet already has data
// if the packet is empty, creates a new buffer
// it creates a new copy of the data and the original can be safely freed
extern u8 packet_append_data (Packet *packet, void *data, size_t data_size);

// sets a the packet's packet using by copying the passed data
// deletes the previuos packet's packet
// returns 0 on succes, 1 on error
extern u8 packet_set_packet (Packet *packet, void *data, size_t data_size);

// prepares the packet to be ready to be sent
extern u8 packet_generate (Packet *packet);

// generates a simple request packet of the requested type reday to be sent, 
// and with option to pass some data
extern Packet *packet_generate_request (PacketType packet_type, u32 req_type, 
	void *data, size_t data_size);

// sends a packet using the tcp protocol and the packet sock fd
// returns 0 on success, 1 on error
extern u8 packet_send_tcp (const Packet *packet, int flags, size_t *total_sent, bool raw);

// sends a packet using the udp protocol
// returns 0 on success, 1 on error
extern u8 packet_send_udp (const void *packet, size_t packet_size);

// sends a packet using its network values
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
extern u8 packet_send (const Packet *packet, int flags, size_t *total_sent, bool raw);

// check for packets with bad size, protocol, version, etc
extern u8 packet_check (Packet *packet);

#endif