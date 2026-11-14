#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/connection/connection.h>
#include <cerver/network/socket.h>
#include <cerver/packets/packets.h>

#include "../test.h"

#define BUFFER_SIZE			128

#define SEND_REQUEST_TYPE		7
#define SEND_DATA_SIZE			3000
#define SEND_PIECES				3
#define SEND_PIECE_SIZE			1000
#define ROUTE_BODY_SIZE			((4096 * 3) + 123)

#define CHECK_MAJOR				2
#define CHECK_MINOR				4

// the values of the sample header, each byte of the wire is different
#define WIRE_PROTOCOL_ID		0x01020304u
#define WIRE_MAJOR				0x0005
#define WIRE_MINOR				0x0006
#define WIRE_HANDLER_ID			0x07
#define WIRE_REQUEST_TYPE		0x08090a0bu
#define WIRE_ROUTE_ID			0x0c0d0e0fu
#define WIRE_PACKET_SIZE		0x1011121314151617ull
#define WIRE_GARBAGE			0xa5
#define WIRE_UNKNOWN_FLAG		0x80

#pragma region header

static void test_packet_header_create (void) {

	u32 request_type = 10;
	size_t packet_size = sizeof (PacketHeader);

	PacketHeader *header = packet_header_create (
		PACKET_TYPE_APP,
		packet_size,
		request_type
	);

	test_check_ptr (header);
	test_check_unsigned_eq (header->packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (header->packet_size, packet_size, NULL);
	test_check_unsigned_eq (header->handler_id, 0, NULL);
	test_check_unsigned_eq (header->request_type, request_type, NULL);
	test_check_unsigned_eq (header->route_id, 0, NULL);

	packet_header_delete (header);

}

// the header with a value in every field, and its bytes in the wire
static PacketHeader packets_wire_sample (void) {

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));

	header.version.protocol_id = WIRE_PROTOCOL_ID;
	header.version.protocol_version.major = WIRE_MAJOR;
	header.version.protocol_version.minor = WIRE_MINOR;
	header.packet_type = PACKET_TYPE_APP;
	header.handler_id = WIRE_HANDLER_ID;
	header.flags = PACKET_FLAG_SERIALIZED;
	header.request_type = WIRE_REQUEST_TYPE;
	header.route_id = WIRE_ROUTE_ID;
	header.packet_size = WIRE_PACKET_SIZE;

	return header;

}

static const u8 packets_wire_sample_bytes[PACKET_HEADER_SIZE] = {
	0x01, 0x02, 0x03, 0x04,							// protocol id
	0x00, 0x05,										// major
	0x00, 0x06,										// minor
	PACKET_TYPE_APP,								// packet type
	0x07,											// handler id
	PACKET_HEADER_FORMAT,							// format
	PACKET_FLAG_SERIALIZED,							// flags
	0x08, 0x09, 0x0a, 0x0b,							// request type
	0x0c, 0x0d, 0x0e, 0x0f,							// route id
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,	// packet size
	0x00, 0x00, 0x00, 0x00							// reserved
};

// the header that a generated packet sends, decoded from the wire
static PacketHeader packets_wire_header (const void *packet) {

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));

	test_check_unsigned_eq (
		packet_header_decode ((const u8 *) packet, &header), PACKET_HEADER_RESULT_NONE, NULL
	);

	return header;

}

// the header is sent field by field in network byte order, in the same bytes in every architecture
static void test_packet_header_encode (void) {

	const PacketHeader header = packets_wire_sample ();

	u8 encoded[PACKET_HEADER_SIZE];
	(void) memset (encoded, WIRE_GARBAGE, PACKET_HEADER_SIZE);
	packet_header_encode (&header, encoded);

	test_check_int_eq (memcmp (encoded, packets_wire_sample_bytes, PACKET_HEADER_SIZE), 0, NULL);

}

static void test_packet_header_decode (void) {

	PacketHeader header;
	test_check_unsigned_eq (
		packet_header_decode (packets_wire_sample_bytes, &header), PACKET_HEADER_RESULT_NONE, NULL
	);

	const PacketHeader expected = packets_wire_sample ();
	test_check_unsigned_eq (header.version.protocol_id, expected.version.protocol_id, NULL);
	test_check_unsigned_eq (header.version.protocol_version.major, expected.version.protocol_version.major, NULL);
	test_check_unsigned_eq (header.version.protocol_version.minor, expected.version.protocol_version.minor, NULL);
	test_check_unsigned_eq (header.packet_type, expected.packet_type, NULL);
	test_check_unsigned_eq (header.handler_id, expected.handler_id, NULL);
	test_check_unsigned_eq (header.flags, expected.flags, NULL);
	test_check_unsigned_eq (header.request_type, expected.request_type, NULL);
	test_check_unsigned_eq (header.route_id, expected.route_id, NULL);
	test_check (header.packet_size == expected.packet_size, "the packet size is different");

}

