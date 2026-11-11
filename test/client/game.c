#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <pthread.h>

#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include <cerver/game/game.h>
#include <cerver/game/lobby.h>

#include "../test.h"

// the clients of the cerver/game pair, two players in another process:
// - the first one searches a lobby and gets a new one, the second one finds it
// - the owner starts the game and both players see it
// - the lobby sends back the app packets of each player
// - the second player leaves and the owner gets the update, then the owner destroys the lobby

#define GAME_CLIENT_PORT				7000
#define GAME_CLIENT_TYPE				"arcade"
#define GAME_CLIENT_MAX_SLEEP			30

#define GAME_CLIENTS					2
#define GAME_FIRST						0
#define GAME_SECOND						1

#define GAME_CLIENT_PACKETS				32

#define GAME_CLIENT_EVENTS				(CLIENT_EVENT_UNKNOWN + 1)
#define GAME_CLIENT_ARGS_SLOTS			64

typedef struct GameClient {

	Client *client;
	Connection *connection;

} GameClient;

static unsigned int events[GAME_CLIENTS][GAME_CLIENT_EVENTS] = { { 0 } };
static unsigned int echoes[GAME_CLIENTS] = { 0 };

static pthread_mutex_t summaries_mutex = PTHREAD_MUTEX_INITIALIZER;
static LobbySummary summaries[GAME_CLIENTS];

static void *game_client_event (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	const unsigned int args = (unsigned int) (uintptr_t) event_data->action_args;
	const unsigned int index = args / GAME_CLIENT_ARGS_SLOTS;
	const unsigned int type = args % GAME_CLIENT_ARGS_SLOTS;

	test_check_ptr (event_data->response_data);

	(void) pthread_mutex_lock (&summaries_mutex);
	(void) memcpy (&summaries[index], event_data->response_data, sizeof (LobbySummary));
	(void) pthread_mutex_unlock (&summaries_mutex);

	client_event_data_delete (event_data);

	test_counter_add (&events[index][type]);

	return NULL;

}

static void game_client_first_echo (void *packet_ptr) {

	(void) packet_ptr;

	test_counter_add (&echoes[GAME_FIRST]);

}

static void game_client_second_echo (void *packet_ptr) {

	(void) packet_ptr;

	test_counter_add (&echoes[GAME_SECOND]);

}

