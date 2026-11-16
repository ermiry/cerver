/**
 * @file cerver/packets/packets.h
 * @brief The packets of cerver: their subtypes, how they are created, filled and sent.
 */

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

/**
 * @defgroup packets Packets
 * @brief The packets that cerver sends and receives: their header, their types, their data and the serializer of
 * their objects.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct _Socket;
struct _Cerver;
struct _Client;
struct _Connection;
struct _Lobby;

/**
 * @defgroup packets_main Packet
 * @ingroup packets
 * @brief The subtypes of the packets of cerver and the struct of a packet.
 * @{
 */

/**
 * @brief The X-macro table of the request types of the PACKET_TYPE_CERVER packets: the number and the name of each one.
 */
#define CERVER_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	INFO)							\
	XX(2, 	TEARDOWN)

/**
 * @brief The request type of a PACKET_TYPE_CERVER packet, one `CERVER_PACKET_TYPE_<name>` for each row of
 * #CERVER_PACKET_TYPE_MAP.
 */
typedef enum CerverPacketType {

	#define XX(num, name) CERVER_PACKET_TYPE_##name = num,
	CERVER_PACKET_TYPE_MAP (XX)
	#undef XX

} CerverPacketType;

/**
 * @brief The X-macro table of the request types of the PACKET_TYPE_CLIENT packets: the number and the name of each one.
 */
#define CLIENT_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	CLOSE_CONNECTION)				\
	XX(2, 	DISCONNECT)

/**
 * @brief The request type of a PACKET_TYPE_CLIENT packet, one `CLIENT_PACKET_TYPE_<name>` for each row of
 * #CLIENT_PACKET_TYPE_MAP.
 */
typedef enum ClientPacketType {

	#define XX(num, name) CLIENT_PACKET_TYPE_##name = num,
	CLIENT_PACKET_TYPE_MAP (XX)
	#undef XX

} ClientPacketType;

/**
 * @brief The X-macro table of the request types of the PACKET_TYPE_REQUEST packets: the number and the name of each one.
 */
#define REQUEST_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	GET_FILE)						\
	XX(2, 	SEND_FILE)

/**
 * @brief The request type of a PACKET_TYPE_REQUEST packet, one `REQUEST_PACKET_TYPE_<name>` for each row of
 * #REQUEST_PACKET_TYPE_MAP.
 */
typedef enum RequestPacketType {

	#define XX(num, name) REQUEST_PACKET_TYPE_##name = num,
	REQUEST_PACKET_TYPE_MAP (XX)
	#undef XX

} RequestPacketType;

/**
 * @brief The X-macro table of the request types of the PACKET_TYPE_AUTH packets: the number and the name of each one.
 */
#define AUTH_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	REQUEST_AUTH)					\
	XX(2, 	CLIENT_AUTH)					\
	XX(3, 	ADMIN_AUTH)						\
	XX(4, 	SUCCESS)						\
	XX(5, 	CLIENT_TOKEN)					\
	XX(6, 	ADMIN_TOKEN)

/**
 * @brief The request type of a PACKET_TYPE_AUTH packet, one `AUTH_PACKET_TYPE_<name>` for each row of
 * #AUTH_PACKET_TYPE_MAP.
 */
typedef enum AuthPacketType {

	#define XX(num, name) AUTH_PACKET_TYPE_##name = num,
	AUTH_PACKET_TYPE_MAP (XX)
	#undef XX

} AuthPacketType;

/**
 * @brief The X-macro table of the request types of the PACKET_TYPE_GAME packets: the number and the name of each one.
 */
#define GAME_PACKET_TYPE_MAP(XX)			\
	XX(0, 	NONE)							\
	XX(1, 	GAME_INIT)						\
	XX(2, 	GAME_START)						\
	XX(3, 	LOBBY_CREATE)					\
	XX(4, 	LOBBY_JOIN)						\
	XX(5, 	LOBBY_LEAVE)					\
	XX(6, 	LOBBY_UPDATE)					\
	XX(7, 	LOBBY_DESTROY)					\

