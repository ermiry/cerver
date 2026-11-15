/**
 * @file cerver/packets/header.h
 * @brief The protocol of the packets, their types and their header in the wire.
 */

#ifndef _CERVER_PACKETS_HEADER_H_
#define _CERVER_PACKETS_HEADER_H_

#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/network/network.h"
#include "cerver/packets/serializer.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Packet;

/**
 * @defgroup packets_protocol Protocol
 * @ingroup packets
 * @brief The protocol id and version of the process, that every header carries.
 * @{
 */

/** @brief The id of the protocol of an application. */
typedef u32 ProtocolID;

/**
 * @brief Returns the protocol id of this process.
 *
 * @return the protocol id, 0 until packets_set_protocol_id ()
 */
CERVER_EXPORT ProtocolID packets_get_protocol_id (void);

/**
 * @brief Sets the protocol id that this process sends in its packets.
 *
 * The protocol id is a number that only the packets of your application have: with the packet checks of the cerver
 * enabled (cerver_set_check_packets ()), a packet with another id is a bad packet.
 *
 * @param[in] protocol_id the protocol id
 * @threadsafety A global of the process without synchronization: set it before cerver starts its threads.
 */
CERVER_EXPORT void packets_set_protocol_id (ProtocolID protocol_id);

/**
 * @brief The version of the protocol of an application.
 */
typedef struct ProtocolVersion {

	/** @brief The major version. */
	u16 major;
	/** @brief The minor version. */
	u16 minor;

} ProtocolVersion;

/**
 * @brief Returns the protocol version of this process.
 *
 * @return the version, 0.0 until packets_set_protocol_version ()
 */
CERVER_EXPORT ProtocolVersion packets_get_protocol_version (void);

/**
 * @brief Sets the protocol version that this process sends in its packets.
 *
 * The version helps to manage different versions of your deployed applications: with the packet checks of the
 * cerver enabled, a packet with another version is a bad packet.
 *
 * @param[in] version the version
 * @threadsafety A global of the process without synchronization: set it before cerver starts its threads.
 */
CERVER_EXPORT void packets_set_protocol_version (ProtocolVersion version);

/** @} */

/**
 * @defgroup packets_version Version
 * @ingroup packets
 * @brief The protocol id and version of a packet.
 * @{
 */

/**
 * @brief The protocol id and version of a packet.
 */
struct _PacketVersion {

	/** @brief The protocol id. */
	ProtocolID protocol_id;
	/** @brief The protocol version. */
	ProtocolVersion protocol_version;

};

/** @brief The protocol of a packet, see struct _PacketVersion. */
typedef struct _PacketVersion PacketVersion;

/**
 * @brief Allocates a version in zero.
 *
 * @return the new version, NULL if it could not be allocated
 */
CERVER_PUBLIC PacketVersion *packet_version_new (void);

/**
 * @brief Releases a version.
 *
 * @param[in] version the version, it can be NULL
 */
CERVER_PUBLIC void packet_version_delete (PacketVersion *version);

/**
 * @brief Allocates a version with the protocol id and version of this process.
 *
 * @return the new version, NULL if it could not be allocated
 */
CERVER_PUBLIC PacketVersion *packet_version_create (void);

/**
 * @brief Copies a version.
 *
 * @param[out] dest the destination
 * @param[in] source the source
 * @retval 0 the version was copied
 * @retval 1 a NULL argument
 */
CERVER_PUBLIC u8 packet_version_copy (
	PacketVersion *dest, const PacketVersion *source
);

/**
 * @brief Prints a version to stdout.
 *
 * @param[in] version the version, it can be NULL
 */
CERVER_PUBLIC void packet_version_print (
	const PacketVersion *version
);

/** @} */

/**
 * @defgroup packets_types Types
 * @ingroup packets
 * @brief The types of the packets and their counts.
 * @{
 */

/** @brief The size of the arrays of counts by packet type. */
#define PACKETS_MAX_TYPES					16

/** @brief The packet types that cerver uses today. */
#define PACKETS_CURRENT_TYPES				10

/**
 * @brief The X-macro table of the packet types: the number and the name of each one.
 */
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

/**
 * @brief What a packet is, one `PACKET_TYPE_<name>` for each row of #PACKET_TYPE_MAP.
 */
typedef enum PacketType {

	#define XX(num, name) PACKET_TYPE_##name = num,
	PACKET_TYPE_MAP (XX)
	#undef XX

} PacketType;

/**
 * @brief The count of the packets of each type.
 */
struct _PacketsPerType {

