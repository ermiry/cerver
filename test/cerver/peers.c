#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/time.h>

#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/network.h>
#include <cerver/packets.h>
#include <cerver/sessions.h>
#include <cerver/udp.h>

#include <app/auth.h>

#include "../test.h"

// an UDP cerver in the same process, with cerver clients and with raw sockets:
// - a client gets the cerver info and its app packets are echoed, in IPv4 and IPv6
// - the biggest packet that fits in a datagram, and one that does not fit is not sent
// - a datagram without a valid packet does not create a peer
// - a peer is removed after its max bad packets, its timeout, or its close / disconnect packets
// - the cerver keeps up to its max peers
// - a request (files) or game packet is a bad packet of a peer
// - a stopped cerver does not create a peer from the wake up of its receive
// - a peer expires while a handler with its own thread still has its packets
// - the authentication of a client, a failed one, and the max auth tries
// - a pending peer only sends auth, test and cerver packets, and it expires
// - every datagram of an authenticated peer has its token, a wrong one is a bad packet
// - an auth packet of an authenticated peer gets its success again
// - a peer with its token moves to a new address
// - a session token joins the client of the session from another address
// - the cerver does not answer an unproven address with more than it sent

#define PEERS_IPV4_LOOPBACK				"127.0.0.1"
#define PEERS_IPV6_LOOPBACK				"::1"
#define PEERS_PORT_BASE					7600
#define PEERS_NAME						"peers-cerver"

#define PEERS_THREADS					4
#define PEERS_POLL_TIMEOUT				200
#define PEERS_JOIN_SECONDS				8

#define PEERS_PACKETS					32
#define PEERS_BAD_DATAGRAMS				4
#define PEERS_MAX_BAD_PACKETS			4
#define PEERS_MAX_PEERS					2
#define PEERS_EXTRA_PEERS				2
#define PEERS_TIMEOUT_SECONDS			1
#define PEERS_ON_HOLD_SECONDS			1
#define PEERS_PENDING_BAD_PACKETS		2
#define PEERS_REJECTED_PACKETS			2
#define PEERS_QUEUED_PACKETS			8
#define PEERS_SLOW_HANDLER_NANOSECONDS	(1L << 28)

#define PEERS_AUTH_MAX_TRIES			2
#define PEERS_AUTH_SINGLE_TRY			1
#define PEERS_USERNAME					"ermiry"
#define PEERS_PASSWORD					"049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b"
#define PEERS_BAD_PASSWORD				"bad-password"

#define PEERS_REQUEST_ECHO				1

#define PEERS_RECEIVE_MICROSECONDS		(1 << 19)
#define PEERS_SHORT_DATAGRAM_SIZE		4
#define PEERS_WRONG_SIZE_OFFSET			8

#define PEERS_EVENTS					(CERVER_EVENT_UNKNOWN + 1)

typedef struct PeersCerver {

	Cerver *cerver;
	pthread_t thread;
	u8 start_result;

} PeersCerver;

typedef struct PeersClient {

	Client *client;
	Connection *connection;

} PeersClient;

// a raw socket connected to the cerver, with the token it got when it authenticated
typedef struct PeersSocket {

	int sock_fd;

	u8 token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	bool has_token;

	SToken session;

} PeersSocket;

static u16 peers_port = PEERS_PORT_BASE;

// cerver side
static unsigned int app_packets = 0;
static unsigned int queued_packets = 0;
static unsigned int events[PEERS_EVENTS] = { 0 };

// client side
static unsigned int echoes = 0;

// the events are counted by the thread of the cerver
static unsigned int peers_event_count (const CerverEventType type) {

	return __atomic_load_n (&events[type], __ATOMIC_SEQ_CST);

}

static void peers_counters_reset (void) {

	app_packets = 0;
	queued_packets = 0;
	(void) memset (events, 0, sizeof (events));
	echoes = 0;

}

#pragma region cerver

static void peers_echo (const Packet *packet) {

	Packet *response = packet_generate_request (
		PACKET_TYPE_APP, PEERS_REQUEST_ECHO, NULL, 0
	);

	test_check_ptr (response);

	packet_set_network_values (
		response, packet->cerver, packet->client, packet->connection, NULL
	);

	(void) packet_send (response, 0, NULL, false);

	packet_delete (response);

}

static void peers_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	test_check_ptr (packet->client);

	if (packet->header.request_type == PEERS_REQUEST_ECHO) {
		peers_echo (packet);
	}

	test_counter_add (&app_packets);

}

