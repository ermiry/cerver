#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/packets.h>

#include "test.h"

#define BUFFER_SIZE			128

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

	test_check_str_len (packet->data, n_copies * buffer_len, NULL);

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

	test_check_bool_eq (ping->packet_ref, false, NULL);

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

int main (int argc, char **argv) {

	(void) printf ("Testing PACKETS...\n");

	// header
	test_packet_header_create ();
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

	(void) printf ("\nDone with PACKETS tests!\n\n");

	return 0;

}