	/** @brief The PACKET_TYPE_CERVER packets. */
	u64 n_cerver_packets;
	/** @brief The PACKET_TYPE_CLIENT packets. */
	u64 n_client_packets;
	/** @brief The PACKET_TYPE_ERROR packets. */
	u64 n_error_packets;
	/** @brief The PACKET_TYPE_REQUEST packets. */
	u64 n_request_packets;
	/** @brief The PACKET_TYPE_AUTH packets. */
	u64 n_auth_packets;
	/** @brief The PACKET_TYPE_GAME packets. */
	u64 n_game_packets;
	/** @brief The PACKET_TYPE_APP packets. */
	u64 n_app_packets;
	/** @brief The PACKET_TYPE_APP_ERROR packets. */
	u64 n_app_error_packets;
	/** @brief The PACKET_TYPE_CUSTOM packets. */
	u64 n_custom_packets;
	/** @brief The PACKET_TYPE_TEST packets. */
	u64 n_test_packets;
	/** @brief The packets of an unknown type. */
	u64 n_unknown_packets;

	/** @brief The bad packets. */
	u64 n_bad_packets;

};

/** @brief The count of the packets of each type, see struct _PacketsPerType. */
typedef struct _PacketsPerType PacketsPerType;

/**
 * @brief Allocates the counts in zero.
 *
 * @return the new counts, NULL if they could not be allocated
 */
CERVER_PUBLIC PacketsPerType *packets_per_type_new (void);

/**
 * @brief Releases the counts.
 *
 * @param[in] packets_per_type_ptr the counts, they can be NULL
 */
CERVER_PUBLIC void packets_per_type_delete (
	void *packets_per_type_ptr
);

/**
 * @brief Logs the counts.
 *
 * @param[in] packets_per_type the counts, they can be NULL
 */
CERVER_PUBLIC void packets_per_type_print (
	const PacketsPerType *packets_per_type
);

/**
 * @brief Logs an array of counts by packet type.
 *
 * @param[in] packets the counts, indexed by the packet type
 */
CERVER_PUBLIC void packets_per_type_array_print (
	const u64 packets[PACKETS_MAX_TYPES]
);

/** @} */

/**
 * @defgroup packets_header Header
 * @ingroup packets
 * @brief The header of a packet and its format in the wire.
 * @{
 */

/**
 * @brief The size of the header in the wire: it travels field by field in network byte order, so it never depends
 * on the layout of PacketHeader in memory.
 */
#define PACKET_HEADER_SIZE							32

/** @brief The format of the header in the wire; a peer that sends another one is closed. */
#define PACKET_HEADER_FORMAT						1

/** @brief The offset of the protocol id in the wire (u32). */
#define PACKET_HEADER_PROTOCOL_ID_OFFSET			0
/** @brief The offset of the major version in the wire (u16). */
#define PACKET_HEADER_MAJOR_OFFSET					4
/** @brief The offset of the minor version in the wire (u16). */
#define PACKET_HEADER_MINOR_OFFSET					6
/** @brief The offset of the packet type in the wire (u8). */
#define PACKET_HEADER_PACKET_TYPE_OFFSET			8
/** @brief The offset of the handler id in the wire (u8). */
#define PACKET_HEADER_HANDLER_ID_OFFSET				9
/** @brief The offset of the format in the wire (u8). */
#define PACKET_HEADER_FORMAT_OFFSET					10
/** @brief The offset of the flags in the wire (u8). */
#define PACKET_HEADER_FLAGS_OFFSET					11
/** @brief The offset of the request type in the wire (u32). */
#define PACKET_HEADER_REQUEST_TYPE_OFFSET			12
/** @brief The offset of the route id in the wire (u32). */
#define PACKET_HEADER_ROUTE_ID_OFFSET				16
/** @brief The offset of the packet size in the wire (u64). */
#define PACKET_HEADER_PACKET_SIZE_OFFSET			20
/** @brief The offset of the reserved bytes in the wire (u32 in zero). */
#define PACKET_HEADER_RESERVED_OFFSET				28

/** @brief The data of the packet is an object of the serializer. */
#define PACKET_FLAG_SERIALIZED						0x01

/** @brief The flags that cerver knows; a header with any other flag is a bad packet. */
#define PACKET_FLAGS_KNOWN							(PACKET_FLAG_SERIALIZED)

/**
 * @brief The header of a packet.
 */
struct _PacketHeader {

	/** @brief The main packet type, a single byte in the wire. */
	PacketType packet_type;
	/** @brief The total size of the packet (header and data). */
	u64 packet_size;