// a header with another format, unknown flags or reserved bytes is rejected without being written
static void test_packet_header_decode_rejects (void) {

	u8 encoded[PACKET_HEADER_SIZE];

	PacketHeader header;
	(void) memset (&header, WIRE_GARBAGE, sizeof (PacketHeader));
	const PacketHeader untouched = header;

	(void) memcpy (encoded, packets_wire_sample_bytes, PACKET_HEADER_SIZE);
	encoded[PACKET_HEADER_FORMAT_OFFSET] = PACKET_HEADER_FORMAT + 1;
	test_check_unsigned_eq (packet_header_decode (encoded, &header), PACKET_HEADER_RESULT_FORMAT, NULL);

	(void) memcpy (encoded, packets_wire_sample_bytes, PACKET_HEADER_SIZE);
	encoded[PACKET_HEADER_FLAGS_OFFSET] = WIRE_UNKNOWN_FLAG;
	test_check_unsigned_eq (packet_header_decode (encoded, &header), PACKET_HEADER_RESULT_FLAGS, NULL);

	(void) memcpy (encoded, packets_wire_sample_bytes, PACKET_HEADER_SIZE);
	encoded[PACKET_HEADER_RESERVED_OFFSET + 3] = 1;
	test_check_unsigned_eq (packet_header_decode (encoded, &header), PACKET_HEADER_RESULT_RESERVED, NULL);

	test_check_int_eq (memcmp (&header, &untouched, sizeof (PacketHeader)), 0, NULL);

}

// a request sends its own header encoded again, with the changes made after it was created
static void test_packet_header_request_changes (void) {

	Packet *request = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (request);

	request->header.handler_id = WIRE_HANDLER_ID;

	u8 sent[PACKET_HEADER_SIZE] = { 0 };
	int fds[2] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);

	Socket *socket = socket_create (fds[0]);
	test_check_ptr (socket);

	test_check_unsigned_eq (packet_send_to_socket (request, socket, 0, NULL, false), 0, NULL);
	test_check_int_eq ((int) recv (fds[1], sent, PACKET_HEADER_SIZE, MSG_WAITALL), PACKET_HEADER_SIZE, NULL);

	test_check_unsigned_eq (packets_wire_header (sent).handler_id, WIRE_HANDLER_ID, NULL);

	socket_delete (socket);
	(void) close (fds[0]);
	(void) close (fds[1]);
	packet_delete (request);

}

static void test_packet_header_create_from (void) {

	u32 request_type = 10;
	u8 handler_id = 2;
	size_t packet_size = sizeof (PacketHeader);
	u32 route_id = 16;

	PacketHeader source = {
		.packet_type = PACKET_TYPE_APP,
		.packet_size = packet_size,

		.handler_id = handler_id,
		.flags = 0,

		.request_type = request_type,

		.route_id = route_id,

		.version = CERVER_ZERO_INITIALIZER
	};

	PacketHeader *real_header = packet_header_create_from (&source);

	test_check_ptr (real_header);
	test_check_unsigned_eq (real_header->packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (real_header->packet_size, packet_size, NULL);
	test_check_unsigned_eq (real_header->handler_id, handler_id, NULL);
	test_check_unsigned_eq (real_header->request_type, request_type, NULL);
	test_check_unsigned_eq (real_header->route_id, route_id, NULL);

	packet_header_delete (real_header);

}

static void test_packet_header_copy (void) {

	u32 request_type = 10;
	u8 handler_id = 2;
	size_t packet_size = sizeof (PacketHeader);
	u32 route_id = 16;

	PacketHeader source = {
		.packet_type = PACKET_TYPE_APP,
		.packet_size = packet_size,

		.handler_id = handler_id,
		.flags = 0,

		.request_type = request_type,

		.route_id = route_id,

		.version = CERVER_ZERO_INITIALIZER
	};

	PacketHeader destination = {
		.packet_type = PACKET_TYPE_NONE,
		.packet_size = 0,

		.handler_id = 0,
		.flags = 0,

		.request_type = 0,

		.route_id = 0,

		.version = CERVER_ZERO_INITIALIZER
	};

	test_check_unsigned_eq (packet_header_copy (&destination, &source), 0, NULL);

	test_check_unsigned_eq (destination.packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (destination.packet_size, packet_size, NULL);
	test_check_unsigned_eq (destination.handler_id, handler_id, NULL);
	test_check_unsigned_eq (destination.request_type, request_type, NULL);
	test_check_unsigned_eq (destination.route_id, route_id, NULL);

}

#pragma endregion

#pragma region handler

static void test_packets_create_with_data (void) {

	size_t data_size = BUFFER_SIZE;

	Packet *packet = packet_create_with_data (data_size);

	test_check_null_ptr (packet->cerver);
	test_check_null_ptr (packet->client);
	test_check_null_ptr (packet->connection);
	test_check_null_ptr (packet->lobby);

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_ptr (packet->data);
	test_check_null_ptr (packet->data_ptr);
	test_check_ptr (packet->data_end);

	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_NONE, NULL);
	test_check_unsigned_eq (packet->header.packet_size, 0, NULL);
	test_check_unsigned_eq (packet->header.handler_id, 0, NULL);
	test_check_unsigned_eq (packet->header.request_type, 0, NULL);
	test_check_unsigned_eq (packet->header.route_id, 0, NULL);

	test_check_unsigned_eq (packet->packet_size, 0, NULL);
	test_check_null_ptr (packet->packet);
	test_check_false (packet->packet_ref);

	packet_delete (packet);

}

static void test_packets_add_data_good (void) {
	
	size_t data_size = BUFFER_SIZE;

	Packet *packet = packet_create_with_data (data_size);

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_ptr (packet->data);
	test_check_null_ptr (packet->data_ptr);
	test_check_ptr (packet->data_end);

	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "1234567890", BUFFER_SIZE - 1);
	// size_t buffer_len = strlen (buffer);

	test_check_unsigned_eq (
		packet_add_data (packet, buffer, BUFFER_SIZE), 0, NULL
	);

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_unsigned_eq (packet->remaining_data, 0, NULL);

	packet_delete (packet);

}

