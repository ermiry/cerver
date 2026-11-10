#ifndef _CERVER_PACKETS_H_
#define _CERVER_PACKETS_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"
#include "cerver/network.h"

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

// copies the data from the source version to the destination
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 packet_version_copy (
	PacketVersion *dest, const PacketVersion *source
);

CERVER_PUBLIC void packet_version_print (
	const PacketVersion *version
);

#pragma endregion

#pragma region types

#define PACKETS_MAX_TYPES					16
#define PACKETS_CURRENT_TYPES				10

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
	XX(10, 	TEST)							\
	XX(11, 	BAD)

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
	const PacketsPerType *packets_per_type
);

CERVER_PUBLIC void packets_per_type_array_print (
	const u64 packets[PACKETS_MAX_TYPES]
);

#pragma endregion

#pragma region header

// the header travels in PACKET_HEADER_SIZE bytes in network byte order, field by field,
// so it never depends on the layout of PacketHeader in memory
#define PACKET_HEADER_SIZE							32

// the format of the header in the wire, a peer that sends another one is closed
#define PACKET_HEADER_FORMAT						1

#define PACKET_HEADER_PROTOCOL_ID_OFFSET			0
#define PACKET_HEADER_MAJOR_OFFSET					4
#define PACKET_HEADER_MINOR_OFFSET					6
#define PACKET_HEADER_PACKET_TYPE_OFFSET			8
#define PACKET_HEADER_HANDLER_ID_OFFSET				9
#define PACKET_HEADER_FORMAT_OFFSET					10
#define PACKET_HEADER_FLAGS_OFFSET					11
#define PACKET_HEADER_REQUEST_TYPE_OFFSET			12
#define PACKET_HEADER_ROUTE_ID_OFFSET				16
#define PACKET_HEADER_PACKET_SIZE_OFFSET			20
#define PACKET_HEADER_RESERVED_OFFSET				28

// the data of the packet is an object of the serializer
#define PACKET_FLAG_SERIALIZED						0x01

// a header with any other flag is a bad packet
#define PACKET_FLAGS_KNOWN							(PACKET_FLAG_SERIALIZED)

struct _PacketHeader {

	PacketType packet_type;		// the main packet type, a single byte in the wire
	u64 packet_size;			// total size of the packet (header + data)

	u8 handler_id;				// used in cervers with multiple app handlers
	u8 flags;					// PACKET_FLAG_*

	u32 request_type;			// the packet's subtype

	// the route of a client of a load balancer, to send the responses of a service back to it
	u32 route_id;

	// the protocol of the sender, packet_check () validates it with the local one
	PacketVersion version;

};

typedef struct _PacketHeader PacketHeader;

#define PACKET_HEADER_RESULT_MAP(XX)																\
	XX(0,	NONE,		"The header is valid")														\
	XX(1,	FORMAT,		"The header has a different format")										\
	XX(2,	FLAGS,		"The header has unknown flags")												\
	XX(3,	RESERVED,	"The reserved bytes of the header are not zero")

typedef enum PacketHeaderResult {

	#define XX(num, name, string) PACKET_HEADER_RESULT_##name = num,
	PACKET_HEADER_RESULT_MAP (XX)
	#undef XX

} PacketHeaderResult;

CERVER_PUBLIC const char *packet_header_result_string (
	const PacketHeaderResult result
);

// writes the header in the wire format, PACKET_HEADER_SIZE bytes in network byte order
CERVER_EXPORT void packet_header_encode (
	const PacketHeader *header, u8 buffer[PACKET_HEADER_SIZE]
);

// reads a header in the wire format, the buffer does not need to be aligned
// the header is only written when the result is PACKET_HEADER_RESULT_NONE
CERVER_EXPORT PacketHeaderResult packet_header_decode (
	const u8 buffer[PACKET_HEADER_SIZE], PacketHeader *header
);

// the protocol id and version of this process, to send them in every header
CERVER_PUBLIC PacketVersion packet_version_local (void);

CERVER_PUBLIC PacketHeader *packet_header_new (void);

CERVER_PUBLIC void packet_header_delete (PacketHeader *header);

CERVER_PUBLIC PacketHeader *packet_header_create (
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
);

// allocates a new packet header and copies the values from source
CERVER_PUBLIC PacketHeader *packet_header_create_from (
	const PacketHeader *source
);

// copies the data from the source header to the destination
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 packet_header_copy (
	PacketHeader *dest, const PacketHeader *source
);

CERVER_PUBLIC void packet_header_print (
	const PacketHeader *header
);