/**
 * @brief The request type of a PACKET_TYPE_GAME packet, one `GAME_PACKET_TYPE_<name>` for each row of
 * #GAME_PACKET_TYPE_MAP.
 */
typedef enum GamePacketType {

	#define XX(num, name) GAME_PACKET_TYPE_##name = num,
	GAME_PACKET_TYPE_MAP (XX)
	#undef XX

} GamePacketType;

/**
 * @brief A packet: its network values, its data and the buffer that is sent.
 */
struct _Packet {

	/** @brief The cerver that the packet is from or goes to. */
	struct _Cerver *cerver;
	/** @brief The client that the packet is from or goes to. */
	struct _Client *client;
	/** @brief The connection that the packet is from or goes to. */
	struct _Connection *connection;
	/** @brief The lobby of the packet. */
	struct _Lobby *lobby;

	/** @brief The packet type. */
	PacketType packet_type;
	/** @brief The request type. */
	u32 req_type;

	/** @brief The size of the data. */
	size_t data_size;
	/** @brief The data, without the header. */
	void *data;
	/** @brief Where the data is read from. */
	char *data_ptr;
	/** @brief The end of the data that was added. */
	char *data_end;
	/** @brief The data is a reference that the packet does not release. */
	bool data_ref;

	/** @brief The room that is left in the data, for the packets that come in many buffers. */
	size_t remaining_data;

	/** @brief The header. */
	PacketHeader header;

	/** @brief A request packet sends its own header from here, encoded again from #header when it is sent. */
	u8 encoded_header[PACKET_HEADER_SIZE];

	/** @brief The protocol of the packet: the one of its header when it was received, the local one when it was created. */
	PacketVersion version;

	/** @brief The size of the buffer that is sent. */
	size_t packet_size;
	/** @brief The buffer that is sent: the encoded header and the data. */
	void *packet;
	/** @brief The buffer is a reference that the packet does not release. */
	bool packet_ref;

	/** @brief Called by packet_delete () before the packet is released, like a received packet that holds a reference to its UDP peer. */
	void (*release) (void *release_args);
	/** @brief The argument of #release. */
	void *release_args;

	/** @brief The class of the recycled buffer of the data, 0 if it was not taken from them. */
	unsigned int data_class;

	/** @brief The next packet while it is kept to be reused. */
	struct _Packet *recycle_next;

};

/** @brief A packet, see struct _Packet. */
typedef struct _Packet Packet;

/** @brief The released packets and buffers of data that are kept to be reused, in each class. */
#define PACKET_RECYCLE_LIMIT				FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

/** @brief The packets that each thread keeps, so they are taken and released without a lock. */
#define PACKET_THREAD_CACHE_SIZE			16

/** @brief The buffers of data are recycled in classes from 2^6 bytes. */
#define PACKET_DATA_MIN_EXPONENT			6
/** @brief The buffers of data are recycled in classes up to 2^12 bytes. */
#define PACKET_DATA_MAX_EXPONENT			12
/** @brief The classes of the recycled buffers of data. */
#define PACKET_DATA_CLASSES					(PACKET_DATA_MAX_EXPONENT - PACKET_DATA_MIN_EXPONENT + 1)

/** @} */

/**
 * @defgroup packets_create Create
 * @ingroup packets
 * @brief Creating, filling and generating packets.
 * @{
 */

/**
 * @brief Allocates (or reuses) an empty packet, with the local protocol.
 *
 * @return the new packet, released with packet_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC Packet *packet_new (void);

/**
 * @brief Releases a packet, its data and its buffer, unless they are references; it calls its release first.
 *
 * @param[in] ptr the packet, it can be NULL
 */
CERVER_PUBLIC void packet_delete (void *ptr);

/**
 * @brief Creates a packet with its types and a copy of the data.
 *
 * @param[in] type the packet type
 * @param[in] req_type the request type
 * @param[in] data the data, copied into the packet; NULL for a packet without data
 * @param[in] data_size the size of the data
 * @return the new packet, NULL if it or the copy of its data could not be allocated
 */