static void test_packets_add_data_bad (void) {
	
	size_t data_size = BUFFER_SIZE;

	Packet *packet = packet_create_with_data (data_size);

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_ptr (packet->data);
	test_check_null_ptr (packet->data_ptr);
	test_check_ptr (packet->data_end);

	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "1234567890", BUFFER_SIZE - 1);
	// size_t buffer_len = strlen (buffer);

	test_check_unsigned_eq (
		packet_add_data (packet, buffer, data_size * 2), 1, NULL
	);

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_unsigned_eq (packet->remaining_data, data_size, NULL);

	packet_delete (packet);

}

static void test_packets_add_data_multiple (void) {
	
	size_t data_size = BUFFER_SIZE;

	Packet *packet = packet_create_with_data (data_size);

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_ptr (packet->data);
	test_check_null_ptr (packet->data_ptr);
	test_check_ptr (packet->data_end);

	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "1234567890", BUFFER_SIZE - 1);
	size_t buffer_len = strlen (buffer);

	size_t n_copies = BUFFER_SIZE / buffer_len;
	for (size_t i = 0; i < 20; i++) {
		if (i < n_copies) {
			test_check_unsigned_eq (
				packet_add_data (packet, buffer, buffer_len), 0, NULL
			);
		}

		else {
			test_check_unsigned_eq (
				packet_add_data (packet, buffer, buffer_len), 1, NULL
			);
		}
	}

	// the packet data is binary and is not terminated with '\0',
	// so check the copies byte by byte instead of using strlen ()
	test_check_unsigned_eq (
		(size_t) ((char *) packet->data_end - (char *) packet->data),
		n_copies * buffer_len, NULL
	);

	for (size_t i = 0; i < n_copies; i++) {
		test_check_int_eq (
			memcmp ((char *) packet->data + (i * buffer_len), buffer, buffer_len), 0, NULL
		);
	}

	test_check_unsigned_eq (packet->data_size, data_size, NULL);
	test_check_unsigned_eq (packet->remaining_data, BUFFER_SIZE % buffer_len, NULL);

	packet_delete (packet);

}

#pragma endregion

#pragma region public

