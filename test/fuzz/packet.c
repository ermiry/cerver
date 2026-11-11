#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/packets.h>

// a header from the network: its decode, that must encode back to the same bytes,
// its copy, the packet that is created from it and the check of its version

#define FUZZ_PACKET_PROTOCOL_ID			0x0F
#define FUZZ_PACKET_PROTOCOL_MAJOR		1
#define FUZZ_PACKET_PROTOCOL_MINOR		8

// a header is valid only with the local format, known flags and zeroed reserved bytes
static bool fuzz_packet_header_is_valid (const uint8_t *data) {

	return (data[PACKET_HEADER_FORMAT_OFFSET] == PACKET_HEADER_FORMAT)
		&& !(data[PACKET_HEADER_FLAGS_OFFSET] & (uint8_t) ~PACKET_FLAGS_KNOWN)
		&& !data[PACKET_HEADER_RESERVED_OFFSET] && !data[PACKET_HEADER_RESERVED_OFFSET + 1]
		&& !data[PACKET_HEADER_RESERVED_OFFSET + 2] && !data[PACKET_HEADER_RESERVED_OFFSET + 3];

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < PACKET_HEADER_SIZE) return 0;

	static bool initialized = false;
	if (!initialized) {
		packets_set_protocol_id (FUZZ_PACKET_PROTOCOL_ID);
		packets_set_protocol_version ((ProtocolVersion) { FUZZ_PACKET_PROTOCOL_MAJOR, FUZZ_PACKET_PROTOCOL_MINOR });
		initialized = true;
	}

	PacketHeader header;
	const bool decoded = (packet_header_decode (data, &header) == PACKET_HEADER_RESULT_NONE);
	if (decoded != fuzz_packet_header_is_valid (data)) abort ();
	if (!decoded) return 0;

	// the wire has a single encoding of each header
	uint8_t encoded[PACKET_HEADER_SIZE];
	packet_header_encode (&header, encoded);
	if (memcmp (encoded, data, PACKET_HEADER_SIZE)) abort ();

	PacketHeader *copy = packet_header_create_from (&header);
	if (copy) {
		if (memcmp (copy, &header, sizeof (PacketHeader))) abort ();
		packet_header_delete (copy);
	}

	Packet *packet = packet_new ();
	if (packet) {
		packet->header = header;
		packet->version = header.version;
		packet->packet_type = header.packet_type;

		// the data that comes after the header, never its declared size
		const size_t data_size = size - PACKET_HEADER_SIZE;
		if (data_size) {
			(void) packet_set_data (packet, data + PACKET_HEADER_SIZE, data_size);
		}

		const bool valid = packet_check (packet);
		const bool expected = (header.version.protocol_id == FUZZ_PACKET_PROTOCOL_ID)
			&& (header.version.protocol_version.major == FUZZ_PACKET_PROTOCOL_MAJOR)
			&& (header.version.protocol_version.minor <= FUZZ_PACKET_PROTOCOL_MINOR);

		if (valid != expected) abort ();

		packet_delete (packet);
	}

	return 0;

}