CERVER_EXPORT Packet *packet_create (
	const PacketType type, const u32 req_type,
	const void *data, const size_t data_size
);

/**
 * @brief Creates a packet with an empty buffer of data, to be filled with packet_add_data ().
 *
 * @param[in] data_size the size of the data, 0 for none
 * @return the new packet, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE Packet *packet_create_with_data (
	const size_t data_size
);

/**
 * @brief Creates a packet ready to be sent, with the object encoded by its schema as its data.
 *
 * Its header has #PACKET_FLAG_SERIALIZED.
 *
 * @param[in] type the packet type
 * @param[in] request_type the request type
 * @param[in] schema the schema of the object
 * @param[in] object the object
 * @return the new packet, NULL if the object can not be encoded or on error
 */
CERVER_EXPORT Packet *packet_create_serialized (
	const PacketType type, const u32 request_type,
	const SerializerSchema *schema, const void *object
);

/**
 * @brief Decodes with its schema the object of a packet that has #PACKET_FLAG_SERIALIZED.
 *
 * @param[in] packet the packet
 * @param[in] schema the schema of the object
 * @param[in,out] arena gives the memory of the arena fields; it can be NULL for a schema without them
 * @param[out] object the object, only written when the result is SERIALIZER_RESULT_NONE
 * @return SERIALIZER_RESULT_NONE when it was decoded, SERIALIZER_RESULT_ARGUMENTS for a NULL argument,
 * SERIALIZER_RESULT_TYPE for a packet that is not serialized or has no data, or what is wrong with the object
 */
CERVER_EXPORT SerializerResult packet_get_object (
	const Packet *packet, const SerializerSchema *schema,
	Arena *arena, void *object
);

/**
 * @brief Sets the destination of the packet.
 *
 * @param[in,out] packet the packet, it can be NULL
 * @param[in] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection that it is sent to
 * @param[in] lobby the lobby
 */