static void test_packets_create_ping (void) {

	Packet *ping = packet_create_ping ();

	test_check_ptr (ping);

	test_check_null_ptr (ping->cerver);
	test_check_null_ptr (ping->client);
	test_check_null_ptr (ping->connection);
	test_check_null_ptr (ping->lobby);

	test_check_unsigned_eq (ping->data_size, 0, NULL);
	test_check_null_ptr (ping->data);
	test_check_null_ptr (ping->data_ptr);
	test_check_null_ptr (ping->data_end);

	test_check_unsigned_eq (ping->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (ping->header.packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_unsigned_eq (ping->header.handler_id, 0, NULL);
	test_check_unsigned_eq (ping->header.request_type, 0, NULL);
	test_check_unsigned_eq (ping->header.route_id, 0, NULL);

	// check real packet
	test_check_unsigned_eq (ping->packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_ptr (ping->packet);
	test_check_unsigned_eq (packets_wire_header (ping->packet).packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packets_wire_header (ping->packet).packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_unsigned_eq (packets_wire_header (ping->packet).handler_id, 0, NULL);
	test_check_unsigned_eq (packets_wire_header (ping->packet).request_type, 0, NULL);
	test_check_unsigned_eq (packets_wire_header (ping->packet).route_id, 0, NULL);

	test_check_bool_eq (ping->packet_ref, true, NULL);

	packet_delete (ping);

}

static void test_packets_append_data (void) {

	Packet *request = packet_new ();

	test_check_ptr (request);
	test_check_unsigned_eq (request->packet_type, PACKET_TYPE_NONE, NULL);
	test_check_unsigned_eq (request->req_type, 0, NULL);
	test_check_null_ptr (request->data);
	test_check_unsigned_eq (request->data_size, 0, NULL);
	test_check_null_ptr (request->packet);

	// add data to the packet
	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "This is test with sample text", BUFFER_SIZE - 1);

	packet_append_data (request, buffer, BUFFER_SIZE);
	test_check_ptr (request->data);
	test_check_unsigned_eq (request->data_size, BUFFER_SIZE, NULL);

	// check packet's data content
	char *data = (char *) request->data;
	test_check_str_eq (data, buffer, NULL);
	test_check_str_len (data, strlen (buffer), NULL);

	packet_delete (request);

}

static void test_packets_create_no_data (void) {

	u32 request_type = 10;

	Packet *packet = packet_create (
		PACKET_TYPE_TEST, request_type,
		NULL, 0
	);

	test_check_ptr (packet);
	test_check_unsigned_eq (packet->packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->req_type, request_type, NULL);
	test_check_null_ptr (packet->data);
	test_check_unsigned_eq (packet->data_size, 0, NULL);
	test_check_null_ptr (packet->packet);

	packet_delete (packet);

}

static void test_packets_create_full (void) {

	u32 request_type = 10;
	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "This is test with sample text", BUFFER_SIZE - 1);

	Packet *packet = packet_create (
		PACKET_TYPE_TEST, request_type,
		buffer, BUFFER_SIZE
	);

	test_check_ptr (packet);
	test_check_unsigned_eq (packet->packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->req_type, request_type, NULL);
	test_check_ptr (packet->data);
	test_check_unsigned_eq (packet->data_size, BUFFER_SIZE, NULL);
	test_check_null_ptr (packet->packet);

	packet_delete (packet);

}

static void test_packets_create_and_generate_no_data (void) {

	u32 request_type = 10;

	Packet *packet = packet_create (
		PACKET_TYPE_TEST, request_type,
		NULL, 0
	);

	test_check_ptr (packet);
	test_check_unsigned_eq (packet->packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->req_type, request_type, NULL);
	test_check_null_ptr (packet->data);
	test_check_unsigned_eq (packet->data_size, 0, NULL);
	test_check_null_ptr (packet->packet);

	// attempt to generate packet
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);

	test_check_unsigned_eq (packet->packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, request_type, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).route_id, packet->header.route_id, NULL);

	packet_delete (packet);

}

static void test_packets_create_and_generate_complete (void) {

	u32 request_type = 10;
	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "This is test with sample text", BUFFER_SIZE - 1);

	Packet *packet = packet_create (
		PACKET_TYPE_TEST, request_type,
		buffer, BUFFER_SIZE
	);

	test_check_ptr (packet);
	test_check_unsigned_eq (packet->packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->req_type, request_type, NULL);
	test_check_ptr (packet->data);
	test_check_unsigned_eq (packet->data_size, BUFFER_SIZE, NULL);
	test_check_null_ptr (packet->packet);

	// attempt to generate packet
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);

	test_check_unsigned_eq (packet->packet_size, PACKET_HEADER_SIZE +BUFFER_SIZE, NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, request_type, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).route_id, packet->header.route_id, NULL);

	packet_delete (packet);

}

// generate packet with no data
static void test_packets_generate_empty (void) {

	Packet *packet = packet_new ();

	test_check_ptr (packet);
	test_check_unsigned_eq (packet->packet_type, PACKET_TYPE_NONE, NULL);
	test_check_unsigned_eq (packet->req_type, 0, NULL);
	test_check_null_ptr (packet->data);
	test_check_unsigned_eq (packet->data_size, 0, NULL);
	test_check_null_ptr (packet->packet);

	// attempt to generate packet
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);

	test_check_unsigned_eq (packet->packet_size, PACKET_HEADER_SIZE, NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_NONE, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, 0, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).route_id, packet->header.route_id, NULL);

	packet_delete (packet);

}