// a handler with its own thread that is slower than the timeout of the peer
static void peers_slow_handler (void *handler_data_ptr) {

	const HandlerData *handler_data = (const HandlerData *) handler_data_ptr;

	const struct timespec interval = { 0, PEERS_SLOW_HANDLER_NANOSECONDS };
	(void) nanosleep (&interval, NULL);

	// the peer of the packet was removed meanwhile, but it is released after it,
	// so the handler still answers with its connection
	test_check_ptr (handler_data->packet->client);
	peers_echo (handler_data->packet);

	test_counter_add (&queued_packets);

}

static void *peers_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

static void peers_events_register (Cerver *cerver) {

	for (unsigned int type = CERVER_EVENT_STARTED; type < CERVER_EVENT_LOBBY_CREATE; type++) {
		(void) cerver_event_register (
			cerver, (CerverEventType) type,
			peers_event, &events[type], NULL,
			false, false
		);
	}

}

static Cerver *peers_cerver_create_base (const u16 port, const char *bind_address) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_CUSTOM, PEERS_NAME,
		port, PROTOCOL_UDP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, bind_address), 0, NULL);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, PEERS_THREADS);
	cerver_set_poll_time_out (cerver, PEERS_POLL_TIMEOUT);
	cerver_set_max_bad_packets (cerver, PEERS_MAX_BAD_PACKETS);

	peers_events_register (cerver);

	return cerver;

}