static GameClient game_client_connect (const unsigned int index, Action echo) {

	GameClient game = { NULL, NULL };

	game.client = client_create ();
	test_check_ptr (game.client);

	const ClientEventType types[] = {
		CLIENT_EVENT_LOBBY_CREATE, CLIENT_EVENT_LOBBY_JOIN, CLIENT_EVENT_LOBBY_LEAVE,
		CLIENT_EVENT_LOBBY_START, CLIENT_EVENT_LOBBY_UPDATE, CLIENT_EVENT_LOBBY_DESTROY
	};

	for (unsigned int i = 0; i < (sizeof (types) / sizeof (ClientEventType)); i++) {
		test_check_unsigned_eq (
			client_event_register (
				game.client, types[i],
				game_client_event, (void *) (uintptr_t) ((index * GAME_CLIENT_ARGS_SLOTS) + types[i]), NULL,
				false, false
			), 0, NULL
		);
	}

	Handler *app_handler = handler_create (echo);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (game.client, app_handler, NULL);

	game.connection = client_connection_create (
		game.client, test_address (), GAME_CLIENT_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (game.connection);
	connection_set_max_sleep (game.connection, GAME_CLIENT_MAX_SLEEP);

	test_check_int_eq (client_connect_and_start (game.client, game.connection), 0, "Failed to connect to cerver!");

	return game;

}

static void game_client_search (const GameClient *game) {

	LobbyJoin request;
	serializer_object_init (&lobby_join_request_schema, &request);
	(void) snprintf (request.game_type, LOBBY_GAME_TYPE_SIZE, "%s", GAME_CLIENT_TYPE);

	test_check_unsigned_eq (
		game_request_send (game->client, game->connection, GAME_PACKET_TYPE_LOBBY_JOIN, &lobby_join_request_schema, &request),
		0, NULL
	);

}

static void game_client_request (const GameClient *game, const u32 request_type, const char *lobby_id) {

	LobbyRequest request;
	serializer_object_init (&lobby_request_schema, &request);
	(void) snprintf (request.lobby_id, LOBBY_ID_SIZE, "%s", lobby_id);

	test_check_unsigned_eq (
		game_request_send (game->client, game->connection, request_type, &lobby_request_schema, &request),
		0, NULL
	);

}

static void game_client_wait (
	const unsigned int index, const ClientEventType type, LobbySummary *summary
) {

	test_check_unsigned_eq (
		test_wait_counter (&events[index][type], 1, TEST_WAIT_SECONDS),
		1, "the client did not get the lobby event"
	);

	if (summary) {
		(void) pthread_mutex_lock (&summaries_mutex);
		(void) memcpy (summary, &summaries[index], sizeof (LobbySummary));
		(void) pthread_mutex_unlock (&summaries_mutex);
	}

}

static void game_client_send (const GameClient *game) {

	for (unsigned int i = 0; i < GAME_CLIENT_PACKETS; i++) {
		Packet *packet = packet_create_request (PACKET_TYPE_APP, i);
		test_check_ptr (packet);

		packet_set_network_values (packet, NULL, game->client, game->connection, NULL);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

		packet_delete (packet);
	}

}

static void game_client_end (GameClient *game) {

	(void) client_connection_end (game->client, game->connection);
	test_check_unsigned_eq (client_teardown (game->client), 0, NULL);

}

int main (void) {

	test_output_init ();

	(void) printf ("Testing CLIENT game...\n");

	GameClient first = game_client_connect (GAME_FIRST, game_client_first_echo);
	GameClient second = game_client_connect (GAME_SECOND, game_client_second_echo);

	LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
	test_check_ptr (summary);

	// the first search creates the lobby, the second one joins it
	game_client_search (&first);
	game_client_wait (GAME_FIRST, CLIENT_EVENT_LOBBY_CREATE, summary);
	test_check_bool_eq (summary->created, true, NULL);

	char lobby_id[LOBBY_ID_SIZE] = { 0 };
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);

	game_client_search (&second);
	game_client_wait (GAME_SECOND, CLIENT_EVENT_LOBBY_JOIN, summary);
	test_check_str_eq (summary->id, lobby_id, NULL);
	test_check_unsigned_eq (summary->n_players, GAME_CLIENTS, NULL);

	// the owner starts the game and both players see it
	game_client_request (&first, GAME_PACKET_TYPE_GAME_START, lobby_id);
	game_client_wait (GAME_FIRST, CLIENT_EVENT_LOBBY_START, summary);
	test_check_bool_eq (summary->in_game, true, NULL);
	game_client_wait (GAME_SECOND, CLIENT_EVENT_LOBBY_START, NULL);

	// the lobby sends back the packets of each player
	game_client_send (&first);
	game_client_send (&second);

	test_check_unsigned_eq (test_wait_counter (&echoes[GAME_FIRST], GAME_CLIENT_PACKETS, TEST_WAIT_SECONDS), GAME_CLIENT_PACKETS, NULL);
	test_check_unsigned_eq (test_wait_counter (&echoes[GAME_SECOND], GAME_CLIENT_PACKETS, TEST_WAIT_SECONDS), GAME_CLIENT_PACKETS, NULL);

	// the second player leaves and the owner gets the update
	game_client_request (&second, GAME_PACKET_TYPE_LOBBY_LEAVE, lobby_id);
	game_client_wait (GAME_SECOND, CLIENT_EVENT_LOBBY_LEAVE, NULL);
	game_client_wait (GAME_FIRST, CLIENT_EVENT_LOBBY_UPDATE, summary);
	test_check_unsigned_eq (summary->n_players, 1, NULL);

	// the owner destroys the lobby
	game_client_request (&first, GAME_PACKET_TYPE_LOBBY_DESTROY, lobby_id);
	game_client_wait (GAME_FIRST, CLIENT_EVENT_LOBBY_DESTROY, NULL);

	game_client_end (&first);
	game_client_end (&second);

	free (summary);

	(void) printf ("Done!\n\n");

	return 0;

}