// generate packet with header values & data
static void test_packets_generate_full (void) {

	u32 request_type = 10;
	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "This is test with sample text", BUFFER_SIZE - 1);

	Packet *packet = packet_new ();

	test_check_ptr (packet);

	packet_append_data (packet, buffer, BUFFER_SIZE);
	test_check_ptr (packet->data);
	test_check_unsigned_eq (packet->data_size, BUFFER_SIZE, NULL);

	packet->packet_type = PACKET_TYPE_TEST;
	packet->req_type = request_type;

	// attempt to generate packet
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);

	test_check_unsigned_eq (packet->packet_size, PACKET_HEADER_SIZE +BUFFER_SIZE, NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, request_type, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (packets_wire_header (packet->packet).route_id, packet->header.route_id, NULL);

	packet_delete (packet);

}

static void test_packets_generate_request (void) {

	char buffer[BUFFER_SIZE] = { 0 };
	(void) strncpy (buffer, "This is test with sample text", BUFFER_SIZE - 1);

	u32 request_type = 10;

	Packet *request = packet_generate_request (
		PACKET_TYPE_APP, request_type,
		buffer, BUFFER_SIZE
	);

	test_check_ptr (request);
	test_check_unsigned_eq (request->packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (request->req_type, request_type, NULL);
	test_check_ptr (request->data);
	test_check_unsigned_eq (request->data_size, BUFFER_SIZE, NULL);
	test_check_ptr (request->packet);

	// check packet's contents
	size_t real_packet_size = PACKET_HEADER_SIZE +BUFFER_SIZE;
	test_check_unsigned_eq (request->packet_size, real_packet_size, NULL);

	test_check_unsigned_eq (packets_wire_header (request->packet).packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (packets_wire_header (request->packet).packet_size, real_packet_size, NULL);
	test_check_unsigned_eq (packets_wire_header (request->packet).handler_id, 0, NULL);
	test_check_unsigned_eq (packets_wire_header (request->packet).request_type, request_type, NULL);
	test_check_unsigned_eq (packets_wire_header (request->packet).route_id, 0, NULL);

	char *end = (char *) request->packet;
	end += PACKET_HEADER_SIZE;
	test_check_str_eq (end, buffer, NULL);
	test_check_str_len (end, strlen (buffer), NULL);

	packet_delete (request);

}

#pragma endregion

#pragma region send

typedef struct SendPair {

	int fds[2];					// [0] is the connection's socket, [1] is the reader
	Connection *connection;

} SendPair;

static SendPair send_pair_create (void) {

	SendPair pair = { { -1, -1 }, NULL };

	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, pair.fds), 0, NULL);

	pair.connection = connection_create_empty ();
	test_check_ptr (pair.connection);

	pair.connection->socket->sock_fd = pair.fds[0];
	pair.connection->active = true;

	return pair;

}

static void send_pair_delete (SendPair *pair) {

	pair->connection->active = false;
	connection_delete (pair->connection);

	(void) close (pair->fds[0]);
	(void) close (pair->fds[1]);

}

// reads exactly size bytes
static void send_read (const int fd, char *buffer, const size_t size) {

	size_t total = 0;
	while (total < size) {
		const ssize_t received = recv (fd, buffer + total, size - total, 0);
		test_check (received > 0, "the receiver did not get every byte");
		total += (size_t) received;
	}

}

// reads a header from the wire and decodes it
static PacketHeader send_read_header (const int fd) {

	u8 encoded[PACKET_HEADER_SIZE];
	send_read (fd, (char *) encoded, PACKET_HEADER_SIZE);

	return packets_wire_header (encoded);

}

static void send_check_header (
	const PacketHeader *header,
	const PacketType packet_type, const u32 request_type, const size_t packet_size
) {

	test_check_int_eq (header->packet_type, packet_type, NULL);
	test_check_unsigned_eq (header->request_type, request_type, NULL);
	test_check_unsigned_eq (header->packet_size, packet_size, NULL);

	// the version of the sender travels in every header
	const PacketVersion local = packet_version_local ();
	test_check_unsigned_eq (header->version.protocol_id, local.protocol_id, NULL);
	test_check_unsigned_eq (header->version.protocol_version.major, local.protocol_version.major, NULL);
	test_check_unsigned_eq (header->version.protocol_version.minor, local.protocol_version.minor, NULL);

}

static void send_fill (char *data, const size_t size, const char seed) {

	for (size_t i = 0; i < size; i++) data[i] = (char) (seed + (char) (i % 64));

}