// the app handler runs in the receive loop of the cerver
static Cerver *peers_cerver_create_at (const u16 port, const char *bind_address) {

	Cerver *cerver = peers_cerver_create_base (port, bind_address);

	Handler *app_handler = handler_create (peers_app_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (cerver, app_handler, NULL);

	return cerver;

}

static Cerver *peers_cerver_create (const u16 port) {

	return peers_cerver_create_at (port, PEERS_IPV4_LOOPBACK);

}

static Cerver *peers_auth_cerver_create (const u16 port, const u8 max_auth_tries) {

	Cerver *cerver = peers_cerver_create (port);
	test_check_unsigned_eq (cerver_set_auth (cerver, max_auth_tries, app_auth_method), 0, NULL);

	return cerver;

}

static void *peers_cerver_thread (void *peers_ptr) {

	PeersCerver *peers = (PeersCerver *) peers_ptr;

	peers->start_result = cerver_start (peers->cerver);

	return NULL;

}

static void peers_cerver_start (PeersCerver *peers, Cerver *cerver) {

	peers->cerver = cerver;
	peers->start_result = 0;

	test_check_int_eq (pthread_create (&peers->thread, NULL, peers_cerver_thread, peers), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_STARTED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

}

static void peers_cerver_stop (PeersCerver *peers) {

	test_check_unsigned_eq (cerver_shutdown (peers->cerver), 0, NULL);

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += PEERS_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (peers->thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (peers->start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (peers->cerver), 0, NULL);

}

// waits until the cerver has the expected peers
static size_t peers_wait_count (const Cerver *cerver, const size_t expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	size_t count = cerver_udp_peers_count (cerver);
	while ((count != expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		count = cerver_udp_peers_count (cerver);
	}

	return count;

}

// waits until a stat of the cerver, updated by other threads, has the expected value
static u64 peers_wait_stat (const u64 *stat, const u64 expected) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	u64 value = CERVER_STATS_GET (*stat);
	while ((value != expected) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
		value = CERVER_STATS_GET (*stat);
	}

	return value;

}

#pragma endregion

#pragma region client

static void peers_client_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == PEERS_REQUEST_ECHO) {
		test_counter_add (&echoes);
	}

}

static PeersClient peers_client_connect (
	const char *address, const u16 port, Credentials *credentials
) {

	PeersClient peers = { NULL, NULL };

	peers.client = client_create ();
	test_check_ptr (peers.client);

	Handler *app_handler = handler_create (peers_client_app_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (peers.client, app_handler, NULL);

	peers.connection = client_connection_create (
		peers.client, address, port, PROTOCOL_UDP, false
	);

	test_check_ptr (peers.connection);

	if (credentials) {
		connection_set_auth_data (
			peers.connection,
			credentials, sizeof (Credentials),
			credentials_delete, false
		);
	}

	test_check_int_eq (
		client_connect_and_start (peers.client, peers.connection), 0,
		"failed to connect to the peers cerver"
	);

	return peers;

}

static void peers_client_send (
	const PeersClient *peers,
	const PacketType packet_type, const u32 request_type,
	const unsigned int count
) {

	for (unsigned int i = 0; i < count; i++) {
		Packet *packet = packet_create_request (packet_type, request_type);
		test_check_ptr (packet);

		packet_set_network_values (packet, NULL, peers->client, peers->connection, NULL);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

		packet_delete (packet);
	}

}

static void peers_client_end (PeersClient *peers) {

	if (client_is_running (peers->client)) {
		(void) client_connection_end (peers->client, peers->connection);
	}

	test_check_unsigned_eq (client_teardown (peers->client), 0, NULL);

}

#pragma endregion

#pragma region socket

static PeersSocket peers_socket_open (const char *ip, const u16 port) {

	PeersSocket peers = { -1, { 0 }, false, { { 0 } } };

	struct sockaddr_storage address;
	test_check_unsigned_eq (network_parse_address (ip, port, &address), 0, NULL);

	peers.sock_fd = socket (address.ss_family, SOCK_DGRAM, 0);
	test_check_int_gt (peers.sock_fd, -1);

	test_check_int_eq (
		connect (peers.sock_fd, (const struct sockaddr *) &address, sock_address_size (&address)),
		0, NULL
	);

	const struct timeval timeout = { 0, PEERS_RECEIVE_MICROSECONDS };
	test_check_int_eq (
		setsockopt (peers.sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (timeout)),
		0, NULL
	);

	return peers;

}

static void peers_socket_close (PeersSocket *peers) {

	(void) close (peers->sock_fd);
	peers->sock_fd = -1;

}

static void peers_socket_send_raw (const PeersSocket *peers, const void *datagram, const size_t size) {

	test_check_int_eq ((int) send (peers->sock_fd, datagram, size, 0), (int) size, NULL);

}

// the packet followed by the token when one is given
static void peers_socket_send (
	const PeersSocket *peers,
	const PacketType packet_type, const u32 request_type,
	const void *data, const size_t data_size,
	const u8 *token
) {

	Packet *packet = packet_generate_request (packet_type, request_type, data, data_size);
	test_check_ptr (packet);

	const size_t token_size = token ? CONNECTION_DATAGRAM_TOKEN_SIZE : 0;
	const size_t size = packet->packet_size + token_size;

	char *datagram = (char *) malloc (size);
	test_check_ptr (datagram);

	(void) memcpy (datagram, packet->packet, packet->packet_size);
	if (token) (void) memcpy (datagram + packet->packet_size, token, token_size);

	peers_socket_send_raw (peers, datagram, size);

	free (datagram);
	packet_delete (packet);

}

// receives a datagram before the receive timeout
// returns its size, or -1 when there was not one
static ssize_t peers_socket_receive (const PeersSocket *peers, char *buffer, const size_t size) {

	return recv (peers->sock_fd, buffer, size, 0);

}

// receives datagrams until one has a packet of the type and request
// returns its size, or -1 when there was not one
static ssize_t peers_socket_receive_packet (
	const PeersSocket *peers, char *buffer, const size_t size,
	const PacketType packet_type, const u32 request_type
) {

	ssize_t received = 0;
	bool found = false;

	while (!found && ((received = peers_socket_receive (peers, buffer, size)) >= 0)) {
		PacketHeader header;
		if ((size_t) received >= sizeof (PacketHeader)) {
			(void) memcpy (&header, buffer, sizeof (PacketHeader));
			found = (header.packet_type == packet_type) && (header.request_type == request_type);
		}
	}

	return found ? received : -1;

}

static void peers_socket_send_credentials (const PeersSocket *peers, const char *password) {

	Credentials *credentials = credentials_new (PEERS_USERNAME, password);
	test_check_ptr (credentials);

	peers_socket_send (
		peers, PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_CLIENT_AUTH,
		credentials, sizeof (Credentials), NULL
	);

	credentials_delete (credentials);

}

// reads the success of an authentication: the session token when it has one, and the datagram token
static void peers_socket_read_success (PeersSocket *peers, const bool sessions) {

	char buffer[MAX_UDP_PACKET_SIZE];
	const ssize_t received = peers_socket_receive_packet (
		peers, buffer, sizeof (buffer), PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_SUCCESS
	);

	const size_t session_size = sessions ? sizeof (SToken) : 0;
	test_check_int_eq (
		(int) received,
		(int) (sizeof (PacketHeader) + session_size + CONNECTION_DATAGRAM_TOKEN_SIZE),
		"the peer did not get its success"
	);

	const char *data = buffer + sizeof (PacketHeader);
	if (sessions) (void) memcpy (&peers->session, data, sizeof (SToken));

	(void) memcpy (peers->token, data + session_size, CONNECTION_DATAGRAM_TOKEN_SIZE);
	peers->has_token = true;

}

static PeersSocket peers_socket_authenticate (const u16 port, const bool sessions) {

	PeersSocket peers = peers_socket_open (PEERS_IPV4_LOOPBACK, port);

	peers_socket_send_credentials (&peers, PEERS_PASSWORD);
	peers_socket_read_success (&peers, sessions);

	return peers;

}

// an echo request that the cerver answers with an echo that has the token
static bool peers_socket_echo (const PeersSocket *peers, const u8 *token) {

	peers_socket_send (peers, PACKET_TYPE_APP, PEERS_REQUEST_ECHO, NULL, 0, token);

	char buffer[MAX_UDP_PACKET_SIZE];
	const ssize_t received = peers_socket_receive_packet (
		peers, buffer, sizeof (buffer), PACKET_TYPE_APP, PEERS_REQUEST_ECHO
	);

	const size_t expected = sizeof (PacketHeader) + (token ? CONNECTION_DATAGRAM_TOKEN_SIZE : 0);
	return (received == (ssize_t) expected)
		&& (!token || !memcmp (buffer + sizeof (PacketHeader), token, CONNECTION_DATAGRAM_TOKEN_SIZE));

}

#pragma endregion

#pragma region tests

// a client gets the cerver info and its app packets are echoed,
// its close connection packet removes its peer
static void peers_echo_check (const char *address) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create_at (port, address));

	PeersClient client = peers_client_connect (address, port, NULL);

	peers_client_send (&client, PACKET_TYPE_APP, PEERS_REQUEST_ECHO, PEERS_PACKETS);
	test_check_unsigned_eq (test_wait_counter (&echoes, PEERS_PACKETS, TEST_WAIT_SECONDS), PEERS_PACKETS, NULL);

	test_check_ptr (__atomic_load_n (&client.connection->cerver_report, __ATOMIC_ACQUIRE));
	test_check_unsigned_eq (peers_wait_count (peers.cerver, 1), 1, NULL);
	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_CONNECTED), 1, NULL);

	peers_client_end (&client);

	test_check_unsigned_eq (peers_wait_count (peers.cerver, 0), 0, "the close connection did not remove the peer");
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_CLOSE_CONNECTION], 1, TEST_WAIT_SECONDS), 1, NULL
	);
	test_check_unsigned_eq (peers_wait_stat (&peers.cerver->stats->current_n_connected_clients, 0), 0, NULL);
	test_check_unsigned_eq (peers_wait_stat (&peers.cerver->stats->current_active_client_connections, 0), 0, NULL);

	peers_cerver_stop (&peers);

}