CERVER_PUBLIC void packet_header_log (
	const PacketHeader *header
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

	// used to handle big packets
	// that don't fit inside a single buffer
	size_t remaining_data;

	PacketHeader header;

	// a request packet sends its own header from here,
	// encoded again from header when it is sent
	u8 encoded_header[PACKET_HEADER_SIZE];

	PacketVersion version;

	// the actual packet to be sent
	size_t packet_size;
	void *packet;
	bool packet_ref;

	// called by packet_delete () before the packet is released,
	// like a received packet that holds a reference to its UDP peer
	void (*release) (void *release_args);
	void *release_args;

};

typedef struct _Packet Packet;

// allocates a new empty packet
CERVER_PUBLIC Packet *packet_new (void);

// correctly deletes a packet and all of its data
CERVER_PUBLIC void packet_delete (void *ptr);

// creates a new packet with the option to pass values directly
// data is copied into packet buffer and can be safely freed
CERVER_EXPORT Packet *packet_create (
	const PacketType type, const u32 req_type,
	const void *data, const size_t data_size
);

// creates a packet with a data buffer of the specified size
CERVER_PRIVATE Packet *packet_create_with_data (
	const size_t data_size
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
	Packet *packet, const PacketHeader *header
);

// sets the packet's header values
// if the packet does NOT yet have a header, it will be created
CERVER_EXPORT void packet_set_header_values (
	Packet *packet,
	PacketType packet_type, size_t packet_size,
	u8 handler_id, u32 request_type,
	u32 route_id
);

// sets the route id of the packet, like the one of a request that came through a load balancer,
// so the response goes back to its client; the header of a generated packet is encoded again
CERVER_EXPORT void packet_set_route_id (
	Packet *packet, const u32 route_id
);

// allocates the packet's data with size data_size
// data can be added using packet_add_data ()
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int packet_create_data (
	Packet *packet, const size_t data_size
);

// sets the data of the packet -> copies the data into the packet
// if the packet had data before it is deleted and replaced with the new one
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_set_data (
	Packet *packet,
	const void *data, const size_t data_size
);

// adds the data to the packet's existing data buffer
// the data size must be <= the packet's remaining data
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 packet_add_data (
	Packet *packet,
	const void *data, const size_t data_size
);

// appends the data to the end if the packet already has data
// if the packet is empty, creates a new buffer
// it creates a new copy of the data and the original can be safely freed
// this does not work if the data has been set using a reference
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_append_data (
	Packet *packet,
	const void *data, const size_t data_size
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

// creates a request packet that is ready to be sent
// returns a newly allocated packet
CERVER_EXPORT Packet *packet_create_request (
	const PacketType packet_type,
	const u32 request_type
);

// creates a new ping packet (PACKET_TYPE_TEST)
// returns a newly allocated packet
CERVER_EXPORT Packet *packet_create_ping (void);

// initializes an already allocated packet with request related values
CERVER_EXPORT void packet_init_request (
	Packet *packet,
	const PacketType packet_type,
	const u32 request_type
);

// initializes an already allocated packet as a ping request
CERVER_EXPORT void packet_init_ping (Packet *packet);

// generates a simple request packet of the requested type reday to be sent,
// and with option to pass some data
// returns a newly allocated packet that should be deleted after use
CERVER_EXPORT Packet *packet_generate_request (
	const PacketType packet_type, const u32 req_type,
	const void *data, const size_t data_size
);

// sends a packet using its network values
// raw flag to send a raw packet (only the data that was set to the packet, without any header)
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send (
	const Packet *packet, int flags, size_t *total_sent, bool raw
);

// sends the packet in a datagram without the token of its UDP connection,
// like the answers to an authentication, that the peer reads before it has the token
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 packet_send_without_datagram_token (const Packet *packet);

// sends a packet of a connection's send queue
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 packet_send_actual (
	const Packet *packet,
	int flags, size_t *total_sent,
	struct _Client *client, struct _Connection *connection
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

// sends a packet of selected types without any data
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_request (
	const PacketType packet_type,
	const u32 request_type,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

// sends a ping packet (PACKET_TYPE_TEST)
// returns 0 on success, 1 on error
CERVER_EXPORT u8 packet_send_ping (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

// routes a packet from one connection's sock fd to another connection's sock fd
// the header is sent first and then the packet's body (if any) is handled directly between fds
// by calling the splice method using a pipe as the middleman
// this method is thread safe, since it will block the socket until the entire packet has been routed
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 packet_route_between_connections (
	struct _Connection *from, struct _Connection *to,
	PacketHeader *header, size_t *sent
);

// check if packet has a compatible protocol id and a version:
// the same protocol id and major, and a minor that is not newer than the local one
// the version is not part of the packet header, so a received packet has the initial one
// returns false on a bad packet
CERVER_EXPORT bool packet_check (const Packet *packet);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
