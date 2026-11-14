#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/connection/connection.h>
#include <cerver/connection/send.h>
#include <cerver/packets/packets.h>

#include "../test.h"

static const char *connection_name = "test-connection";

static Connection *test_connection_create (void) {

	Connection *connection = connection_create_empty ();

	test_check_str_eq (connection->name, CONNECTION_DEFAULT_NAME, NULL);
	test_check_str_len (connection->name, strlen (CONNECTION_DEFAULT_NAME), NULL);
	test_check_unsigned_eq (connection->connected_timestamp, 0, NULL);
	test_check_ptr (connection->socket);
	test_check_ptr (connection->stats);

	return connection;

}

static void test_connection_base_configuration (void) {

	Connection *connection = test_connection_create ();

	connection_set_name (connection,connection_name);
	test_check_str_eq (connection->name, connection_name, NULL);
	test_check_str_len (connection->name, strlen (connection_name), NULL);

	connection_delete (connection);

}

#define CONNECTION_TEST_RECYCLE_ROUNDS			64
#define CONNECTION_TEST_RECYCLE_BYTES			1024
#define CONNECTION_TEST_RECYCLE_PACKETS			16

// the stats, the state mutex and the synchronization values live inside the connection
static void test_connection_storage (void) {

	Connection *connection = test_connection_create ();

	test_check_ptr_eq (connection->stats, &connection->stats_storage);
	test_check_ptr_eq (connection->stats->received_packets, &connection->stats->received_packets_storage);
	test_check_ptr_eq (connection->stats->sent_packets, &connection->stats->sent_packets_storage);
	test_check_ptr_eq (connection->state_mutex, &connection->state_mutex_storage);
	test_check_null_ptr (connection->cond);
	test_check_null_ptr (connection->mutex);

	test_check_ptr_eq (connection->socket->read_mutex, &connection->socket->read_mutex_storage);
	test_check_ptr_eq (connection->socket->write_mutex, &connection->socket->write_mutex_storage);
	test_check_unsigned_eq (connection->socket->references, 1, NULL);

	connection_delete (connection);

	connection = connection_create_complete ();
	test_check_ptr_eq (connection->cond, &connection->cond_storage);
	test_check_ptr_eq (connection->mutex, &connection->mutex_storage);
	connection_delete (connection);

}

// a connection that is created after another one was deleted
// starts without anything of the deleted one, whether it was reused or not
static void test_connection_recycle (void) {

	for (unsigned int i = 0; i < CONNECTION_TEST_RECYCLE_ROUNDS; i++) {
		Connection *connection = test_connection_create ();

		test_check_unsigned_eq (connection->stats->total_bytes_received, 0, NULL);
		test_check_unsigned_eq (connection->stats->n_packets_received, 0, NULL);
		test_check_unsigned_eq (connection->stats->received_packets->n_app_packets, 0, NULL);
		test_check_unsigned_eq (connection->stats->sent_packets->n_app_packets, 0, NULL);
		test_check_int_eq (connection->socket->sock_fd, -1, NULL);
		test_check_null_ptr (connection->socket->packet_buffer);
		test_check_unsigned_eq (connection->socket->references, 1, NULL);
		test_check_null_ptr (connection->recycle_next);
		test_check_null_ptr (connection->socket->recycle_next);
		test_check_bool_eq (connection->active, false, NULL);

		connection_set_name (connection, connection_name);
		connection->stats->total_bytes_received = CONNECTION_TEST_RECYCLE_BYTES;
		connection->stats->n_packets_received = CONNECTION_TEST_RECYCLE_PACKETS;
		connection->stats->received_packets->n_app_packets = CONNECTION_TEST_RECYCLE_PACKETS;
		connection->stats->sent_packets->n_app_packets = CONNECTION_TEST_RECYCLE_PACKETS;

		// a reference that outlives the connection keeps the socket
		Socket *socket = socket_reference (connection->socket);
		connection_delete (connection);
		test_check_unsigned_eq (socket->references, 1, NULL);
		socket_delete (socket);
	}

}

#define CONNECTION_TEST_BUFFER_SIZE				2048

static unsigned int released_packets = 0;

static void test_connection_packet_release (void *args) {

	(void) args;

	released_packets += 1;

}

// a receive buffer of 0 bytes is ignored, and a packet that can not be queued
// is deleted like the send thread does with the ones it sends
static void test_connection_setters_invalid (void) {

	Connection *connection = test_connection_create ();

	connection_set_receive_buffer_size (connection, CONNECTION_TEST_BUFFER_SIZE);
	connection_set_receive_buffer_size (connection, 0);
	test_check_unsigned_eq (
		connection->receive_packet_buffer_size, CONNECTION_TEST_BUFFER_SIZE,
		"set a receive buffer of 0 bytes"
	);

	Packet *packet = packet_new ();
	test_check_ptr (packet);
	packet->release = test_connection_packet_release;

	released_packets = 0;
	connection_send_packet (connection, packet);
	test_check_unsigned_eq (released_packets, 1, "a packet without a send queue was not deleted");

	connection_delete (connection);

}

int main (void) {

	test_output_init ();

	(void) printf ("Testing CONNECTION...\n");

	test_connection_base_configuration ();

	test_connection_setters_invalid ();

	test_connection_storage ();

	test_connection_recycle ();

	(void) printf ("\nDone with CONNECTION tests!\n\n");

	return 0;

}