static void test_peers_echo (void) {

	peers_echo_check (PEERS_IPV4_LOOPBACK);
	peers_echo_check (PEERS_IPV6_LOOPBACK);

}

// the biggest packet that fits in a datagram reaches the handler,
// one byte more is not sent
static void test_peers_biggest_packet (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	PeersClient client = peers_client_connect (PEERS_IPV4_LOOPBACK, port, NULL);

	const size_t data_size = MAX_UDP_PACKET_SIZE - sizeof (PacketHeader);
	char *data = (char *) calloc (data_size + 1, sizeof (char));
	test_check_ptr (data);

	Packet *biggest = packet_generate_request (PACKET_TYPE_APP, PEERS_REQUEST_ECHO, data, data_size);
	packet_set_network_values (biggest, NULL, client.client, client.connection, NULL);
	test_check_unsigned_eq (packet_send (biggest, 0, NULL, false), 0, NULL);
	packet_delete (biggest);

	test_check_unsigned_eq (test_wait_counter (&echoes, 1, TEST_WAIT_SECONDS), 1, "the biggest packet was not handled");

	Packet *too_big = packet_generate_request (PACKET_TYPE_APP, PEERS_REQUEST_ECHO, data, data_size + 1);
	packet_set_network_values (too_big, NULL, client.client, client.connection, NULL);
	test_check_unsigned_eq (packet_send (too_big, 0, NULL, false), 1, "a packet bigger than a datagram was sent");
	packet_delete (too_big);

	free (data);

	peers_client_end (&client);

	peers_cerver_stop (&peers);

	test_check_unsigned_eq (app_packets, 1, NULL);

}

