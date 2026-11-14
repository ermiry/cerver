#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/uio.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/game/lobby.h"

#ifdef PACKETS_DEBUG
#include "cerver/utils/log.h"
#endif
#include "cerver/packets/internal.h"

#pragma region protocol

ProtocolID protocol_id = 0;
ProtocolVersion protocol_version = { 0, 0 };

ProtocolID packets_get_protocol_id (void) {
	
	return protocol_id;
	
}

void packets_set_protocol_id (ProtocolID proto_id) {
	
	protocol_id = proto_id;
	
}

ProtocolVersion packets_get_protocol_version (void) {
	
	return protocol_version;
	
}

void packets_set_protocol_version (ProtocolVersion version) {

	protocol_version = version;

}

PacketVersion packet_version_local (void) {

	PacketVersion version = { protocol_id, protocol_version };

	return version;

}

#pragma endregion

#pragma region version

PacketVersion *packet_version_new (void) {

	PacketVersion *version = (PacketVersion *) malloc (sizeof (PacketVersion));
	if (version) {
		version->protocol_id = 0;
		version->protocol_version.minor = 0;
		version->protocol_version.major = 0;
	}

	return version;

}

void packet_version_delete (PacketVersion *version) {
	
	if (version) free (version);
	
}

PacketVersion *packet_version_create (void) {

	PacketVersion *version = (PacketVersion *) malloc (sizeof (PacketVersion));
	if (version) {
		version->protocol_id = protocol_id;
		version->protocol_version = protocol_version;
	}

	return version;

}

// copies the data from the source version to the destination
// returns 0 on success, 1 on error
u8 packet_version_copy (
	PacketVersion *dest, const PacketVersion *source
) {

	u8 retval = 1;

	if (dest && source) {
		(void) memcpy (dest, source, sizeof (PacketVersion));
		retval = 0;
	}

	return retval;

}

void packet_version_print (
	const PacketVersion *version
) {

	if (version) {
		(void) printf (
			"Protocol id: %u\n",
			version->protocol_id
		);

		(void) printf (
			"Protocol version: { %u - %u }\n",
			version->protocol_version.major,
			version->protocol_version.minor
		);
	}

}

#pragma endregion

#pragma region types

PacketsPerType *packets_per_type_new (void) {

	PacketsPerType *packets_per_type = (PacketsPerType *) malloc (sizeof (PacketsPerType));
	if (packets_per_type) {
		(void) memset (packets_per_type, 0, sizeof (PacketsPerType));
	}

	return packets_per_type;

}

void packets_per_type_delete (void *packets_per_type_ptr) {
		
	if (packets_per_type_ptr) free (packets_per_type_ptr);

}

void packets_per_type_print (
	const PacketsPerType *packets_per_type
) {

	if (packets_per_type) {
		cerver_log_msg ("Cerver:            %lu", CERVER_STATS_GET (packets_per_type->n_cerver_packets));
		cerver_log_msg ("Client:            %lu", CERVER_STATS_GET (packets_per_type->n_client_packets));
		cerver_log_msg ("Error:             %lu", CERVER_STATS_GET (packets_per_type->n_error_packets));
		cerver_log_msg ("Request:           %lu", CERVER_STATS_GET (packets_per_type->n_request_packets));
		cerver_log_msg ("Auth:              %lu", CERVER_STATS_GET (packets_per_type->n_auth_packets));
		cerver_log_msg ("Game:              %lu", CERVER_STATS_GET (packets_per_type->n_game_packets));
		cerver_log_msg ("App:               %lu", CERVER_STATS_GET (packets_per_type->n_app_packets));
		cerver_log_msg ("App Error:         %lu", CERVER_STATS_GET (packets_per_type->n_app_error_packets));
		cerver_log_msg ("Custom:            %lu", CERVER_STATS_GET (packets_per_type->n_custom_packets));
		cerver_log_msg ("Test:              %lu", CERVER_STATS_GET (packets_per_type->n_test_packets));
		cerver_log_msg ("Unknown:           %lu", CERVER_STATS_GET (packets_per_type->n_unknown_packets));
		cerver_log_msg ("Bad:               %lu", CERVER_STATS_GET (packets_per_type->n_bad_packets));
	}

}

