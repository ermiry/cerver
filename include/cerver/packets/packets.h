#ifndef _CERVER_PACKETS_PACKETS_H_
#define _CERVER_PACKETS_PACKETS_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/freelist.h"

#include "cerver/config.h"

#include "cerver/network/network.h"
#include "cerver/packets/header.h"
#include "cerver/packets/serializer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Socket;
struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;

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
	XX(4, 	SUCCESS)						\
	XX(5, 	CLIENT_TOKEN)					\
	XX(6, 	ADMIN_TOKEN)

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

	// the class of the recycled buffer of data, 0 if it was not taken from them
	unsigned int data_class;

	// the next packet while it is kept to be reused
	struct _Packet *recycle_next;

};

typedef struct _Packet Packet;

// the released packets and buffers of data that are kept to be reused, in each class
#define PACKET_RECYCLE_LIMIT				FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

// each thread keeps some of them, so they are taken and released without a lock
#define PACKET_THREAD_CACHE_SIZE			16

// the buffers of data are recycled in classes of 2^6 to 2^12 bytes
#define PACKET_DATA_MIN_EXPONENT			6
#define PACKET_DATA_MAX_EXPONENT			12
#define PACKET_DATA_CLASSES					(PACKET_DATA_MAX_EXPONENT - PACKET_DATA_MIN_EXPONENT + 1)

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

// creates a packet ready to be sent with the object encoded by its schema as its data,
// its header has PACKET_FLAG_SERIALIZED
// returns NULL if the object can not be encoded
CERVER_EXPORT Packet *packet_create_serialized (
	const PacketType type, const u32 request_type,
	const SerializerSchema *schema, const void *object
);

// decodes the object of a packet that has PACKET_FLAG_SERIALIZED with its schema,
// the arena gives the memory of the arena fields, it can be NULL for a schema without them
// the object is only written when the result is SERIALIZER_RESULT_NONE
CERVER_EXPORT SerializerResult packet_get_object (
	const Packet *packet, const SerializerSchema *schema,
	Arena *arena, void *object
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
// a received packet has the protocol id and version of its header, a created one the local ones
// returns false on a bad packet
CERVER_EXPORT bool packet_check (const Packet *packet);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
