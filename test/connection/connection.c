#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/connection/connection.h>
#include <cerver/connection/end.h>
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
#define CONNECTION_TEST_PORT					7001

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

// a NULL name or ip keeps the value that the connection had
static void test_connection_setters_null (void) {

	Connection *connection = test_connection_create ();

	connection_set_name (connection, connection_name);
	connection_set_name (connection, NULL);
	test_check_str_eq (connection->name, connection_name, "a NULL name changed the name");

	connection_set_values (connection, "127.0.0.1", CONNECTION_TEST_PORT, PROTOCOL_TCP, false);
	connection_set_values (connection, NULL, CONNECTION_TEST_PORT, PROTOCOL_TCP, false);
	test_check_str_eq (connection->ip, "127.0.0.1", "a NULL ip changed the ip");

	connection_delete (connection);

}

#define CONNECTION_TEST_RECEIVED_SIZE			64

static unsigned int received_data_deleted = 0;

static void test_connection_received_data_delete (void *data) {

	received_data_deleted += 1;

	free (data);

}

// different received data releases the data that it replaces (a leak before), the same data again is kept
static void test_connection_received_data_replaced (void) {

	Connection *connection = test_connection_create ();

	void *first = malloc (CONNECTION_TEST_RECEIVED_SIZE);
	test_check_ptr (first);
	connection_set_received_data (connection, first, CONNECTION_TEST_RECEIVED_SIZE, test_connection_received_data_delete);
	connection_set_received_data (connection, first, CONNECTION_TEST_RECEIVED_SIZE, test_connection_received_data_delete);
	test_check_unsigned_eq (received_data_deleted, 0, "the same data was released");

	void *second = malloc (CONNECTION_TEST_RECEIVED_SIZE);
	test_check_ptr (second);
	connection_set_received_data (connection, second, CONNECTION_TEST_RECEIVED_SIZE, test_connection_received_data_delete);
	test_check_unsigned_eq (received_data_deleted, 1, "the replaced data was not released");

	connection_delete (connection);
	test_check_unsigned_eq (received_data_deleted, 2, NULL);

}

#define CONNECTION_TEST_END_THREADS				2

static void *test_connection_end_thread (void *connection_ptr) {

	connection_end ((Connection *) connection_ptr);

	return NULL;

}

// two threads that end the same connection, like a client teardown and its reconnection thread,
// agree on which one ends it (a data race of the active flag with TSan)
static void test_connection_end_concurrent (void) {

	Connection *connection = test_connection_create ();

	int fds[CONNECTION_TEST_END_THREADS] = { -1, -1 };
	test_check_int_eq (socketpair (AF_UNIX, SOCK_STREAM, 0, fds), 0, NULL);
	connection->socket->sock_fd = fds[0];
	connection_set_state (connection, CONNECTION_STATE_READY);
	connection->active = true;

	pthread_t threads[CONNECTION_TEST_END_THREADS];
	for (unsigned int i = 0; i < CONNECTION_TEST_END_THREADS; i++) {
		test_check_int_eq (pthread_create (&threads[i], NULL, test_connection_end_thread, connection), 0, NULL);
	}

	for (unsigned int i = 0; i < CONNECTION_TEST_END_THREADS; i++) {
		(void) pthread_join (threads[i], NULL);
	}

	test_check_bool_eq (connection->active, false, "an ended connection is still active");
	test_check_int_eq (connection->socket->sock_fd, -1, "the socket of an ended connection is open");
	test_check_int_eq (connection_get_state (connection), CONNECTION_STATE_DISCONNECTED, NULL);

	(void) close (fds[1]);
	connection_delete (connection);

}

int main (void) {

	test_output_init ();

	(void) printf ("Testing CONNECTION...\n");

	test_connection_base_configuration ();

	test_connection_setters_invalid ();

	test_connection_setters_null ();

	test_connection_received_data_replaced ();

	test_connection_storage ();

	test_connection_recycle ();

	test_connection_end_concurrent ();

	(void) printf ("\nDone with CONNECTION tests!\n\n");

	return 0;

}