void packets_per_type_array_print (
	const u64 packets[PACKETS_MAX_TYPES]
) {

	cerver_log_msg ("\tCerver:              %lu", packets[PACKET_TYPE_CERVER]);
	cerver_log_msg ("\tClient:              %lu", packets[PACKET_TYPE_CLIENT]);
	cerver_log_msg ("\tError:               %lu", packets[PACKET_TYPE_ERROR]);
	cerver_log_msg ("\tRequest:             %lu", packets[PACKET_TYPE_REQUEST]);
	cerver_log_msg ("\tAuth:                %lu", packets[PACKET_TYPE_AUTH]);
	cerver_log_msg ("\tGame:                %lu", packets[PACKET_TYPE_GAME]);
	cerver_log_msg ("\tApp:                 %lu", packets[PACKET_TYPE_APP]);
	cerver_log_msg ("\tApp Error:           %lu", packets[PACKET_TYPE_APP_ERROR]);
	cerver_log_msg ("\tCustom:              %lu", packets[PACKET_TYPE_CUSTOM]);
	cerver_log_msg ("\tTest:                %lu", packets[PACKET_TYPE_TEST]);
	cerver_log_msg ("\tBad:                 %lu", packets[PACKET_TYPE_BAD]);

}

#pragma endregion

#pragma region header

PacketHeader *packet_header_new (void) {

	PacketHeader *header = (PacketHeader *) malloc (sizeof (PacketHeader));
	if (header) {
		(void) memset (header, 0, sizeof (PacketHeader));
	}

	return header;

}

void packet_header_delete (PacketHeader *header) {
	
	if (header) free (header);
	
}

// the header is sent field by field, its padding never reaches the wire,
// it is zeroed so headers can be compared and copied as a whole
void packet_header_init (
	PacketHeader *header,
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
) {

	(void) memset (header, 0, sizeof (PacketHeader));

	header->packet_type = packet_type;
	header->packet_size = packet_size;

	header->request_type = req_type;

	header->version = packet_version_local ();

}

PacketHeader *packet_header_create (
	const PacketType packet_type,
	const size_t packet_size,
	const u32 req_type
) {

	PacketHeader *header = (PacketHeader *) malloc (sizeof (PacketHeader));
	if (header) {
		packet_header_init (header, packet_type, packet_size, req_type);
	}

	return header;

}

// allocates a new packet header and copies the values from source
PacketHeader *packet_header_create_from (const PacketHeader *source) {

	PacketHeader *header = packet_header_new ();
	if (header && source) {
		(void) memcpy (header, source, sizeof (PacketHeader));
	}

	return header;

}

// copies the data from the source header to the destination
// returns 0 on success, 1 on error
u8 packet_header_copy (PacketHeader *dest, const PacketHeader *source) {

	u8 retval = 1;

	if (dest && source) {
		(void) memcpy (dest, source, sizeof (PacketHeader));
		retval = 0;
	}

	return retval;

}

void packet_header_print (const PacketHeader *header) {

	if (header) {
		(void) printf ("Header size: %d\n", PACKET_HEADER_SIZE);
		(void) printf ("Packet type: %u\n", (unsigned int) header->packet_type);
		(void) printf ("Packet size: %lu\n", (unsigned long) header->packet_size);
		(void) printf ("Handler id: %u\n", header->handler_id);
		(void) printf ("Flags: %u\n", header->flags);
		(void) printf ("Request type: %u\n", header->request_type);
		(void) printf ("Route id: %u\n", header->route_id);
	}

}

void packet_header_log (const PacketHeader *header) {

	if (header) {
		cerver_log_msg ("Packet type: %u", (unsigned int) header->packet_type);
		cerver_log_msg ("Packet size: %lu", (unsigned long) header->packet_size);
		cerver_log_msg ("Handler id: %u", header->handler_id);
		cerver_log_msg ("Flags: %u", header->flags);
		cerver_log_msg ("Request type: %u", header->request_type);
		cerver_log_msg ("Route id: %u", header->route_id);
	}

}

const char *packet_header_result_string (const PacketHeaderResult result) {

	switch (result) {
		#define XX(num, name, string) case PACKET_HEADER_RESULT_##name: return string;
		PACKET_HEADER_RESULT_MAP (XX)
		#undef XX
	}

	return packet_header_result_string (PACKET_HEADER_RESULT_NONE);

}

static void packet_header_write_u16 (u8 *buffer, const u16 value) {

	buffer[0] = (u8) (value >> 8);
	buffer[1] = (u8) value;

}

static void packet_header_write_u32 (u8 *buffer, const u32 value) {

	packet_header_write_u16 (buffer, (u16) (value >> 16));
	packet_header_write_u16 (buffer + sizeof (u16), (u16) value);

}

