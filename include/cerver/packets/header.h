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

#ifdef __cplusplus
}
#endif

#endif