// the header and the data are sent apart, and arrive complete and in order
static void test_packets_send_split (void) {

	SendPair pair = send_pair_create ();

	char data[SEND_DATA_SIZE];
	send_fill (data, SEND_DATA_SIZE, 'a');

	Packet *packet = packet_create (PACKET_TYPE_APP, SEND_REQUEST_TYPE, data, SEND_DATA_SIZE);
	test_check_ptr (packet);
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);
	packet_set_network_values (packet, NULL, NULL, pair.connection, NULL);

	size_t sent = 0;
	test_check_unsigned_eq (packet_send_split (packet, 0, &sent), 0, NULL);
	test_check_unsigned_eq (sent, PACKET_HEADER_SIZE +SEND_DATA_SIZE, NULL);

	const PacketHeader header = send_read_header (pair.fds[1]);
	send_check_header (&header, PACKET_TYPE_APP, SEND_REQUEST_TYPE, PACKET_HEADER_SIZE +SEND_DATA_SIZE);

	char received[SEND_DATA_SIZE];
	send_read (pair.fds[1], received, SEND_DATA_SIZE);
	test_check_int_eq (memcmp (received, data, SEND_DATA_SIZE), 0, NULL);

	packet_delete (packet);
	send_pair_delete (&pair);

}

// the pieces follow the header in order
static void test_packets_send_pieces (void) {

	SendPair pair = send_pair_create ();

	char pieces_data[SEND_PIECES][SEND_PIECE_SIZE];
	void *pieces[SEND_PIECES];
	size_t sizes[SEND_PIECES];
	for (unsigned int i = 0; i < SEND_PIECES; i++) {
		send_fill (pieces_data[i], SEND_PIECE_SIZE, (char) ('A' + i));
		pieces[i] = pieces_data[i];
		sizes[i] = SEND_PIECE_SIZE;
	}

	const size_t packet_size = PACKET_HEADER_SIZE +(SEND_PIECES * SEND_PIECE_SIZE);

	Packet *packet = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (packet);
	packet->header.packet_size = packet_size;
	packet_set_network_values (packet, NULL, NULL, pair.connection, NULL);

	size_t sent = 0;
	test_check_unsigned_eq (packet_send_pieces (packet, pieces, sizes, SEND_PIECES, 0, &sent), 0, NULL);
	test_check_unsigned_eq (sent, packet_size, NULL);

	const PacketHeader header = send_read_header (pair.fds[1]);
	send_check_header (&header, PACKET_TYPE_APP, SEND_REQUEST_TYPE, packet_size);

	char received[SEND_PIECE_SIZE];
	for (unsigned int i = 0; i < SEND_PIECES; i++) {
		send_read (pair.fds[1], received, SEND_PIECE_SIZE);
		test_check_int_eq (memcmp (received, pieces_data[i], SEND_PIECE_SIZE), 0, NULL);
	}

	packet_delete (packet);
	send_pair_delete (&pair);

}

// a piece that can not be sent fails the packet
static void test_packets_send_pieces_failed (void) {

	SendPair pair = send_pair_create ();
	(void) close (pair.fds[1]);
	pair.fds[1] = -1;

	char piece[SEND_PIECE_SIZE];
	send_fill (piece, SEND_PIECE_SIZE, 'z');
	void *pieces[1] = { piece };
	size_t sizes[1] = { SEND_PIECE_SIZE };

	Packet *packet = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (packet);
	packet_set_network_values (packet, NULL, NULL, pair.connection, NULL);

	test_check_unsigned_eq (packet_send_pieces (packet, pieces, sizes, 1, MSG_NOSIGNAL, NULL), 1, NULL);

	packet_delete (packet);
	send_pair_delete (&pair);

}

// the whole generated packet is written to the socket
static void test_packets_send_to_socket (void) {

	SendPair pair = send_pair_create ();

	char data[SEND_DATA_SIZE];
	send_fill (data, SEND_DATA_SIZE, '0');

	Packet *packet = packet_create (PACKET_TYPE_CUSTOM, SEND_REQUEST_TYPE, data, SEND_DATA_SIZE);
	test_check_ptr (packet);
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);

	size_t sent = 0;
	test_check_unsigned_eq (packet_send_to_socket (packet, pair.connection->socket, 0, &sent, false), 0, NULL);
	test_check_unsigned_eq (sent, packet->packet_size, NULL);

	char *received = (char *) calloc (packet->packet_size, sizeof (char));
	test_check_ptr (received);
	send_read (pair.fds[1], received, packet->packet_size);

	const PacketHeader decoded = packets_wire_header (received);
	send_check_header (&decoded, PACKET_TYPE_CUSTOM, SEND_REQUEST_TYPE, packet->packet_size);
	test_check_int_eq (memcmp (received + PACKET_HEADER_SIZE, data, SEND_DATA_SIZE), 0, NULL);

	free (received);
	packet_delete (packet);
	send_pair_delete (&pair);

}

// a request is only a header
static void test_packets_send_request (void) {

	SendPair pair = send_pair_create ();

	test_check_unsigned_eq (
		packet_send_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE, NULL, NULL, pair.connection, NULL),
		0, NULL
	);

	const PacketHeader header = send_read_header (pair.fds[1]);
	send_check_header (&header, PACKET_TYPE_APP, SEND_REQUEST_TYPE, PACKET_HEADER_SIZE);

	send_pair_delete (&pair);

}

