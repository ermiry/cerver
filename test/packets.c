#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/packets.h>

#include "test.h"

#define BUFFER_SIZE			128

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

	test_check_null_ptr (ping->version);

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
	char *data = request->data;
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

	char *end = request->packet;
	end += sizeof (PacketHeader);
	test_check_str_eq (end, buffer, NULL);
	test_check_str_len (end, strlen (buffer), NULL);

	packet_delete (request);

}

int main (int argc, char **argv) {

	(void) printf ("Testing PACKETS...\n");

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