CERVER_EXPORT void packet_set_network_values (
	Packet *packet,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

/**
 * @brief Copies the values of a header into the header of the packet.
 *
 * @param[in,out] packet the packet
 * @param[in] header the header; NULL keeps the current one
 */
CERVER_EXPORT void packet_set_header (
	Packet *packet, const PacketHeader *header
);

/**
 * @brief Sets the values of the header of the packet.
 *
 * @param[in,out] packet the packet, it can be NULL
 * @param[in] packet_type the packet type
 * @param[in] packet_size the total size of the packet (header and data)
 * @param[in] handler_id the app handler
 * @param[in] request_type the request type
 * @param[in] route_id the route id
 */
CERVER_EXPORT void packet_set_header_values (
	Packet *packet,
	PacketType packet_type, size_t packet_size,
	u8 handler_id, u32 request_type,
	u32 route_id
);

/**
 * @brief Sets the route id of the packet, like the one of a request that came through a load balancer, so the
 * response goes back to its client.
 *
 * The header of a generated packet is encoded again.
 *
 * @param[in,out] packet the packet, it can be NULL
 * @param[in] route_id the route id
 */
CERVER_EXPORT void packet_set_route_id (
	Packet *packet, const u32 route_id
);

/**
 * @brief Allocates the data of the packet, to be filled with packet_add_data ().
 *
 * @param[in,out] packet the packet
 * @param[in] data_size the size of the data
 * @retval 0 the data was allocated
 * @retval 1 a NULL packet, a size of 0 or it could not be allocated
 */
CERVER_PUBLIC unsigned int packet_create_data (
	Packet *packet, const size_t data_size
);

/**
 * @brief Sets the data of the packet with a copy; the data that it had is released.
 *
 * @param[in,out] packet the packet
 * @param[in] data the data
 * @param[in] data_size the size of the data
 * @retval 0 the data was copied
 * @retval 1 a NULL argument or it could not be allocated
 */
CERVER_EXPORT u8 packet_set_data (
	Packet *packet,
	const void *data, const size_t data_size
);

/**
 * @brief Adds data to the room that is left in the data of the packet (packet_create_data ()).
 *
 * @param[in,out] packet the packet
 * @param[in] data the data
 * @param[in] data_size the size of the data, at most the remaining room
 * @retval 0 the data was added
 * @retval 1 a NULL argument or the data does not fit
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 packet_add_data (
	Packet *packet,
	const void *data, const size_t data_size
);

/**
 * @brief Appends a copy of the data to the end of the data of the packet, or creates it if the packet is empty.
 *
 * @param[in,out] packet the packet
 * @param[in] data the data, it can be freed after the call
 * @param[in] data_size the size of the data
 * @retval 0 the data was appended
 * @retval 1 a NULL argument, a total size that does not fit in a size_t, or it could not be allocated
 * @note When the buffer can not grow, the packet keeps its data as it was.
 * @warning It does not work after packet_set_data_ref ().
 */
CERVER_EXPORT u8 packet_append_data (
	Packet *packet,
	const void *data, const size_t data_size
);

/**
 * @brief Sets a reference to a buffer as the data of the packet; it is not copied nor released.
 *
 * Useful to send raw data, like a JSON body to a peer that is not cerver, with packet_send () and the raw flag.
 *
 * @param[in,out] packet the packet
 * @param[in] data the data, it must live while the packet uses it
 * @param[in] data_size the size of the data
 * @retval 0 the reference was set
 * @retval 1 a NULL argument
 */
CERVER_EXPORT u8 packet_set_data_ref (
	Packet *packet, void *data, size_t data_size
);

/**
 * @brief Sets the buffer that is sent with a copy of the data, that must already have its header (otherwise send it
 * with the raw flag); the buffer that it had is released.
 *
 * @param[in,out] packet the packet
 * @param[in] data the buffer, it can be freed after the call
 * @param[in] data_size the size of the buffer
 * @retval 0 the buffer was copied
 * @retval 1 a NULL argument or it could not be allocated
 */
CERVER_EXPORT u8 packet_set_packet (
	Packet *packet, void *data, size_t data_size
);

/**
 * @brief Sets a reference to a buffer as the buffer that is sent; it is not copied nor released.
 *
 * Useful to generate a cerver packet by hand.
 *
 * @param[in,out] packet the packet
 * @param[in] data the buffer, with its header; it must live while the packet uses it
 * @param[in] packet_size the size of the buffer
 * @retval 0 the reference was set
 * @retval 1 a NULL argument
 */
CERVER_EXPORT u8 packet_set_packet_ref (
	Packet *packet, void *data, size_t packet_size
);

/**
 * @brief Generates the buffer that is sent: the header, with the types, the size and the local protocol, and a copy
 * of the data.
 *
 * @param[in,out] packet the packet
 * @retval 0 the buffer was generated
 * @retval 1 a NULL packet or it could not be allocated
 * @warning Do not call it after setting the buffer with packet_set_packet () or packet_set_packet_ref (): it
 * replaces it.
 */
CERVER_EXPORT u8 packet_generate (Packet *packet);

/**
 * @brief Creates a request packet without data, ready to be sent: its buffer is its own encoded header.
 *
 * @param[in] packet_type the packet type
 * @param[in] request_type the request type
 * @return the new packet, released with packet_delete (); NULL if it could not be allocated
 */
CERVER_EXPORT Packet *packet_create_request (
	const PacketType packet_type,
	const u32 request_type
);

/**
 * @brief Creates a ping packet (PACKET_TYPE_TEST), ready to be sent.
 *
 * @return the new packet, released with packet_delete (); NULL if it could not be allocated
 */
CERVER_EXPORT Packet *packet_create_ping (void);

/**
 * @brief Initializes an already allocated packet, like one in the stack, as a request without data ready to be sent.
 *
 * @param[out] packet the packet, it must not be NULL; every value is overwritten
 * @param[in] packet_type the packet type
 * @param[in] request_type the request type
 */
CERVER_EXPORT void packet_init_request (
	Packet *packet,
	const PacketType packet_type,
	const u32 request_type
);

/**
 * @brief Initializes an already allocated packet as a ping request (PACKET_TYPE_TEST).
 *
 * @param[out] packet the packet, it must not be NULL
 */
CERVER_EXPORT void packet_init_ping (Packet *packet);

/**
 * @brief Creates and generates a request packet with a copy of the data, ready to be sent.
 *
 * @param[in] packet_type the packet type
 * @param[in] req_type the request type
 * @param[in] data the data, NULL for none
 * @param[in] data_size the size of the data
 * @return the new packet, released with packet_delete (); NULL on error
 */
CERVER_EXPORT Packet *packet_generate_request (
	const PacketType packet_type, const u32 req_type,
	const void *data, const size_t data_size
);

/** @} */

/**
 * @defgroup packets_send Send
 * @ingroup packets
 * @brief Sending, routing and checking packets.
 * @{
 */

/**
 * @brief Sends a packet to the connection of its network values, with the write mutex of its socket.
 *
 * @param[in] packet the packet, generated
 * @param[in] flags the flags of the send
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @param[in] raw true to send only the data of the packet, without any header
 * @retval 0 the packet was sent
 * @retval 1 a NULL packet, a packet without a connection, or the send failed
 */
CERVER_EXPORT u8 packet_send (
	const Packet *packet, int flags, size_t *total_sent, bool raw
);

/**
 * @brief Sends the packet in a datagram without the token of its UDP connection, like the answers to an
 * authentication, that the peer reads before it has the token.
 *
 * @param[in] packet the packet
 * @retval 0 the packet was sent
 * @retval 1 a NULL packet, one without a connection, or the send failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 packet_send_without_datagram_token (const Packet *packet);

/**
 * @brief Sends a packet of the send queue of a connection.
 *
 * @param[in] packet the packet
 * @param[in] flags the flags of the send
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @param[in] client the client of the connection
 * @param[in] connection the connection, it must not be NULL
 * @retval 0 the packet was sent
 * @retval 1 the send failed
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 packet_send_actual (
	const Packet *packet,
	int flags, size_t *total_sent,
	struct _Client *client, struct _Connection *connection
);

/**
 * @brief Works as packet_send () but without locking the write mutex of the socket, for a caller that holds it.
 *
 * @param[in] packet the packet, generated
 * @param[in] flags the flags of the send
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @param[in] raw true to send only the data of the packet, without any header
 * @retval 0 the packet was sent
 * @retval 1 a NULL packet, a packet without a connection, or the send failed
 * @threadsafety The caller must hold the write mutex of the socket of the connection.
 */
CERVER_PUBLIC u8 packet_send_unsafe (
	const Packet *packet, int flags, size_t *total_sent, bool raw
);

/**
 * @brief Sends a packet to a destination, with the flags in 0.
 *
 * @param[in] packet the packet, generated
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @param[in] raw true to send only the data of the packet, without any header
 * @param[in] cerver the cerver, for its stats
 * @param[in] client the client, for its stats
 * @param[in] connection the connection, required
 * @param[in] lobby the lobby, for its stats
 * @retval 0 the packet was sent
 * @retval 1 a NULL packet or connection, or the send failed
 */
CERVER_EXPORT u8 packet_send_to (
	const Packet *packet,
	size_t *total_sent, bool raw,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

/**
 * @brief Sends a packet in two parts, first the header and then the data, with the write mutex of the socket.
 *
 * Useful to forward a big received packet without copying it into a single buffer.
 *
 * @param[in] packet the packet
 * @param[in] flags the flags of the send
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @retval 0 the packet was sent
 * @retval 1 a NULL packet, a packet without a connection, or the send failed
 */
CERVER_EXPORT u8 packet_send_split (
	const Packet *packet, int flags, size_t *total_sent
);

/**
 * @brief Works as packet_send_split () to a destination, with the flags in 0.
 *
 * @param[in] packet the packet
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @param[in] cerver the cerver, for its stats
 * @param[in] client the client, for its stats
 * @param[in] connection the connection, required
 * @param[in] lobby the lobby, for its stats
 * @retval 0 the packet was sent
 * @retval 1 a NULL packet or connection, or the send failed
 */
CERVER_EXPORT u8 packet_send_to_split (
	const Packet *packet,
	size_t *total_sent,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

/**
 * @brief Sends the header of the packet and then each buffer as it is, with the write mutex of the socket for the
 * whole send.
 *
 * @param[in] packet the packet, with its header and its connection
 * @param[in] pieces the buffers
 * @param[in] sizes the size of each buffer
 * @param[in] n_pieces the number of buffers
 * @param[in] flags the flags of the send
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @retval 0 the packet was sent
 * @retval 1 a NULL argument, a packet without a connection, or a send failed
 */
CERVER_EXPORT u8 packet_send_pieces (
	const Packet *packet,
	void **pieces, size_t *sizes, u32 n_pieces,
	int flags,
	size_t *total_sent
);

/**
 * @brief Sends a packet directly to a socket, with its write mutex and a deadline of #CERVER_DEFAULT_SEND_TIMEOUT.
 *
 * @param[in] packet the packet, generated
 * @param[in] socket the socket
 * @param[in] flags the flags of the send
 * @param[out] total_sent the bytes that were sent, it can be NULL
 * @param[in] raw true to send only the data of the packet, without any header
 * @retval 0 the packet was sent
 * @retval 1 a NULL argument or the send failed
 */
CERVER_EXPORT u8 packet_send_to_socket (
	const Packet *packet,
	struct _Socket *socket, int flags, size_t *total_sent, bool raw
);

/**
 * @brief Sends a packet of some types without data.
 *
 * @param[in] packet_type the packet type
 * @param[in] request_type the request type
 * @param[in] cerver the cerver, for its stats
 * @param[in] client the client, for its stats
 * @param[in] connection the connection, required
 * @param[in] lobby the lobby, for its stats
 * @retval 0 the whole header was sent
 * @retval 1 a NULL connection or the send failed
 */
CERVER_EXPORT u8 packet_send_request (
	const PacketType packet_type,
	const u32 request_type,
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

/**
 * @brief Sends a ping packet (PACKET_TYPE_TEST).
 *
 * @param[in] cerver the cerver, for its stats
 * @param[in] client the client, for its stats
 * @param[in] connection the connection, required
 * @param[in] lobby the lobby, for its stats
 * @retval 0 the ping was sent
 * @retval 1 a NULL connection or the send failed
 */
CERVER_EXPORT u8 packet_send_ping (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	struct _Lobby *lobby
);

/**
 * @brief Routes a packet from the socket of one connection to the one of another.
 *
 * The header is sent first, and then the body moves between the fds with splice () through a pipe.
 *
 * @param[in] from the connection that the body is read from
 * @param[in] to the connection that the packet is sent to
 * @param[in] header the header of the packet
 * @param[out] sent the bytes that were sent, it can be NULL
 * @retval 0 the whole packet was routed
 * @retval 1 a NULL argument or on error
 * @threadsafety It holds the write mutex of the socket of to until the whole packet has been routed.
 */
CERVER_PUBLIC u8 packet_route_between_connections (
	struct _Connection *from, struct _Connection *to,
	PacketHeader *header, size_t *sent
);

/**
 * @brief Checks that a packet has a compatible protocol: the same protocol id and major version, and a minor
 * version that is not newer than the local one.
 *
 * A received packet has the protocol of its header, a created one the local one.
 *
 * @param[in] packet the packet
 * @return true for a compatible packet, false for a bad packet or NULL
 */
CERVER_EXPORT bool packet_check (const Packet *packet);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
