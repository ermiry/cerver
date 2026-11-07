#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <signal.h>
#include <unistd.h>

#include <sys/socket.h>

#include <cerver/connection.h>
#include <cerver/packets.h>
#include <cerver/socket.h>

#include "test.h"

#define BUFFER_SIZE			128

#define SEND_REQUEST_TYPE		7
#define SEND_DATA_SIZE			3000
#define SEND_PIECES				3
#define SEND_PIECE_SIZE			1000
#define ROUTE_BODY_SIZE			((4096 * 3) + 123)

#define CHECK_MAJOR				2
#define CHECK_MINOR				4

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
	test_check_unsigned_eq (header->sock_fd, 0, NULL);

	packet_header_delete (header);

}

// a header is sent as it is in memory, so the bytes of its padding must be zeroed:
// they sent uninitialized memory of the process (found by valgrind in the core tests)
static void test_packet_header_padding (void) {

	PacketHeader expected;
	(void) memset (&expected, 0, sizeof (PacketHeader));
	expected.packet_type = PACKET_TYPE_APP;
	expected.packet_size = sizeof (PacketHeader);
	expected.request_type = SEND_REQUEST_TYPE;
	expected.version = packet_version_local ();

	// the memory that is reused, with garbage where the padding is
	PacketHeader *dirty = (PacketHeader *) malloc (sizeof (PacketHeader));
	test_check_ptr (dirty);
	(void) memset (dirty, 0xff, sizeof (PacketHeader));
	free (dirty);

	PacketHeader *header = packet_header_create (PACKET_TYPE_APP, sizeof (PacketHeader), SEND_REQUEST_TYPE);
	test_check_ptr (header);
	test_check (!memcmp (header, &expected, sizeof (PacketHeader)), "the created header has garbage");
	packet_header_delete (header);

	Packet *request = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (request);
	test_check (!memcmp (&request->header, &expected, sizeof (PacketHeader)), "the request header has garbage");
	packet_delete (request);

}