// datagrams without a valid packet are dropped without a peer
static void test_peers_invalid_datagrams (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);

	// shorter than a header, empty, and a header with another size
	const char short_datagram[PEERS_SHORT_DATAGRAM_SIZE] = { 0 };
	peers_socket_send_raw (&raw, short_datagram, sizeof (short_datagram));
	peers_socket_send_raw (&raw, short_datagram, 0);

	Packet *packet = packet_create_request (PACKET_TYPE_APP, PEERS_REQUEST_ECHO);
	packet->header.packet_size = sizeof (PacketHeader) + PEERS_WRONG_SIZE_OFFSET;
	peers_socket_send_raw (&raw, &packet->header, sizeof (PacketHeader));

	// a datagram with a bigger packet than the cerver takes
	packet->header.packet_size = MAX_UDP_PACKET_SIZE + 1;
	peers_socket_send_raw (&raw, &packet->header, sizeof (PacketHeader));
	packet_delete (packet);

	test_check (
		test_wait_counter_64 (&peers.cerver->stats->received_packets->n_bad_packets, PEERS_BAD_DATAGRAMS, TEST_WAIT_SECONDS)
			>= PEERS_BAD_DATAGRAMS,
		"the invalid datagrams were not dropped"
	);

	test_check_unsigned_eq (cerver_udp_peers_count (peers.cerver), 0, NULL);
	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_CONNECTED), 0, NULL);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// the invalid datagrams of a peer are bad packets, it is removed after the max
static void test_peers_bad_packets (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);

	peers_socket_send (&raw, PACKET_TYPE_APP, 0, NULL, 0, NULL);
	test_check_unsigned_eq (peers_wait_count (peers.cerver, 1), 1, NULL);

	const char short_datagram[PEERS_SHORT_DATAGRAM_SIZE] = { 0 };
	for (unsigned int i = 0; i < PEERS_MAX_BAD_PACKETS; i++) {
		peers_socket_send_raw (&raw, short_datagram, sizeof (short_datagram));
	}

	test_check_unsigned_eq (peers_wait_count (peers.cerver, 0), 0, "the peer was not removed");
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DROPPED], 1, TEST_WAIT_SECONDS), 1, NULL
	);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// a request (files) and a game packet are bad packets, the peer is kept before the max
static void test_peers_request (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);

	peers_socket_send (&raw, PACKET_TYPE_REQUEST, REQUEST_PACKET_TYPE_GET_FILE, NULL, 0, NULL);
	peers_socket_send (&raw, PACKET_TYPE_GAME, GAME_PACKET_TYPE_LOBBY_JOIN, NULL, 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter_64 (&peers.cerver->stats->received_packets->n_bad_packets, PEERS_REJECTED_PACKETS, TEST_WAIT_SECONDS),
		PEERS_REJECTED_PACKETS, NULL
	);

	test_check_unsigned_eq (cerver_udp_peers_count (peers.cerver), 1, NULL);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// a peer without datagrams is removed after its timeout
static void test_peers_timeout (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	Cerver *cerver = peers_cerver_create (port);
	cerver_set_udp_peer_timeout (cerver, PEERS_TIMEOUT_SECONDS);

	PeersCerver peers;
	peers_cerver_start (&peers, cerver);

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	peers_socket_send (&raw, PACKET_TYPE_APP, 0, NULL, 0, NULL);

	test_check_unsigned_eq (peers_wait_count (cerver, 1), 1, NULL);
	test_check_unsigned_eq (peers_wait_count (cerver, 0), 0, "the peer did not expire");
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DROPPED], 1, TEST_WAIT_SECONDS), 1, NULL
	);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// the cerver keeps up to its max peers, the datagrams of a new address are rejected
static void test_peers_max_peers (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	Cerver *cerver = peers_cerver_create (port);
	cerver_set_udp_max_peers (cerver, PEERS_MAX_PEERS);

	PeersCerver peers;
	peers_cerver_start (&peers, cerver);

	PeersSocket sockets[PEERS_MAX_PEERS + PEERS_EXTRA_PEERS];
	for (unsigned int i = 0; i < (PEERS_MAX_PEERS + PEERS_EXTRA_PEERS); i++) {
		sockets[i] = peers_socket_open (PEERS_IPV4_LOOPBACK, port);
		peers_socket_send (&sockets[i], PACKET_TYPE_APP, 0, NULL, 0, NULL);
	}

	test_check_unsigned_eq (
		test_wait_counter_64 (&cerver->stats->total_rejected_connections, PEERS_EXTRA_PEERS, TEST_WAIT_SECONDS),
		PEERS_EXTRA_PEERS, NULL
	);

	test_check_unsigned_eq (cerver_udp_peers_count (cerver), PEERS_MAX_PEERS, NULL);

	for (unsigned int i = 0; i < (PEERS_MAX_PEERS + PEERS_EXTRA_PEERS); i++) {
		peers_socket_close (&sockets[i]);
	}

	peers_cerver_stop (&peers);

}

