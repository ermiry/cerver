#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <netinet/in.h>

#include <cerver/auth.h>
#include <cerver/connection.h>
#include <cerver/network.h>
#include <cerver/packets.h>
#include <cerver/sessions.h>
#include <cerver/udp.h>

#include "test.h"

// the datagrams of UDP: a single packet followed by nothing or by a token,
// the token of a connection, the release of a packet, and the datagrams that packet_send () sends

#define UDP_TEST_LOOPBACK				"127.0.0.1"
#define UDP_TEST_DATA_SIZE				64
#define UDP_TEST_DATA_BYTE				0x5a
#define UDP_TEST_RECEIVE_MICROSECONDS	(1 << 19)
#define UDP_TEST_EXTRA_BYTE				1

static const u8 udp_test_token[CONNECTION_DATAGRAM_TOKEN_SIZE] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10
};

// a datagram with a packet of data_size bytes, followed by trailing bytes
static char *udp_test_datagram (const size_t data_size, const size_t trailing, size_t *size) {

	Packet *packet = packet_generate_request (PACKET_TYPE_APP, 0, NULL, 0);
	test_check_ptr (packet);

	*size = sizeof (PacketHeader) + data_size + trailing;

	char *datagram = (char *) calloc (*size, sizeof (char));
	test_check_ptr (datagram);

	packet->header.packet_size = sizeof (PacketHeader) + data_size;
	(void) memcpy (datagram, &packet->header, sizeof (PacketHeader));
	(void) memset (datagram + sizeof (PacketHeader), UDP_TEST_DATA_BYTE, data_size);

	packet_delete (packet);

	return datagram;

}

static UdpDatagram udp_test_check (const size_t data_size, const size_t trailing) {

	size_t size = 0;
	char *datagram = udp_test_datagram (data_size, trailing, &size);

	PacketHeader header;
	const UdpDatagram kind = udp_datagram_check (datagram, size, MAX_UDP_PACKET_SIZE, &header);

	free (datagram);

	return kind;

}

static void test_udp_datagram_check (void) {

	PacketHeader header;

	test_check_int_eq (udp_test_check (UDP_TEST_DATA_SIZE, 0), UDP_DATAGRAM_PLAIN, NULL);
	test_check_int_eq (udp_test_check (0, 0), UDP_DATAGRAM_PLAIN, NULL);
	test_check_int_eq (udp_test_check (UDP_TEST_DATA_SIZE, CONNECTION_DATAGRAM_TOKEN_SIZE), UDP_DATAGRAM_TOKEN, NULL);

	// trailing bytes that are not a token
	test_check_int_eq (udp_test_check (UDP_TEST_DATA_SIZE, UDP_TEST_EXTRA_BYTE), UDP_DATAGRAM_INVALID, NULL);
	test_check_int_eq (
		udp_test_check (UDP_TEST_DATA_SIZE, CONNECTION_DATAGRAM_TOKEN_SIZE + UDP_TEST_EXTRA_BYTE),
		UDP_DATAGRAM_INVALID, NULL
	);

	// shorter than a header, and without a datagram
	const char short_datagram[sizeof (PacketHeader) - UDP_TEST_EXTRA_BYTE] = { 0 };
	test_check_int_eq (
		udp_datagram_check (short_datagram, sizeof (short_datagram), MAX_UDP_PACKET_SIZE, &header),
		UDP_DATAGRAM_INVALID, NULL
	);

	test_check_int_eq (udp_datagram_check (NULL, 0, MAX_UDP_PACKET_SIZE, &header), UDP_DATAGRAM_INVALID, NULL);

	// a packet bigger than the datagram, smaller than its header, and bigger than the max
	size_t size = 0;
	char *datagram = udp_test_datagram (UDP_TEST_DATA_SIZE, 0, &size);

	PacketHeader *packet_header = (PacketHeader *) datagram;

	packet_header->packet_size = size + UDP_TEST_EXTRA_BYTE;
	test_check_int_eq (udp_datagram_check (datagram, size, MAX_UDP_PACKET_SIZE, &header), UDP_DATAGRAM_INVALID, NULL);

	packet_header->packet_size = sizeof (PacketHeader) - UDP_TEST_EXTRA_BYTE;
	test_check_int_eq (udp_datagram_check (datagram, size, MAX_UDP_PACKET_SIZE, &header), UDP_DATAGRAM_INVALID, NULL);

	packet_header->packet_size = size;
	test_check_int_eq (udp_datagram_check (datagram, size, size - UDP_TEST_EXTRA_BYTE, &header), UDP_DATAGRAM_INVALID, NULL);
	test_check_int_eq (udp_datagram_check (datagram, size, size, &header), UDP_DATAGRAM_PLAIN, NULL);

	free (datagram);

}