// a request packet references its own header, generating it must not release it
static void test_packets_generate_created_request (void) {

	Packet *packet = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (packet);

	test_check_unsigned_eq (packet_generate (packet), 0, NULL);
	test_check_ptr (packet->packet);
	test_check_bool_eq (packet->packet_ref, false, NULL);
	test_check_unsigned_eq (packet->packet_size, PACKET_HEADER_SIZE, NULL);

	packet_delete (packet);

}

// the body moves between the sockets through a pipe, in more than one round
static void test_packets_route_between_connections (void) {

	SendPair from = send_pair_create ();
	SendPair to = send_pair_create ();

	char *body = (char *) malloc (ROUTE_BODY_SIZE);
	test_check_ptr (body);
	send_fill (body, ROUTE_BODY_SIZE, 'r');

	// the body waits in the from connection, like a packet that was not read yet
	test_check (send (from.fds[1], body, ROUTE_BODY_SIZE, 0) == (ssize_t) ROUTE_BODY_SIZE, NULL);

	PacketHeader header;
	(void) memset (&header, 0, sizeof (PacketHeader));
	header.packet_type = PACKET_TYPE_APP;
	header.request_type = SEND_REQUEST_TYPE;
	header.packet_size = PACKET_HEADER_SIZE +ROUTE_BODY_SIZE;
	header.version = packet_version_local ();

	size_t sent = 0;
	test_check_unsigned_eq (packet_route_between_connections (from.connection, to.connection, &header, &sent), 0, NULL);
	test_check_unsigned_eq (sent, header.packet_size, NULL);

	const PacketHeader received_header = send_read_header (to.fds[1]);
	send_check_header (&received_header, PACKET_TYPE_APP, SEND_REQUEST_TYPE, header.packet_size);

	char *received = (char *) malloc (ROUTE_BODY_SIZE);
	test_check_ptr (received);
	send_read (to.fds[1], received, ROUTE_BODY_SIZE);
	test_check_int_eq (memcmp (received, body, ROUTE_BODY_SIZE), 0, NULL);

	free (received);
	free (body);
	send_pair_delete (&to);
	send_pair_delete (&from);

}

// the same protocol id and major with a minor that is not newer passes,
// it used to accept older majors and reject older minors
static void test_packets_check (void) {

	Packet *packet = packet_new ();
	test_check_ptr (packet);

	const ProtocolVersion previous = packets_get_protocol_version ();
	const ProtocolVersion local = { CHECK_MAJOR, CHECK_MINOR };
	packets_set_protocol_version (local);

	packet->version.protocol_id = packets_get_protocol_id ();
	packet->version.protocol_version = local;
	test_check_bool_eq (packet_check (packet), true, NULL);

	// an older minor of the same major
	packet->version.protocol_version.minor = CHECK_MINOR - 1;
	test_check_bool_eq (packet_check (packet), true, NULL);

	// a newer minor
	packet->version.protocol_version.minor = CHECK_MINOR + 1;
	test_check_bool_eq (packet_check (packet), false, NULL);

	// another major, older or newer
	packet->version.protocol_version = local;
	packet->version.protocol_version.major = CHECK_MAJOR - 1;
	test_check_bool_eq (packet_check (packet), false, NULL);
	packet->version.protocol_version.major = CHECK_MAJOR + 1;
	test_check_bool_eq (packet_check (packet), false, NULL);

	packet->version.protocol_version = local;
	packet->version.protocol_id = packets_get_protocol_id () + 1;
	test_check_bool_eq (packet_check (packet), false, NULL);

	packets_set_protocol_version (previous);

	test_check_bool_eq (packet_check (NULL), false, NULL);

	packet_delete (packet);

}

#pragma endregion

#pragma region recycle

#define RECYCLE_SMALL_SIZE			100
#define RECYCLE_CLASS_SIZE			128
#define RECYCLE_SMALL_CLASS			2
#define RECYCLE_BIG_SIZE			8192
#define RECYCLE_APPENDS				64
#define RECYCLE_BYTE				0x5A

// #172 / #175: the packets and the buffers of data that fit a class are reused,
// except in the builds with sanitizers, that do not recycle
static void test_packets_recycle (void) {

	Packet *packet = packet_create_with_data (RECYCLE_SMALL_SIZE);
	test_check_ptr (packet);
	test_check_unsigned_eq (packet->data_class, RECYCLE_SMALL_CLASS, "100 bytes are in the class of 128");

	Packet *const first = packet;
	void *const first_data = packet->data;
	packet_delete (packet);

	packet = packet_create_with_data (RECYCLE_CLASS_SIZE);
	test_check_ptr (packet);

	if ((PACKET_RECYCLE_LIMIT) > 0) {
		test_check_ptr_eq (packet, first);
		test_check_ptr_eq (packet->data, first_data);
	}

	test_check_null_ptr (packet->recycle_next);
	packet_delete (packet);

	// a big buffer is not recycled
	packet = packet_create_with_data (RECYCLE_BIG_SIZE);
	test_check_ptr (packet);
	test_check_unsigned_eq (packet->data_class, 0, NULL);
	packet_delete (packet);

}