// a disconnect packet removes the peer, and its client
static void test_peers_disconnect (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	peers_socket_send (&raw, PACKET_TYPE_APP, 0, NULL, 0, NULL);
	test_check_unsigned_eq (peers_wait_count (peers.cerver, 1), 1, NULL);

	peers_socket_send (&raw, PACKET_TYPE_CLIENT, CLIENT_PACKET_TYPE_DISCONNECT, NULL, 0, NULL);

	test_check_unsigned_eq (peers_wait_count (peers.cerver, 0), 0, "the disconnect did not remove the peer");
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DISCONNECTED], 1, TEST_WAIT_SECONDS), 1, NULL
	);

	test_check_unsigned_eq (peers_wait_stat (&peers.cerver->stats->current_n_connected_clients, 0), 0, NULL);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// the shutdown wakes up the receive without a datagram, it does not create a peer
static void test_peers_stop (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	peers_cerver_stop (&peers);

	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_CONNECTED), 0, NULL);
	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_TEARDOWN), 1, NULL);

}

// the peer expires while a handler with its own thread still has its packets,
// they keep it until they are released (ASan and TSan check it)
static void test_peers_queued_handler (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	Cerver *cerver = peers_cerver_create_base (port, PEERS_IPV4_LOOPBACK);
	cerver_set_udp_peer_timeout (cerver, PEERS_TIMEOUT_SECONDS);
	cerver_set_app_handlers (cerver, handler_create (peers_slow_handler), NULL);

	PeersCerver peers;
	peers_cerver_start (&peers, cerver);

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	for (unsigned int i = 0; i < PEERS_QUEUED_PACKETS; i++) {
		peers_socket_send (&raw, PACKET_TYPE_APP, 0, NULL, 0, NULL);
	}

	test_check_unsigned_eq (peers_wait_count (cerver, 1), 1, NULL);
	test_check_unsigned_eq (peers_wait_count (cerver, 0), 0, "the peer did not expire");

	test_check_unsigned_eq (
		test_wait_counter (&queued_packets, PEERS_QUEUED_PACKETS, TEST_WAIT_SECONDS),
		PEERS_QUEUED_PACKETS, NULL
	);

	test_check_unsigned_eq (peers_wait_stat (&cerver->stats->current_n_connected_clients, 0), 0, NULL);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// a client authenticates when it connects, and its datagrams have its token
static void test_peers_auth_success (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_auth_cerver_create (port, PEERS_AUTH_MAX_TRIES));

	PeersClient client = peers_client_connect (
		PEERS_IPV4_LOOPBACK, port, credentials_new (PEERS_USERNAME, PEERS_PASSWORD)
	);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_SUCCESS_AUTH], 1, TEST_WAIT_SECONDS),
		1, "the client did not authenticate"
	);

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;
	while (!connection_has_datagram_token (client.connection) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	test_check (connection_has_datagram_token (client.connection), "the client did not get its token");

	peers_client_send (&client, PACKET_TYPE_APP, PEERS_REQUEST_ECHO, PEERS_PACKETS);
	test_check_unsigned_eq (test_wait_counter (&echoes, PEERS_PACKETS, TEST_WAIT_SECONDS), PEERS_PACKETS, NULL);

	peers_client_end (&client);

	test_check_unsigned_eq (peers_wait_count (peers.cerver, 0), 0, "the close connection did not remove the peer");

	peers_cerver_stop (&peers);

	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_CONNECTED), 1, NULL);
	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_FAILED_AUTH), 0, NULL);

}

// wrong credentials fail, and the peer is removed after its last try
static void test_peers_auth_failed (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_auth_cerver_create (port, PEERS_AUTH_SINGLE_TRY));

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	peers_socket_send_credentials (&raw, PEERS_BAD_PASSWORD);

	char buffer[MAX_UDP_PACKET_SIZE];
	test_check (
		peers_socket_receive_packet (&raw, buffer, sizeof (buffer), PACKET_TYPE_ERROR, REQUEST_PACKET_TYPE_NONE) > 0,
		"the peer did not get the failed auth"
	);

	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_FAILED_AUTH), 1, NULL);
	test_check_unsigned_eq (peers_wait_count (peers.cerver, 0), 0, "the peer was not removed");
	test_check_unsigned_eq (CERVER_STATS_GET (peers.cerver->stats->current_n_hold_connections), 0, NULL);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_CONNECTED), 0, NULL);

}