static void test_udp_token_matches (void) {

	size_t size = 0;
	char *datagram = udp_test_datagram (UDP_TEST_DATA_SIZE, CONNECTION_DATAGRAM_TOKEN_SIZE, &size);

	PacketHeader header;
	test_check_int_eq (udp_datagram_check (datagram, size, MAX_UDP_PACKET_SIZE, &header), UDP_DATAGRAM_TOKEN, NULL);

	(void) memcpy (datagram + header.packet_size, udp_test_token, CONNECTION_DATAGRAM_TOKEN_SIZE);
	test_check_true (udp_datagram_token_matches (datagram, &header, udp_test_token));

	// any byte that differs
	for (size_t i = 0; i < CONNECTION_DATAGRAM_TOKEN_SIZE; i++) {
		datagram[header.packet_size + i] ^= 1;
		test_check_false (udp_datagram_token_matches (datagram, &header, udp_test_token));
		datagram[header.packet_size + i] ^= 1;
	}

	free (datagram);

}

static void test_udp_packet_create (void) {

	size_t size = 0;
	char *datagram = udp_test_datagram (UDP_TEST_DATA_SIZE, 0, &size);

	PacketHeader header;
	test_check_int_eq (udp_datagram_check (datagram, size, MAX_UDP_PACKET_SIZE, &header), UDP_DATAGRAM_PLAIN, NULL);

	Packet *packet = udp_datagram_packet_create (datagram, &header);
	test_check_ptr (packet);

	test_check_unsigned_eq (packet->packet_size, size, NULL);
	test_check_unsigned_eq (packet->data_size, UDP_TEST_DATA_SIZE, NULL);
	test_check_int_eq (packet->packet_type, PACKET_TYPE_APP, NULL);
	test_check_int_eq (memcmp (packet->data, datagram + sizeof (PacketHeader), UDP_TEST_DATA_SIZE), 0, NULL);

	packet_delete (packet);
	free (datagram);

}

static unsigned int releases = 0;

static void udp_test_release (void *args) {

	test_check_ptr_eq (args, &releases);

	releases += 1;

}

// packet_delete () calls the release of the packet once, before it is released
static void test_udp_packet_release (void) {

	releases = 0;

	Packet *packet = packet_new ();
	test_check_ptr (packet);
	test_check_null_ptr (packet->release);

	packet->release = udp_test_release;
	packet->release_args = &releases;

	packet_delete (packet);

	test_check_unsigned_eq (releases, 1, NULL);

	Packet *request = packet_create_request (PACKET_TYPE_TEST, 0);
	test_check_null_ptr (request->release);
	packet_delete (request);

	test_check_unsigned_eq (releases, 1, NULL);

}

static void test_udp_connection_token (void) {

	Connection *connection = connection_create_empty ();
	test_check_ptr (connection);

	u8 token[CONNECTION_DATAGRAM_TOKEN_SIZE] = { 0 };

	test_check_false (connection_has_datagram_token (connection));
	test_check_false (connection_get_datagram_token (connection, token));

	connection_set_datagram_token (connection, udp_test_token);
	test_check_true (connection_has_datagram_token (connection));
	test_check_true (connection_get_datagram_token (connection, token));
	test_check_int_eq (memcmp (token, udp_test_token, CONNECTION_DATAGRAM_TOKEN_SIZE), 0, NULL);

	connection_clear_datagram_token (connection);
	test_check_false (connection_has_datagram_token (connection));

	connection_delete (connection);

	// a recycled connection does not keep the token
	connection = connection_create_empty ();
	test_check_false (connection_has_datagram_token (connection));
	connection_delete (connection);

}

// a token without its NUL is taken as credentials, it was read past the packet
static void test_udp_auth_data (void) {

	SToken session = { { 0 } };
	(void) memset (session.token, UDP_TEST_DATA_BYTE, TOKEN_SIZE - UDP_TEST_EXTRA_BYTE);

	Packet *packet = packet_create (PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_CLIENT_AUTH, &session, sizeof (SToken));
	AuthData *auth_data = auth_data_get (packet);
	test_check_ptr (auth_data);
	test_check_ptr (auth_data->token);
	test_check_unsigned_eq (auth_data->token->len, TOKEN_SIZE - UDP_TEST_EXTRA_BYTE, NULL);
	auth_data_release (auth_data);
	packet_delete (packet);

	(void) memset (session.token, UDP_TEST_DATA_BYTE, TOKEN_SIZE);

	packet = packet_create (PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_CLIENT_AUTH, &session, sizeof (SToken));
	auth_data = auth_data_get (packet);
	test_check_ptr (auth_data);
	test_check_null_ptr (auth_data->token);
	test_check_unsigned_eq (auth_data->auth_data_size, sizeof (SToken), NULL);
	auth_data_release (auth_data);
	packet_delete (packet);

}

typedef struct UdpTestPair {

	int receiver;
	Connection *connection;

} UdpTestPair;

// an UDP connection with its own socket, connected to a receiver
static UdpTestPair udp_test_pair_create (void) {

	UdpTestPair pair = { -1, NULL };

	struct sockaddr_storage address;
	test_check_unsigned_eq (network_parse_address (UDP_TEST_LOOPBACK, 0, &address), 0, NULL);

	pair.receiver = socket (AF_INET, SOCK_DGRAM, 0);
	test_check_int_gt (pair.receiver, -1);
	test_check_int_eq (bind (pair.receiver, (struct sockaddr *) &address, sock_address_size (&address)), 0, NULL);

	socklen_t address_size = sizeof (address);
	test_check_int_eq (getsockname (pair.receiver, (struct sockaddr *) &address, &address_size), 0, NULL);

	const struct timeval timeout = { 0, UDP_TEST_RECEIVE_MICROSECONDS };
	test_check_int_eq (setsockopt (pair.receiver, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)), 0, NULL);

	pair.connection = connection_create_empty ();
	test_check_ptr (pair.connection);

	pair.connection->protocol = PROTOCOL_UDP;
	pair.connection->socket->sock_fd = socket (AF_INET, SOCK_DGRAM, 0);
	test_check_int_gt (pair.connection->socket->sock_fd, -1);

	test_check_int_eq (
		connect (pair.connection->socket->sock_fd, (struct sockaddr *) &address, address_size),
		0, NULL
	);

	return pair;

}

static void udp_test_pair_delete (UdpTestPair *pair) {

	(void) close (pair->connection->socket->sock_fd);
	pair->connection->socket->sock_fd = -1;

	connection_delete (pair->connection);

	(void) close (pair->receiver);

}

// sends a packet with data_size bytes and returns the size of the datagram that arrived
static ssize_t udp_test_send (
	const UdpTestPair *pair, const size_t data_size, const bool split, char *buffer
) {

	char *data = (char *) calloc (data_size + UDP_TEST_EXTRA_BYTE, sizeof (char));
	test_check_ptr (data);

	Packet *packet = packet_generate_request (PACKET_TYPE_APP, 0, data, data_size);
	test_check_ptr (packet);

	packet_set_network_values (packet, NULL, NULL, pair->connection, NULL);

	const u8 result = split ? packet_send_split (packet, 0, NULL) : packet_send (packet, 0, NULL, false);

	packet_delete (packet);
	free (data);

	return result ? -1 : recv (pair->receiver, buffer, MAX_UDP_PACKET_SIZE, 0);

}

// a packet goes whole in a datagram, followed by the token of the connection when it has one,
// and a packet that does not fit is not sent
static void test_udp_send (void) {

	UdpTestPair pair = udp_test_pair_create ();

	char *buffer = (char *) malloc (MAX_UDP_PACKET_SIZE);
	test_check_ptr (buffer);

	const size_t biggest = MAX_UDP_PACKET_SIZE - sizeof (PacketHeader);

	test_check_int_eq ((int) udp_test_send (&pair, UDP_TEST_DATA_SIZE, false, buffer), (int) (sizeof (PacketHeader) + UDP_TEST_DATA_SIZE), NULL);
	test_check_int_eq ((int) udp_test_send (&pair, UDP_TEST_DATA_SIZE, true, buffer), (int) (sizeof (PacketHeader) + UDP_TEST_DATA_SIZE), NULL);
	test_check_int_eq ((int) udp_test_send (&pair, biggest, false, buffer), MAX_UDP_PACKET_SIZE, NULL);
	test_check_int_eq ((int) udp_test_send (&pair, biggest + UDP_TEST_EXTRA_BYTE, false, buffer), -1, NULL);

	connection_set_datagram_token (pair.connection, udp_test_token);

	const ssize_t received = udp_test_send (&pair, UDP_TEST_DATA_SIZE, false, buffer);
	test_check_int_eq ((int) received, (int) (sizeof (PacketHeader) + UDP_TEST_DATA_SIZE + CONNECTION_DATAGRAM_TOKEN_SIZE), NULL);
	test_check_int_eq (
		memcmp (buffer + sizeof (PacketHeader) + UDP_TEST_DATA_SIZE, udp_test_token, CONNECTION_DATAGRAM_TOKEN_SIZE),
		0, NULL
	);

	// with the token, the biggest packet does not fit anymore
	test_check_int_eq ((int) udp_test_send (&pair, biggest, false, buffer), -1, NULL);

	free (buffer);

	udp_test_pair_delete (&pair);

}

int main (int argc, char **argv) {

	test_output_init ();

	(void) argc;
	(void) argv;

	(void) printf ("Testing UDP...\n");

	test_udp_datagram_check ();
	test_udp_token_matches ();
	test_udp_packet_create ();
	test_udp_packet_release ();
	test_udp_connection_token ();
	test_udp_auth_data ();
	test_udp_send ();

	(void) printf ("\nDone with UDP tests!\n\n");

	return 0;

}
