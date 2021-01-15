#ifndef _CERVER_PACKETS_H_
#define _CERVER_PACKETS_H_

#include <stdlib.h>
#include <stdbool.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/client.h"
#include "cerver/network.h"

#include "cerver/game/lobby.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Socket;
struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;

#pragma region protocol

typedef u32 ProtocolID;

// gets the current protocol id set in your application
CERVER_EXPORT ProtocolID packets_get_protocol_id (void);

// Sets the protocol id that this cerver will use for its packets.
// The protocol id is a unique number that you can set to only accept packets that are comming from your application
// If the protocol id coming from the cerver don't match your application's, it will be considered a bad packet
// This value is only cheked if you enable packet checking for your cerver
CERVER_EXPORT void packets_set_protocol_id (ProtocolID protocol_id);

typedef struct ProtocolVersion {

	u16 major;
	u16 minor;

} ProtocolVersion;

// gets the current version set in your application
CERVER_EXPORT ProtocolVersion packets_get_protocol_version (void);

// Sets the protocol version for the cerver.
// The version is an identifier to help you manage different versions of your deployed applications
// If the versions of your packet don't match, it will be considered a bad packet
// This value is only cheked if you enable packet checking for your cerver
CERVER_EXPORT void packets_set_protocol_version (ProtocolVersion version);

#pragma endregion

#pragma region version

struct _PacketVersion {

	ProtocolID protocol_id;
	ProtocolVersion protocol_version;

};

typedef struct _PacketVersion PacketVersion;

CERVER_PUBLIC PacketVersion *packet_version_new (void);

CERVER_PUBLIC void packet_version_delete (PacketVersion *version);

CERVER_PUBLIC PacketVersion *packet_version_create (void);

CERVER_PUBLIC void packet_version_print (PacketVersion *version);

#pragma endregion

#pragma region types

#define PACKET_TYPE_MAP(XX)					\
	XX(0, 	NONE)							\
	XX(1, 	CERVER)							\
	XX(2, 	CLIENT)							\
	XX(3, 	ERROR)							\
	XX(4, 	REQUEST)						\
	XX(5, 	AUTH)							\
	XX(6, 	GAME)							\
	XX(7, 	APP)							\
	XX(8, 	APP_ERROR)						\
	XX(9, 	CUSTOM)							\
	XX(10, 	TEST)

// these indicate what type of packet we are sending/recieving
typedef enum PacketType {

	#define XX(num, name) PACKET_TYPE_##name = num,
	PACKET_TYPE_MAP (XX)
	#undef XX

} PacketType;

struct _PacketsPerType {

	u64 n_cerver_packets;
	u64 n_client_packets;
	u64 n_error_packets;
	u64 n_request_packets;
	u64 n_auth_packets;
	u64 n_game_packets;
	u64 n_app_packets;
	u64 n_app_error_packets;
	u64 n_custom_packets;
	u64 n_test_packets;
	u64 n_unknown_packets;

	u64 n_bad_packets;

};

typedef struct _PacketsPerType PacketsPerType;

CERVER_PUBLIC PacketsPerType *packets_per_type_new (void);

CERVER_PUBLIC void packets_per_type_delete (
	void *packets_per_type_ptr
);

CERVER_PUBLIC void packets_per_type_print (
	PacketsPerType *packets_per_type
);

#pragma endregion

#pragma region header

struct _PacketHeader {

	PacketType packet_type;		// the main packet type
	size_t packet_size;			// total size of the packet (header + data)

	u8 handler_id;				// used in cervers with multiple app handlers

	u32 request_type;			// the packet's subtype

	u16 sock_fd;				// used in when working with load balancers

};

typedef struct _PacketHeader PacketHeader;

CERVER_PUBLIC PacketHeader *packet_header_new (void);

CERVER_PUBLIC void packet_header_delete (
	PacketHeader *header
);

CERVER_PUBLIC PacketHeader *packet_header_create (
	PacketType packet_type, size_t packet_size, u32 req_type
);

// prints an already existing PacketHeader
// mostly used for debugging
CERVER_PUBLIC void packet_header_print (
	PacketHeader *header
);

// allocates space for the dest packet header and copies the data from source
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 packet_header_copy (
	PacketHeader **dest, PacketHeader *source
);

#pragma endregion

#pragma region packets

#define CERVER_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	INFO)							\
	XX(2, 	TEARDOWN)

typedef enum CerverPacketType {

	#define XX(num, name) CERVER_PACKET_TYPE_##name = num,
	CERVER_PACKET_TYPE_MAP (XX)
	#undef XX

} CerverPacketType;

#define CLIENT_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	CLOSE_CONNECTION)				\
	XX(2, 	DISCONNECT)

typedef enum ClientPacketType {

	#define XX(num, name) CLIENT_PACKET_TYPE_##name = num,
	CLIENT_PACKET_TYPE_MAP (XX)
	#undef XX

} ClientPacketType;

#define REQUEST_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	GET_FILE)						\
	XX(2, 	SEND_FILE)

typedef enum RequestPacketType {

	#define XX(num, name) REQUEST_PACKET_TYPE_##name = num,
	REQUEST_PACKET_TYPE_MAP (XX)
	#undef XX

} RequestPacketType;

#define AUTH_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	REQUEST_AUTH)					\
	XX(2, 	CLIENT_AUTH)					\
	XX(3, 	ADMIN_AUTH)						\
	XX(4, 	SUCCESS)