// the data grows from one class to the next ones and out of them without losing a byte
static void test_packets_recycle_append (void) {

	Packet *packet = packet_new ();
	test_check_ptr (packet);

	char chunk[RECYCLE_SMALL_SIZE];
	(void) memset (chunk, RECYCLE_BYTE, RECYCLE_SMALL_SIZE);

	for (unsigned int index = 0; index < RECYCLE_APPENDS; index++) {
		test_check_unsigned_eq (packet_append_data (packet, chunk, RECYCLE_SMALL_SIZE), 0, NULL);
	}

	test_check_unsigned_eq (packet->data_size, RECYCLE_SMALL_SIZE * RECYCLE_APPENDS, NULL);
	test_check_unsigned_eq (packet->data_class, 0, "6400 bytes do not fit a class");

	const char *data = (const char *) packet->data;
	for (size_t index = 0; index < packet->data_size; index++) {
		test_check (data[index] == RECYCLE_BYTE, "a byte was lost when the data grew");
	}

	// replaced by data of its own, and then by a reference
	test_check_unsigned_eq (packet_set_data (packet, chunk, RECYCLE_SMALL_SIZE), 0, NULL);
	test_check_unsigned_ne (packet->data_class, 0);
	test_check_unsigned_eq (packet_set_data_ref (packet, chunk, RECYCLE_SMALL_SIZE), 0, NULL);
	test_check_unsigned_eq (packet->data_class, 0, NULL);

	packet_delete (packet);

}

static void *packets_recycle_thread (void *packet_ptr) {

	Packet **packet = (Packet **) packet_ptr;

	*packet = packet_create_with_data (RECYCLE_SMALL_SIZE);
	packet_delete (*packet);

	return NULL;

}

// the packets that a thread kept go to the shared lists when it ends
static void test_packets_recycle_thread (void) {

	Packet *released = NULL;

	pthread_t thread;
	test_check_int_eq (pthread_create (&thread, NULL, packets_recycle_thread, &released), 0, NULL);
	test_check_int_eq (pthread_join (thread, NULL), 0, NULL);

	test_check_ptr (released);

	// this thread takes its own first, so it takes from the shared list until it finds it
	bool found = false;
	Packet *taken[PACKET_THREAD_CACHE_SIZE + 1];
	unsigned int count = 0;
	while ((count <= PACKET_THREAD_CACHE_SIZE) && !found) {
		taken[count] = packet_new ();
		found = (taken[count] == released);
		count += 1;
	}

	for (unsigned int index = 0; index < count; index++) {
		packet_delete (taken[index]);
	}

	if ((PACKET_RECYCLE_LIMIT) > 0) {
		test_check (found, "the packet of the thread that ended was not reused");
	}

}

#pragma endregion

int main (void) {

	test_output_init ();

	(void) printf ("Testing PACKETS...\n");

	// a closed peer must fail the send, not kill the test
	(void) signal (SIGPIPE, SIG_IGN);

	// header
	test_packet_header_create ();
	test_packet_header_encode ();
	test_packet_header_decode ();
	test_packet_header_decode_rejects ();
	test_packet_header_request_changes ();
	test_packet_header_create_from ();
	test_packet_header_copy ();

	// recycle
	test_packets_recycle ();
	test_packets_recycle_append ();
	test_packets_recycle_thread ();

	// handler
	test_packets_create_with_data ();
	test_packets_add_data_good ();
	test_packets_add_data_bad ();
	test_packets_add_data_multiple ();

	// public
	test_packets_create_ping ();
	test_packets_append_data ();
	test_packets_create_no_data ();
	test_packets_create_full ();
	test_packets_create_and_generate_no_data ();
	test_packets_create_and_generate_complete ();
	test_packets_generate_empty ();
	test_packets_generate_full ();
	test_packets_generate_request ();
	test_packets_generate_created_request ();

	// send, with a version that is not the default one
	const ProtocolVersion version = { CHECK_MAJOR, CHECK_MINOR };
	packets_set_protocol_version (version);

	test_packets_send_split ();
	test_packets_send_pieces ();
	test_packets_send_pieces_failed ();
	test_packets_send_to_socket ();
	test_packets_send_request ();
	test_packets_route_between_connections ();
	test_packets_check ();

	(void) printf ("\nDone with PACKETS tests!\n\n");

	return 0;

}