	/** @brief The app handler, in cervers with multiple app handlers. */
	u8 handler_id;
	/** @brief The `PACKET_FLAG_*` of the packet. */
	u8 flags;

	/** @brief The subtype of the packet. */
	u32 request_type;

	/** @brief The route of a client of a load balancer, to send the responses of a service back to it. */
	u32 route_id;

	/** @brief The protocol of the sender, packet_check () validates it with the local one. */
	PacketVersion version;

};

/** @brief The header of a packet, see struct _PacketHeader. */
typedef struct _PacketHeader PacketHeader;

/**
 * @brief The X-macro table of the results of decoding a header: the number, the name and the string of each one.
 */
#define PACKET_HEADER_RESULT_MAP(XX)																\
	XX(0,	NONE,		"The header is valid")														\
	XX(1,	FORMAT,		"The header has a different format")										\
	XX(2,	FLAGS,		"The header has unknown flags")												\
	XX(3,	RESERVED,	"The reserved bytes of the header are not zero")

/**
 * @brief The result of packet_header_decode (), one `PACKET_HEADER_RESULT_<name>` for each row of
 * #PACKET_HEADER_RESULT_MAP.
 */
typedef enum PacketHeaderResult {

	#define XX(num, name, string) PACKET_HEADER_RESULT_##name = num,
	PACKET_HEADER_RESULT_MAP (XX)
	#undef XX

} PacketHeaderResult;

/**
 * @brief Returns the string of a result of decoding a header.
 *
 * @param[in] result the result
 * @return the string of its row in #PACKET_HEADER_RESULT_MAP, or the one of PACKET_HEADER_RESULT_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *packet_header_result_string (
	const PacketHeaderResult result
);

/**
 * @brief Writes the header in the wire format, #PACKET_HEADER_SIZE bytes in network byte order.
 *
 * It always writes #PACKET_HEADER_FORMAT and zero reserved bytes.
 *
 * @param[in] header the header, it must not be NULL
 * @param[out] buffer the #PACKET_HEADER_SIZE bytes
 */
CERVER_EXPORT void packet_header_encode (
	const PacketHeader *header, u8 buffer[PACKET_HEADER_SIZE]
);

/**
 * @brief Reads a header in the wire format; the buffer does not need to be aligned.
 *
 * @param[in] buffer the #PACKET_HEADER_SIZE bytes
 * @param[out] header the header, only written when the result is PACKET_HEADER_RESULT_NONE
 * @return PACKET_HEADER_RESULT_NONE for a valid header, or what is wrong with it
 */
CERVER_EXPORT PacketHeaderResult packet_header_decode (
	const u8 buffer[PACKET_HEADER_SIZE], PacketHeader *header
);

/**
 * @brief Returns the protocol id and version of this process, to send them in every header.
 *
 * @return the version
 */
CERVER_PUBLIC PacketVersion packet_version_local (void);

/**
 * @brief Allocates a header in zero.
 *
 * @return the new header, NULL if it could not be allocated
 */
CERVER_PUBLIC PacketHeader *packet_header_new (void);

/**
 * @brief Releases a header.
 *
 * @param[in] header the header, it can be NULL
 */
CERVER_PUBLIC void packet_header_delete (PacketHeader *header);

/**
 * @brief Allocates a header with its values and the protocol of this process.
 *
 * @param[in] packet_type the packet type
 * @param[in] packet_size the total size of the packet (header and data)
 * @param[in] req_type the request type
 * @return the new header, NULL if it could not be allocated
 */
CERVER_PUBLIC PacketHeader *packet_header_create (
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
);

/**
 * @brief Allocates a header and copies the values of another one.
 *
 * @param[in] source the header to copy; with NULL the new header is in zero
 * @return the new header, NULL if it could not be allocated
 */
CERVER_PUBLIC PacketHeader *packet_header_create_from (
	const PacketHeader *source
);

/**
 * @brief Copies a header.
 *
 * @param[out] dest the destination
 * @param[in] source the source
 * @retval 0 the header was copied
 * @retval 1 a NULL argument
 */
CERVER_PUBLIC u8 packet_header_copy (
	PacketHeader *dest, const PacketHeader *source
);

/**
 * @brief Prints a header to stdout.
 *
 * @param[in] header the header, it can be NULL
 */
CERVER_PUBLIC void packet_header_print (
	const PacketHeader *header
);

/**
 * @brief Logs a header with the log of cerver.
 *
 * @param[in] header the header, it can be NULL
 */
CERVER_PUBLIC void packet_header_log (
	const PacketHeader *header
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