typedef enum AuthPacketType {

	#define XX(num, name) AUTH_PACKET_TYPE_##name = num,
	AUTH_PACKET_TYPE_MAP (XX)
	#undef XX

} AuthPacketType;

#define GAME_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	GAME_INIT)						\
	XX(2, 	GAME_START)						\
	XX(3, 	LOBBY_CREATE)					\
	XX(4, 	LOBBY_JOIN)						\
	XX(5, 	LOBBY_LEAVE)					\
	XX(6, 	LOBBY_UPDATE)					\
	XX(7, 	LOBBY_DESTROY)					\

typedef enum GamePacketType {

	#define XX(num, name) GAME_PACKET_TYPE_##name = num,
	GAME_PACKET_TYPE_MAP (XX)
	#undef XX

} GamePacketType;

struct _Packet {

	// the cerver and client the packet is from
	struct _Cerver *cerver;
	struct _Client *client;
	struct _Connection *connection;
	struct _Lobby *lobby;

	PacketType packet_type;
	u32 req_type;

	// serilized data
	size_t data_size;
	void *data;
	char *data_ptr;
	char *data_end;
	bool data_ref;

	// the actual packet to be sent
	PacketHeader *header;
	PacketVersion *version;
	size_t packet_size;
	void *packet;
	bool packet_ref;

};

typedef struct _Packet Packet;

// allocates a new empty packet
CERVER_PUBLIC Packet *packet_new (void);

// correctly deletes a packet and all of its data
CERVER_PUBLIC void packet_delete (void *ptr);

// creates a new packet with the option to pass values directly
// data is copied into packet buffer and can be safely freed
CERVER_EXPORT Packet *packet_create (
	PacketType type, void *data, size_t data_size
);

// sets the packet destinatary to whom this packet is going to be sent
CERVER_EXPORT void packet_set_network_values (
	Packet *packet,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

// sets the packet's header
// copies the header's values into the packet
CERVER_EXPORT void packet_set_header (
	Packet *packet, PacketHeader *header
);

// sets the packet's header values
// if the packet does NOT yet have a header, it will be created
CERVER_EXPORT void packet_set_header_values (
	Packet *packet,
	PacketType packet_type, size_t packet_size,
	u8 handler_id, u32 request_type,
	u16 sock_fd
);

// sets the data of the packet -> copies the data into the packet
// if the packet had data before it is deleted and replaced with the new one
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_set_data (
	Packet *packet, void *data, size_t data_size
);

// appends the data to the end if the packet already has data
// if the packet is empty, creates a new buffer
// it creates a new copy of the data and the original can be safely freed
// this does not work if the data has been set using a reference
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_append_data (
	Packet *packet, void *data, size_t data_size
);

// sets a reference to a data buffer to send
// data will not be copied into the packet and will not be freed after use
// this method is usefull for example if you just want to send a raw json packet to a non-cerver
// use this method with packet_send () with the raw flag on
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_set_data_ref (
	Packet *packet, void *data, size_t data_size
);

// sets a packet's packet by copying the passed data, so you will be able to free your data
// this data is expected to already contain a header, otherwise, send with raw flag
// deletes the previuos packet's packet
// returns 0 on succes, 1 on error
CERVER_EXPORT u8 packet_set_packet (
	Packet *packet, void *data, size_t data_size
);

// sets a reference to a data buffer to send as the packet
// data will not be copied into the packet and will not be freed after use
// usefull when you need to generate your own cerver type packet by hand
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_set_packet_ref (
	Packet *packet, void *data, size_t packet_size
);

// prepares the packet to be ready to be sent
// WARNING: dont call this method if you have set the packet directly
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_generate (Packet *packet);

// generates a simple request packet of the requested type reday to be sent,
// and with option to pass some data
// returns a newly allocated packet that should be deleted after use
CERVER_EXPORT Packet *packet_generate_request (
	PacketType packet_type, u32 req_type,
	void *data, size_t data_size
);

// sends a packet using its network values
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send (
	const Packet *packet, int flags, size_t *total_sent, bool raw
);

// works just as packet_send () but the socket's write mutex won't be locked
// useful when you need to lock the mutex manually
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 packet_send_unsafe (
	const Packet *packet, int flags, size_t *total_sent, bool raw
);

// sends a packet to the specified destination
// sets flags to 0
// at least a packet & an active connection are required for this method to succeed
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_to (
	const Packet *packet,
	size_t *total_sent, bool raw,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

// sends a packet to the socket in two parts, first the header & then the data
// this method can be useful when trying to forward a big received packet without the overhead of
// performing and additional copy to create a continuos data (packet) buffer
// the socket's write mutex will be locked to ensure that the packet
// is sent correctly and to avoid race conditions
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_split (
	const Packet *packet, int flags, size_t *total_sent
);

// sends a packet to the socket in two parts, first the header & then the data
// works just as packet_send_split () but with the flags set to 0
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_to_split (
	const Packet *packet,
	size_t *total_sent,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

// sends a packet in pieces, taking the header from the packet's field
// sends each buffer as they are with they respective sizes
// socket mutex will be locked for the entire operation
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_pieces (
	const Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces,
	int flags,
	size_t *total_sent
);

// sends a packet directly to the socket
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_to_socket (
	const Packet *packet,
	struct _Socket *socket, int flags, size_t *total_sent, bool raw
);

// sends a ping packet (PACKET_TYPE_TEST)
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_ping (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

// check if packet has a compatible protocol id and a version
// returns false on a bad packet
CERVER_EXPORT bool packet_check (Packet *packet);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif