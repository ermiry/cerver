#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/packets.h>

// a header from the network: its copy, the packet that is created from it,
// the check of its version and the print of its values

#define FUZZ_PACKET_PROTOCOL_ID			0x0F
#define FUZZ_PACKET_PROTOCOL_MAJOR		1
#define FUZZ_PACKET_PROTOCOL_MINOR		8

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < sizeof (PacketHeader)) return 0;

	static bool initialized = false;
	if (!initialized) {
		packets_set_protocol_id (FUZZ_PACKET_PROTOCOL_ID);
		packets_set_protocol_version ((ProtocolVersion) { FUZZ_PACKET_PROTOCOL_MAJOR, FUZZ_PACKET_PROTOCOL_MINOR });
		initialized = true;
	}

	PacketHeader header;
	(void) memcpy (&header, data, sizeof (PacketHeader));

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
		const size_t data_size = size - sizeof (PacketHeader);
		if (data_size) {
			(void) packet_set_data (packet, data + sizeof (PacketHeader), data_size);
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
