#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/connection/connection.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/events.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/game/game.h>
#include <cerver/game/lobby.h>
#include <cerver/game/player.h>

#include "../test.h"

// a game cerver in the same process with two connected clients:
// - a lobby is created with the first client as its owner, and the second one joins it
// - a broadcast reaches every player of the lobby
// - the owner leaves and the other player is the new owner
// - the last player leaves and the lobby is destroyed
// - the cerver is stopped with its clients connected

#define LOBBY_ADDRESS					"127.0.0.1"
#define LOBBY_PORT						7600
#define LOBBY_NAME						"lobby-cerver"

#define LOBBY_THREADS					4
#define LOBBY_POLL_TIMEOUT				200
#define LOBBY_JOIN_SECONDS				8

#define LOBBY_CLIENTS					2
#define LOBBY_MAX_PLAYERS				2
#define LOBBY_BROADCASTS				8
#define LOBBY_REQUEST_BROADCAST			1

static unsigned int cerver_started = 0;
static unsigned int connected_clients = 0;
static unsigned int broadcasts = 0;

// the clients of the cerver, in the order they connected
static Client *cerver_clients[LOBBY_CLIENTS] = { NULL };

static pthread_t cerver_thread = 0;
static u8 cerver_start_result = 0;

#pragma region cerver

static void *lobby_cerver_started (void *event_data_ptr) {

	cerver_event_data_delete ((CerverEventData *) event_data_ptr);

	test_counter_add (&cerver_started);

	return NULL;

}

// the event runs before the handler owns the connection, in the accept thread
static void *lobby_cerver_client_connected (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	const unsigned int idx = __atomic_load_n (&connected_clients, __ATOMIC_SEQ_CST);
	if (idx < LOBBY_CLIENTS) cerver_clients[idx] = (Client *) event_data->client;

	cerver_event_data_delete (event_data);

	test_counter_add (&connected_clients);

	return NULL;

}

static void *lobby_cerver_thread (void *cerver_ptr) {

	cerver_start_result = cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static Cerver *lobby_cerver_start (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_GAME, LOBBY_NAME,
		LOBBY_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_ptr (cerver->cerver_data);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, LOBBY_THREADS);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (cerver, LOBBY_POLL_TIMEOUT);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_STARTED,
		lobby_cerver_started, NULL, NULL,
		false, false
	);

	(void) cerver_event_register (
		cerver, CERVER_EVENT_CLIENT_CONNECTED,
		lobby_cerver_client_connected, NULL, NULL,
		false, false
	);

	test_check_int_eq (pthread_create (&cerver_thread, NULL, lobby_cerver_thread, cerver), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&cerver_started, 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

	return cerver;

}

static void lobby_cerver_stop (Cerver *cerver) {

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += LOBBY_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (cerver_thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (cerver_start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

#pragma region client

static void lobby_client_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == LOBBY_REQUEST_BROADCAST) {
		test_counter_add (&broadcasts);
	}

}

static Client *lobby_client_connect (void) {

	Client *client = client_create ();
	test_check_ptr (client);

	Handler *app_handler = handler_create (lobby_client_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (client, app_handler, NULL);

	Connection *connection = client_connection_create (
		client, LOBBY_ADDRESS, LOBBY_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (connection);

	test_check_int_eq (client_connect_and_start (client, connection), 0, NULL);

	return client;

}

#pragma endregion

#pragma region tests

static void lobby_broadcast (Cerver *cerver, const Lobby *lobby) {

	for (unsigned int i = 0; i < LOBBY_BROADCASTS; i++) {
		Packet *packet = packet_generate_request (PACKET_TYPE_APP, LOBBY_REQUEST_BROADCAST, NULL, 0);
		test_check_ptr (packet);

		player_broadcast_to_all (cerver, lobby, packet, PROTOCOL_TCP, 0);

		packet_delete (packet);
	}

}

static Player *lobby_player_of (const Lobby *lobby, const Client *client) {

	Player *found = NULL;

	for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
		if (((Player *) le->data)->client == client) {
			found = (Player *) le->data;
			break;
		}
	}

	return found;

}

static void test_lobby_players (Cerver *cerver) {

	Lobby *lobby = lobby_create (cerver, cerver_clients[0], false, NULL, LOBBY_MAX_PLAYERS);
	test_check_ptr (lobby);
	test_check_ptr (lobby->id);
	test_check_ptr (lobby->owner);
	test_check_ptr_eq (lobby->owner->client, cerver_clients[0]);
	test_check_unsigned_eq (lobby->n_current_players, 1, NULL);

	test_check_unsigned_eq (lobby_join (cerver, cerver_clients[1], lobby), 0, NULL);
	test_check_unsigned_eq (lobby->n_current_players, 2, NULL);

	// every player gets every broadcast
	lobby_broadcast (cerver, lobby);
	test_check_unsigned_eq (
		test_wait_counter (&broadcasts, LOBBY_CLIENTS * LOBBY_BROADCASTS, TEST_WAIT_SECONDS),
		LOBBY_CLIENTS * LOBBY_BROADCASTS, "the broadcasts did not arrive"
	);

	// the owner leaves, the other player is the new owner
	Player *owner = lobby->owner;
	test_check_unsigned_eq (lobby_leave (cerver, lobby, owner), 0, NULL);
	test_check_unsigned_eq (lobby->n_current_players, 1, NULL);
	test_check_ptr_eq (lobby->owner, lobby_player_of (lobby, cerver_clients[1]));

	// only the player that is left gets the broadcasts
	lobby_broadcast (cerver, lobby);
	test_check_unsigned_eq (
		test_wait_counter (&broadcasts, (LOBBY_CLIENTS + 1) * LOBBY_BROADCASTS, TEST_WAIT_SECONDS),
		(LOBBY_CLIENTS + 1) * LOBBY_BROADCASTS, NULL
	);

	// the last player destroys the lobby
	test_check_unsigned_eq (lobby_leave (cerver, lobby, lobby->owner), 0, NULL);

}

#pragma endregion

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	test_output_init ();

	(void) printf ("Testing CERVER lobby...\n");

	Cerver *cerver = lobby_cerver_start ();

	Client *clients[LOBBY_CLIENTS] = { NULL };
	for (unsigned int i = 0; i < LOBBY_CLIENTS; i++) {
		clients[i] = lobby_client_connect ();

		test_check_unsigned_eq (
			test_wait_counter (&connected_clients, i + 1, TEST_WAIT_SECONDS),
			i + 1, "the client did not connect"
		);
	}

	test_lobby_players (cerver);

	// the clients see the remote close
	lobby_cerver_stop (cerver);

	for (unsigned int i = 0; i < LOBBY_CLIENTS; i++) {
		test_check_unsigned_eq (client_teardown (clients[i]), 0, NULL);
	}

	(void) printf ("Done with CERVER lobby tests!\n\n");

	return 0;

}