static void test_packet_header_create_from (void) {

	u32 request_type = 10;
	u8 handler_id = 2;
	size_t packet_size = sizeof (PacketHeader);
	u16 sock_fd = 16;

	PacketHeader source = {
		.packet_type = PACKET_TYPE_APP,
		.packet_size = packet_size,

		.handler_id = handler_id,

		.request_type = request_type,

		.sock_fd = sock_fd
	};

	PacketHeader *real_header = packet_header_create_from (&source);

	test_check_ptr (real_header);
	test_check_unsigned_eq (real_header->packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (real_header->packet_size, packet_size, NULL);
	test_check_unsigned_eq (real_header->handler_id, handler_id, NULL);
	test_check_unsigned_eq (real_header->request_type, request_type, NULL);
	test_check_unsigned_eq (real_header->sock_fd, sock_fd, NULL);

	packet_header_delete (real_header);

}

static void test_packet_header_copy (void) {

	u32 request_type = 10;
	u8 handler_id = 2;
	size_t packet_size = sizeof (PacketHeader);
	u16 sock_fd = 16;

	PacketHeader source = {
		.packet_type = PACKET_TYPE_APP,
		.packet_size = packet_size,

		.handler_id = handler_id,

		.request_type = request_type,

		.sock_fd = sock_fd
	};

	PacketHeader destination = {
		.packet_type = PACKET_TYPE_NONE,
		.packet_size = 0,

		.handler_id = 0,

		.request_type = 0,

		.sock_fd = 0
	};

	test_check_unsigned_eq (packet_header_copy (&destination, &source), 0, NULL);

	test_check_unsigned_eq (destination.packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (destination.packet_size, packet_size, NULL);
	test_check_unsigned_eq (destination.handler_id, handler_id, NULL);
	test_check_unsigned_eq (destination.request_type, request_type, NULL);
	test_check_unsigned_eq (destination.sock_fd, sock_fd, NULL);

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
	test_check_unsigned_eq (packet->header.sock_fd, 0, NULL);

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
	test_check_unsigned_eq (ping->header.packet_size, sizeof (PacketHeader), NULL);
	test_check_unsigned_eq (ping->header.handler_id, 0, NULL);
	test_check_unsigned_eq (ping->header.request_type, 0, NULL);
	test_check_unsigned_eq (ping->header.sock_fd, 0, NULL);

	// check real packet
	test_check_unsigned_eq (ping->packet_size, sizeof (PacketHeader), NULL);
	test_check_ptr (ping->packet);
	test_check_unsigned_eq (((PacketHeader *) ping->packet)->packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (((PacketHeader *) ping->packet)->packet_size, sizeof (PacketHeader), NULL);
	test_check_unsigned_eq (((PacketHeader *) ping->packet)->handler_id, 0, NULL);
	test_check_unsigned_eq (((PacketHeader *) ping->packet)->request_type, 0, NULL);
	test_check_unsigned_eq (((PacketHeader *) ping->packet)->sock_fd, 0, NULL);

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

	test_check_unsigned_eq (packet->packet_size, sizeof (PacketHeader), NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, request_type, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->sock_fd, packet->header.sock_fd, NULL);

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

	test_check_unsigned_eq (packet->packet_size, sizeof (PacketHeader) + BUFFER_SIZE, NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, request_type, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->sock_fd, packet->header.sock_fd, NULL);

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

	test_check_unsigned_eq (packet->packet_size, sizeof (PacketHeader), NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_NONE, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, 0, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->sock_fd, packet->header.sock_fd, NULL);

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

	test_check_unsigned_eq (packet->packet_size, sizeof (PacketHeader) + BUFFER_SIZE, NULL);
	test_check_unsigned_eq (packet->header.packet_type, PACKET_TYPE_TEST, NULL);
	test_check_unsigned_eq (packet->header.packet_size, packet->packet_size, NULL);
	test_check_unsigned_eq (packet->header.request_type, request_type, NULL);

	test_check_ptr (packet->packet);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->packet_size, packet->header.packet_size, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->handler_id, packet->header.handler_id, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->request_type, packet->header.packet_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) packet->packet)->sock_fd, packet->header.sock_fd, NULL);

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
	size_t real_packet_size = sizeof (PacketHeader) + BUFFER_SIZE;
	test_check_unsigned_eq (request->packet_size, real_packet_size, NULL);

	test_check_unsigned_eq (((PacketHeader *) request->packet)->packet_type, PACKET_TYPE_APP, NULL);
	test_check_unsigned_eq (((PacketHeader *) request->packet)->packet_size, real_packet_size, NULL);
	test_check_unsigned_eq (((PacketHeader *) request->packet)->handler_id, 0, NULL);
	test_check_unsigned_eq (((PacketHeader *) request->packet)->request_type, request_type, NULL);
	test_check_unsigned_eq (((PacketHeader *) request->packet)->sock_fd, 0, NULL);

	char *end = (char *) request->packet;
	end += sizeof (PacketHeader);
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
	test_check_unsigned_eq (sent, sizeof (PacketHeader) + SEND_DATA_SIZE, NULL);

	PacketHeader header;
	send_read (pair.fds[1], (char *) &header, sizeof (PacketHeader));
	send_check_header (&header, PACKET_TYPE_APP, SEND_REQUEST_TYPE, sizeof (PacketHeader) + SEND_DATA_SIZE);

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

	const size_t packet_size = sizeof (PacketHeader) + (SEND_PIECES * SEND_PIECE_SIZE);

	Packet *packet = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (packet);
	packet->header.packet_size = packet_size;
	packet_set_network_values (packet, NULL, NULL, pair.connection, NULL);

	size_t sent = 0;
	test_check_unsigned_eq (packet_send_pieces (packet, pieces, sizes, SEND_PIECES, 0, &sent), 0, NULL);
	test_check_unsigned_eq (sent, packet_size, NULL);

	PacketHeader header;
	send_read (pair.fds[1], (char *) &header, sizeof (PacketHeader));
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

	send_check_header ((PacketHeader *) received, PACKET_TYPE_CUSTOM, SEND_REQUEST_TYPE, packet->packet_size);
	test_check_int_eq (memcmp (received + sizeof (PacketHeader), data, SEND_DATA_SIZE), 0, NULL);

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

	PacketHeader header;
	send_read (pair.fds[1], (char *) &header, sizeof (PacketHeader));
	send_check_header (&header, PACKET_TYPE_APP, SEND_REQUEST_TYPE, sizeof (PacketHeader));

	send_pair_delete (&pair);

}

// a request packet references its own header, generating it must not release it
static void test_packets_generate_created_request (void) {

	Packet *packet = packet_create_request (PACKET_TYPE_APP, SEND_REQUEST_TYPE);
	test_check_ptr (packet);

	test_check_unsigned_eq (packet_generate (packet), 0, NULL);
	test_check_ptr (packet->packet);
	test_check_bool_eq (packet->packet_ref, false, NULL);
	test_check_unsigned_eq (packet->packet_size, sizeof (PacketHeader), NULL);

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
	header.packet_size = sizeof (PacketHeader) + ROUTE_BODY_SIZE;
	header.version = packet_version_local ();

	size_t sent = 0;
	test_check_unsigned_eq (packet_route_between_connections (from.connection, to.connection, &header, &sent), 0, NULL);
	test_check_unsigned_eq (sent, header.packet_size, NULL);

	PacketHeader received_header;
	send_read (to.fds[1], (char *) &received_header, sizeof (PacketHeader));
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

int main (int argc, char **argv) {

	test_output_init ();

	(void) printf ("Testing PACKETS...\n");

	// a closed peer must fail the send, not kill the test
	(void) signal (SIGPIPE, SIG_IGN);

	// header
	test_packet_header_create ();
	test_packet_header_padding ();
	test_packet_header_create_from ();
	test_packet_header_copy ();

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