// a pending peer is only created by auth, test and cerver packets,
// any other packet of it is a bad one, and it expires after the on hold timeout
static void test_peers_pending (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	Cerver *cerver = peers_auth_cerver_create (port, PEERS_AUTH_MAX_TRIES);
	cerver_set_on_hold_timeout (cerver, PEERS_ON_HOLD_SECONDS);

	PeersCerver peers;
	peers_cerver_start (&peers, cerver);

	PeersSocket raw =peers_socket_open (PEERS_IPV4_LOOPBACK, port);

	peers_socket_send (&raw, PACKET_TYPE_APP, PEERS_REQUEST_ECHO, NULL, 0, NULL);
	test_check_unsigned_eq (
		test_wait_counter_64 (&cerver->stats->total_rejected_connections, 1, TEST_WAIT_SECONDS), 1, NULL
	);

	test_check_unsigned_eq (cerver_udp_peers_count (cerver), 0, NULL);

	// the test packet comes back, it has the size of the one that was sent
	peers_socket_send (&raw, PACKET_TYPE_TEST, 0, NULL, 0, NULL);

	char buffer[MAX_UDP_PACKET_SIZE];
	test_check (
		peers_socket_receive_packet (&raw, buffer, sizeof (buffer), PACKET_TYPE_TEST, 0) > 0,
		"the pending peer did not get its test packet"
	);

	test_check_unsigned_eq (cerver_udp_peers_count (cerver), 1, NULL);

	// the first app packet was dropped without a peer, this one is a bad packet of the peer
	peers_socket_send (&raw, PACKET_TYPE_APP, PEERS_REQUEST_ECHO, NULL, 0, NULL);
	test_check_unsigned_eq (
		test_wait_counter_64 (&cerver->stats->received_packets->n_bad_packets, PEERS_PENDING_BAD_PACKETS, TEST_WAIT_SECONDS),
		PEERS_PENDING_BAD_PACKETS, NULL
	);

	test_check_unsigned_eq (peers_wait_count (cerver, 0), 0, "the pending peer did not expire");

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

	test_check_unsigned_eq (app_packets, 0, NULL);

}

// every datagram of an authenticated peer has its token, a wrong or missing one is a bad packet,
// and an auth packet without it gets the same success again
static void test_peers_token (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_auth_cerver_create (port, PEERS_AUTH_MAX_TRIES));

	PeersSocket raw =peers_socket_authenticate (port, false);

	test_check (peers_socket_echo (&raw, raw.token), "the token was not accepted");

	u8 wrong[CONNECTION_DATAGRAM_TOKEN_SIZE];
	(void) memcpy (wrong, raw.token, sizeof (wrong));
	wrong[0] ^= 1;

	test_check (!peers_socket_echo (&raw, wrong), "a wrong token was accepted");
	test_check (!peers_socket_echo (&raw, NULL), "a datagram without a token was accepted");

	u8 token[CONNECTION_DATAGRAM_TOKEN_SIZE];
	(void) memcpy (token, raw.token, sizeof (token));

	peers_socket_send_credentials (&raw, PEERS_PASSWORD);
	peers_socket_read_success (&raw, false);
	test_check (!memcmp (token, raw.token, sizeof (token)), "the success has another token");

	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_SUCCESS_AUTH), 1, NULL);
	test_check_unsigned_eq (__atomic_load_n (&app_packets, __ATOMIC_SEQ_CST), 1, NULL);

	peers_socket_close (&raw);

	peers_cerver_stop (&peers);

}

// a datagram with the token of a peer from another address moves the peer to it
static void test_peers_migration (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_auth_cerver_create (port, PEERS_AUTH_MAX_TRIES));

	PeersSocket first = peers_socket_authenticate (port, false);
	test_check (peers_socket_echo (&first, first.token), NULL);

	PeersSocket moved = peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	test_check (peers_socket_echo (&moved, first.token), "the peer did not move");

	test_check_unsigned_eq (cerver_udp_peers_count (peers.cerver), 1, NULL);

	// the old address does not have a peer, its datagrams are dropped
	test_check (!peers_socket_echo (&first, NULL), NULL);

	test_check_unsigned_eq (peers_event_count (CERVER_EVENT_CLIENT_CONNECTED), 1, NULL);

	peers_socket_close (&first);
	peers_socket_close (&moved);

	peers_cerver_stop (&peers);

}