static void packet_header_write_u64 (u8 *buffer, const u64 value) {

	packet_header_write_u32 (buffer, (u32) (value >> 32));
	packet_header_write_u32 (buffer + sizeof (u32), (u32) value);

}

static u16 packet_header_read_u16 (const u8 *buffer) {

	return (u16) (((u16) buffer[0] << 8) | (u16) buffer[1]);

}

static u32 packet_header_read_u32 (const u8 *buffer) {

	return ((u32) packet_header_read_u16 (buffer) << 16)
		| (u32) packet_header_read_u16 (buffer + sizeof (u16));

}

static u64 packet_header_read_u64 (const u8 *buffer) {

	return ((u64) packet_header_read_u32 (buffer) << 32)
		| (u64) packet_header_read_u32 (buffer + sizeof (u32));

}

// writes the header in the wire format, PACKET_HEADER_SIZE bytes in network byte order
void packet_header_encode (
	const PacketHeader *header, u8 buffer[PACKET_HEADER_SIZE]
) {

	packet_header_write_u32 (buffer + PACKET_HEADER_PROTOCOL_ID_OFFSET, header->version.protocol_id);
	packet_header_write_u16 (buffer + PACKET_HEADER_MAJOR_OFFSET, header->version.protocol_version.major);
	packet_header_write_u16 (buffer + PACKET_HEADER_MINOR_OFFSET, header->version.protocol_version.minor);

	buffer[PACKET_HEADER_PACKET_TYPE_OFFSET] = (u8) header->packet_type;
	buffer[PACKET_HEADER_HANDLER_ID_OFFSET] = header->handler_id;
	buffer[PACKET_HEADER_FORMAT_OFFSET] = PACKET_HEADER_FORMAT;
	buffer[PACKET_HEADER_FLAGS_OFFSET] = header->flags;

	packet_header_write_u32 (buffer + PACKET_HEADER_REQUEST_TYPE_OFFSET, header->request_type);
	packet_header_write_u32 (buffer + PACKET_HEADER_ROUTE_ID_OFFSET, header->route_id);
	packet_header_write_u64 (buffer + PACKET_HEADER_PACKET_SIZE_OFFSET, header->packet_size);
	packet_header_write_u32 (buffer + PACKET_HEADER_RESERVED_OFFSET, 0);

}

static PacketHeaderResult packet_header_decode_check (const u8 *buffer) {

	PacketHeaderResult result = PACKET_HEADER_RESULT_NONE;

	if (buffer[PACKET_HEADER_FORMAT_OFFSET] != PACKET_HEADER_FORMAT) {
		result = PACKET_HEADER_RESULT_FORMAT;
	}

	else if (buffer[PACKET_HEADER_FLAGS_OFFSET] & (u8) ~PACKET_FLAGS_KNOWN) {
		result = PACKET_HEADER_RESULT_FLAGS;
	}

	else if (packet_header_read_u32 (buffer + PACKET_HEADER_RESERVED_OFFSET)) {
		result = PACKET_HEADER_RESULT_RESERVED;
	}

	return result;

}

// reads a header in the wire format, the buffer does not need to be aligned
// the header is only written when the result is PACKET_HEADER_RESULT_NONE
PacketHeaderResult packet_header_decode (
	const u8 buffer[PACKET_HEADER_SIZE], PacketHeader *header
) {

	const PacketHeaderResult result = packet_header_decode_check (buffer);

	if (result == PACKET_HEADER_RESULT_NONE) {
		(void) memset (header, 0, sizeof (PacketHeader));

		header->version.protocol_id = packet_header_read_u32 (buffer + PACKET_HEADER_PROTOCOL_ID_OFFSET);
		header->version.protocol_version.major = packet_header_read_u16 (buffer + PACKET_HEADER_MAJOR_OFFSET);
		header->version.protocol_version.minor = packet_header_read_u16 (buffer + PACKET_HEADER_MINOR_OFFSET);

		header->packet_type = (PacketType) buffer[PACKET_HEADER_PACKET_TYPE_OFFSET];
		header->handler_id = buffer[PACKET_HEADER_HANDLER_ID_OFFSET];
		header->flags = buffer[PACKET_HEADER_FLAGS_OFFSET];

		header->request_type = packet_header_read_u32 (buffer + PACKET_HEADER_REQUEST_TYPE_OFFSET);
		header->route_id = packet_header_read_u32 (buffer + PACKET_HEADER_ROUTE_ID_OFFSET);
		header->packet_size = packet_header_read_u64 (buffer + PACKET_HEADER_PACKET_SIZE_OFFSET);
	}

	return result;

}

#pragma endregion