// a session token joins the client of the session from another address,
// the client is released with its last peer
static void test_peers_sessions (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	Cerver *cerver = peers_auth_cerver_create (port, PEERS_AUTH_MAX_TRIES);
	test_check_unsigned_eq (cerver_set_sessions (cerver, session_default_generate_id), 0, NULL);

	PeersCerver peers;
	peers_cerver_start (&peers, cerver);

	PeersSocket first = peers_socket_authenticate (port, true);
	test_check (first.session.token[0] != '\0', "the success does not have the session token");

	PeersSocket second = peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	peers_socket_send (&second, PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_CLIENT_AUTH, &first.session, sizeof (SToken), NULL);
	peers_socket_read_success (&second, true);

	// the success is sent before the events are triggered
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_NEW_CONNECTION], 1, TEST_WAIT_SECONDS), 1, NULL
	);

	test_check_unsigned_eq (cerver_udp_peers_count (cerver), 2, NULL);
	test_check_unsigned_eq (CERVER_STATS_GET (cerver->stats->current_n_connected_clients), 1, NULL);

	test_check (peers_socket_echo (&second, second.token), NULL);

	// an auth packet much smaller than the success (with the session token) can not ask for it again,
	// its address could be forged
	char buffer[MAX_UDP_PACKET_SIZE];
	peers_socket_send (&second, PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_CLIENT_AUTH, NULL, 0, NULL);
	test_check (
		peers_socket_receive_packet (&second, buffer, sizeof (buffer), PACKET_TYPE_AUTH, AUTH_PACKET_TYPE_SUCCESS) < 0,
		"a small auth packet got the success"
	);

	peers_socket_send (&first, PACKET_TYPE_CLIENT, CLIENT_PACKET_TYPE_CLOSE_CONNECTION, NULL, 0, first.token);
	test_check_unsigned_eq (peers_wait_count (cerver, 1), 1, NULL);
	test_check_unsigned_eq (CERVER_STATS_GET (cerver->stats->current_n_connected_clients), 1, NULL);

	peers_socket_send (&second, PACKET_TYPE_CLIENT, CLIENT_PACKET_TYPE_CLOSE_CONNECTION, NULL, 0, second.token);
	test_check_unsigned_eq (peers_wait_count (cerver, 0), 0, NULL);
	test_check_unsigned_eq (peers_wait_stat (&cerver->stats->current_n_connected_clients, 0), 0, NULL);

	peers_socket_close (&first);
	peers_socket_close (&second);

	peers_cerver_stop (&peers);

}

// an unproven address only gets the cerver info when it sent enough bytes for it
static void test_peers_amplification (void) {

	peers_counters_reset ();

	const u16 port = peers_port++;
	PeersCerver peers;
	peers_cerver_start (&peers, peers_cerver_create (port));

	char buffer[MAX_UDP_PACKET_SIZE];

	PeersSocket small = peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	peers_socket_send (&small, PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO, NULL, 0, NULL);
	test_check (
		peers_socket_receive_packet (&small, buffer, sizeof (buffer), PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO) < 0,
		"a small request got the cerver info"
	);

	const SCerver padding = { 0 };
	PeersSocket padded = peers_socket_open (PEERS_IPV4_LOOPBACK, port);
	peers_socket_send (&padded, PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO, &padding, sizeof (SCerver), NULL);
	test_check (
		peers_socket_receive_packet (&padded, buffer, sizeof (buffer), PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO) > 0,
		"the padded request did not get the cerver info"
	);

	peers_socket_close (&small);
	peers_socket_close (&padded);

	peers_cerver_stop (&peers);

}

#pragma endregion

int main (int argc, char **argv) {

	test_output_init ();

	(void) argc;
	(void) argv;

	cerver_init ();

	(void) printf ("Testing CERVER UDP peers...\n");

	test_peers_echo ();
	test_peers_biggest_packet ();
	test_peers_invalid_datagrams ();
	test_peers_bad_packets ();
	test_peers_request ();
	test_peers_timeout ();
	test_peers_max_peers ();
	test_peers_disconnect ();
	test_peers_stop ();
	test_peers_queued_handler ();
	test_peers_auth_success ();
	test_peers_auth_failed ();
	test_peers_pending ();
	test_peers_token ();
	test_peers_migration ();
	test_peers_sessions ();
	test_peers_amplification ();

	(void) printf ("\nDone with CERVER UDP peers tests!\n\n");

	cerver_end ();

	return 0;

}
