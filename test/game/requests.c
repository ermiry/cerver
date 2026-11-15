#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/client/events.h>
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
#include <cerver/game/types.h>

#include "../test.h"
#include "game.h"

// a game cerver in the same process with cerver clients that send game requests,
// with the poll and the threads handler types:
// - create a lobby: with a bad game type, twice from the same client, and the client gets its summary
// - join a lobby by its id, a full one, and without an id: it creates a lobby when none has a place
// - only the owner inits, starts and destroys the game, and a game is not started twice
// - the app packets of the players reach the packet handler of the lobby while it runs,
//   which in poll means that the lobby poll receives them
// - a player that disconnects leaves its lobby, the others get the update, and an owner that leaves gives it
// - a player that stops sending leaves the lobby after the player timeout
// - an unknown request is answered with an error, and a cerver that is not a game cerver answers every one
// - the cerver stops with a running lobby

#define REQUESTS_ADDRESS				"127.0.0.1"
#define REQUESTS_PORT_BASE				7720
#define REQUESTS_NAME					"requests-game"
#define REQUESTS_GAME_TYPE				"arcade"
#define REQUESTS_BAD_GAME_TYPE			"missing"

#define REQUESTS_THREADS				4
#define REQUESTS_POLL_TIMEOUT			200
#define REQUESTS_JOIN_SECONDS			8

#define REQUESTS_CLIENTS				3
#define REQUESTS_FIRST					0
#define REQUESTS_SECOND					1
#define REQUESTS_THIRD					2

#define REQUESTS_MAX_PLAYERS			2
#define REQUESTS_APP_PACKETS			8
#define REQUESTS_UNKNOWN_REQUEST		99

#define REQUESTS_PLAYER_TIMEOUT			1
#define REQUESTS_FAST_FPS				32
#define REQUESTS_KEEP_ALIVE_NANOSECONDS	100000000L
#define REQUESTS_KEEP_ALIVE_ROUNDS		48

#define REQUESTS_CLIENT_EVENTS			(CLIENT_EVENT_UNKNOWN + 1)
#define REQUESTS_CLIENT_ERRORS			(CLIENT_ERROR_UNKNOWN + 1)
#define REQUESTS_CERVER_EVENTS			(CERVER_EVENT_UNKNOWN + 1)

// the action args of the client events and errors are the index of the client and the type
#define REQUESTS_ARGS_SLOTS				64

typedef struct RequestsCerver {

	Cerver *cerver;
	pthread_t thread;
	u8 start_result;

} RequestsCerver;

typedef struct RequestsClient {

	Client *client;
	Connection *connection;

} RequestsClient;

static u16 requests_port = REQUESTS_PORT_BASE;

static unsigned int cerver_started = 0;
static unsigned int cerver_events[REQUESTS_CERVER_EVENTS] = { 0 };

static unsigned int init_callbacks = 0;
static unsigned int start_callbacks = 0;
static unsigned int end_callbacks = 0;
static unsigned int join_callbacks = 0;
static unsigned int leave_callbacks = 0;
static unsigned int update_calls = 0;

static unsigned int lobby_app_packets = 0;
static unsigned int cerver_app_packets = 0;

static unsigned int client_events[REQUESTS_CLIENTS][REQUESTS_CLIENT_EVENTS] = { { 0 } };
static unsigned int client_errors[REQUESTS_CLIENTS][REQUESTS_CLIENT_ERRORS] = { { 0 } };

static pthread_mutex_t summaries_mutex = PTHREAD_MUTEX_INITIALIZER;
static LobbySummary summaries[REQUESTS_CLIENTS];

// the counters are written by the threads of the cerver and of the clients
static unsigned int requests_read (const unsigned int *counter) {

	return __atomic_load_n (counter, __ATOMIC_SEQ_CST);

}

static void requests_counters_reset (void) {

	cerver_started = 0;
	(void) memset (cerver_events, 0, sizeof (cerver_events));

	init_callbacks = 0;
	start_callbacks = 0;
	end_callbacks = 0;
	join_callbacks = 0;
	leave_callbacks = 0;
	update_calls = 0;

	lobby_app_packets = 0;
	cerver_app_packets = 0;

	(void) memset (client_events, 0, sizeof (client_events));
	(void) memset (client_errors, 0, sizeof (client_errors));
	(void) memset (summaries, 0, sizeof (summaries));

}

#pragma region cerver

static void *requests_cerver_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;
	const unsigned int type = (unsigned int) (uintptr_t) event_data->action_args;

	cerver_event_data_delete (event_data);

	test_counter_add (&cerver_events[type]);

	return NULL;

}

static void requests_cerver_events_register (Cerver *cerver) {

	const CerverEventType types[] = {
		CERVER_EVENT_STARTED,
		CERVER_EVENT_LOBBY_CREATE, CERVER_EVENT_LOBBY_JOIN, CERVER_EVENT_LOBBY_LEAVE,
		CERVER_EVENT_LOBBY_START, CERVER_EVENT_LOBBY_DESTROY
	};

	for (unsigned int i = 0; i < (sizeof (types) / sizeof (CerverEventType)); i++) {
		test_check_unsigned_eq (
			cerver_event_register (
				cerver, types[i],
				requests_cerver_event, (void *) (uintptr_t) types[i], NULL,
				false, false
			), 0, NULL
		);
	}

}

static void requests_cerver_app_handler (void *packet_ptr) {

	(void) packet_ptr;

	test_counter_add (&cerver_app_packets);

}

static void requests_lobby_packet_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;
	test_check_ptr (packet->lobby);

	test_counter_add (&lobby_app_packets);

}

static void requests_lobby_update (void *cerver_lobby_ptr) {

	const CerverLobby *cerver_lobby = (const CerverLobby *) cerver_lobby_ptr;
	test_check_ptr (cerver_lobby->lobby);

	test_counter_add (&update_calls);

}

static void requests_game_init (void *lobby_player_ptr) {

	const LobbyPlayer *lobby_player = (const LobbyPlayer *) lobby_player_ptr;
	test_check_ptr_eq (lobby_player->player, lobby_player->lobby->owner);

	test_counter_add (&init_callbacks);

}

static void *requests_game_start (void *lobby_ptr) {

	Lobby *lobby = (Lobby *) lobby_ptr;

	lobby_set_packet_handler (lobby, requests_lobby_packet_handler);
	lobby_set_update (lobby, requests_lobby_update);

	test_counter_add (&start_callbacks);

	return NULL;

}

static void *requests_game_end (void *lobby_ptr) {

	(void) lobby_ptr;

	test_counter_add (&end_callbacks);

	return NULL;

}

static void requests_game_join (void *lobby_player_ptr) {

	(void) lobby_player_ptr;

	test_counter_add (&join_callbacks);

}

static void requests_game_leave (void *lobby_player_ptr) {

	(void) lobby_player_ptr;

	test_counter_add (&leave_callbacks);

}

static void requests_game_type_register (Cerver *cerver, const u32 player_timeout) {

	GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

	GameType *game_type = game_type_create (
		REQUESTS_GAME_TYPE, NULL, NULL,
		requests_game_start, requests_game_end
	);

	test_check_ptr (game_type);

	(void) game_type_add_lobby_config (game_type, true, NULL, REQUESTS_MAX_PLAYERS);
	game_type_set_init (game_type, requests_game_init);
	game_type_set_on_lobby_join (game_type, requests_game_join);
	game_type_set_on_lobby_leave (game_type, requests_game_leave);
	game_type_set_player_timeout (game_type, player_timeout);
	game_type_set_fps (game_type, REQUESTS_FAST_FPS);

	test_check_int_eq (game_type_register (game_cerver->game_types, game_type), 0, NULL);

}

static void *requests_cerver_thread (void *requests_ptr) {

	RequestsCerver *requests = (RequestsCerver *) requests_ptr;

	requests->start_result = cerver_start (requests->cerver);

	return NULL;

}

static void requests_cerver_start (
	RequestsCerver *requests,
	const CerverType type, const CerverHandlerType handler_type,
	const u16 port, const u32 player_timeout
) {

	requests->cerver = cerver_create (
		type, REQUESTS_NAME,
		port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (requests->cerver);

	cerver_set_reusable_address_flags (requests->cerver, true);
	cerver_set_thpool_n_threads (requests->cerver, REQUESTS_THREADS);
	cerver_set_handler_type (requests->cerver, handler_type);
	cerver_set_poll_time_out (requests->cerver, REQUESTS_POLL_TIMEOUT);

	Handler *app_handler = handler_create (requests_cerver_app_handler);
	handler_set_direct_handle (app_handler, true);
	cerver_set_app_handlers (requests->cerver, app_handler, NULL);

	requests_cerver_events_register (requests->cerver);

	if (type == CERVER_TYPE_GAME) requests_game_type_register (requests->cerver, player_timeout);

	requests->start_result = 0;
	test_check_int_eq (pthread_create (&requests->thread, NULL, requests_cerver_thread, requests), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&cerver_events[CERVER_EVENT_STARTED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

}

static void requests_cerver_stop (RequestsCerver *requests) {

	test_check_unsigned_eq (cerver_shutdown (requests->cerver), 0, NULL);

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += REQUESTS_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (requests->thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (requests->start_result, 0, NULL);

	test_check_unsigned_eq (cerver_teardown (requests->cerver), 0, NULL);

}

#pragma endregion

#pragma region client

static void *requests_client_event (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	const unsigned int args = (unsigned int) (uintptr_t) event_data->action_args;
	const unsigned int index = args / REQUESTS_ARGS_SLOTS;
	const unsigned int type = args % REQUESTS_ARGS_SLOTS;

	if (event_data->response_data) {
		(void) pthread_mutex_lock (&summaries_mutex);
		(void) memcpy (&summaries[index], event_data->response_data, sizeof (LobbySummary));
		(void) pthread_mutex_unlock (&summaries_mutex);
	}

	client_event_data_delete (event_data);

	test_counter_add (&client_events[index][type]);

	return NULL;

}

static void *requests_client_error (void *error_data_ptr) {

	ClientErrorData *error_data = (ClientErrorData *) error_data_ptr;

	const unsigned int args = (unsigned int) (uintptr_t) error_data->action_args;
	const unsigned int index = args / REQUESTS_ARGS_SLOTS;
	const unsigned int type = args % REQUESTS_ARGS_SLOTS;

	client_error_data_delete (error_data);

	test_counter_add (&client_errors[index][type]);

	return NULL;

}

static void requests_client_register (Client *client, const unsigned int index) {

	const ClientEventType events[] = {
		CLIENT_EVENT_LOBBY_CREATE, CLIENT_EVENT_LOBBY_JOIN, CLIENT_EVENT_LOBBY_LEAVE,
		CLIENT_EVENT_LOBBY_START, CLIENT_EVENT_LOBBY_UPDATE, CLIENT_EVENT_LOBBY_DESTROY,
		CLIENT_EVENT_GAME_INIT
	};

	for (unsigned int i = 0; i < (sizeof (events) / sizeof (ClientEventType)); i++) {
		test_check_unsigned_eq (
			client_event_register (
				client, events[i],
				requests_client_event, (void *) (uintptr_t) ((index * REQUESTS_ARGS_SLOTS) + events[i]), NULL,
				false, false
			), 0, NULL
		);
	}

	const ClientErrorType errors[] = {
		CLIENT_ERROR_PACKET_ERROR,
		CLIENT_ERROR_CREATE_LOBBY, CLIENT_ERROR_JOIN_LOBBY, CLIENT_ERROR_LEAVE_LOBBY,
		CLIENT_ERROR_FIND_LOBBY, CLIENT_ERROR_GAME_INIT, CLIENT_ERROR_GAME_START,
		CLIENT_ERROR_DESTROY_LOBBY
	};

	for (unsigned int i = 0; i < (sizeof (errors) / sizeof (ClientErrorType)); i++) {
		test_check_unsigned_eq (
			client_error_register (
				client, errors[i],
				requests_client_error, (void *) (uintptr_t) ((index * REQUESTS_ARGS_SLOTS) + errors[i]), NULL,
				false, false
			), 0, NULL
		);
	}

}

static RequestsClient requests_client_connect (const u16 port, const unsigned int index) {

	RequestsClient requests = { NULL, NULL };

	requests.client = client_create ();
	test_check_ptr (requests.client);

	requests_client_register (requests.client, index);

	requests.connection = client_connection_create (
		requests.client, REQUESTS_ADDRESS, port, PROTOCOL_TCP, false
	);

	test_check_ptr (requests.connection);

	test_check_int_eq (client_connect_and_start (requests.client, requests.connection), 0, NULL);

	return requests;

}

static void requests_client_end (RequestsClient *requests) {

	if (client_is_running (requests->client)) {
		(void) client_connection_end (requests->client, requests->connection);
	}

	test_check_unsigned_eq (client_teardown (requests->client), 0, NULL);

}

static void requests_create (const RequestsClient *requests, const char *game_type) {

	LobbyCreate request;
	serializer_object_init (&lobby_create_request_schema, &request);
	(void) snprintf (request.game_type, LOBBY_GAME_TYPE_SIZE, "%s", game_type);

	test_check_unsigned_eq (
		game_request_send (
			requests->client, requests->connection,
			GAME_PACKET_TYPE_LOBBY_CREATE, &lobby_create_request_schema, &request
		), 0, NULL
	);

}

static void requests_join (const RequestsClient *requests, const char *lobby_id, const char *game_type) {

	LobbyJoin request;
	serializer_object_init (&lobby_join_request_schema, &request);
	if (lobby_id) (void) snprintf (request.lobby_id, LOBBY_ID_SIZE, "%s", lobby_id);
	(void) snprintf (request.game_type, LOBBY_GAME_TYPE_SIZE, "%s", game_type);

	test_check_unsigned_eq (
		game_request_send (
			requests->client, requests->connection,
			GAME_PACKET_TYPE_LOBBY_JOIN, &lobby_join_request_schema, &request
		), 0, NULL
	);

}

static void requests_lobby (const RequestsClient *requests, const u32 request_type, const char *lobby_id) {

	LobbyRequest request;
	serializer_object_init (&lobby_request_schema, &request);
	(void) snprintf (request.lobby_id, LOBBY_ID_SIZE, "%s", lobby_id);

	test_check_unsigned_eq (
		game_request_send (
			requests->client, requests->connection,
			request_type, &lobby_request_schema, &request
		), 0, NULL
	);

}

static void requests_app_send (const RequestsClient *requests, const unsigned int count) {

	for (unsigned int i = 0; i < count; i++) {
		Packet *packet = packet_create_request (PACKET_TYPE_APP, 0);
		test_check_ptr (packet);

		packet_set_network_values (packet, NULL, requests->client, requests->connection, NULL);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

		packet_delete (packet);
	}

}

// waits for the event of the client, and copies the summary that came with it
static void requests_wait_event (
	const unsigned int index, const ClientEventType type,
	const unsigned int expected, LobbySummary *summary
) {

	test_check_unsigned_eq (
		test_wait_counter (&client_events[index][type], expected, TEST_WAIT_SECONDS),
		expected, "the client did not get the lobby event"
	);

	if (summary) {
		(void) pthread_mutex_lock (&summaries_mutex);
		(void) memcpy (summary, &summaries[index], sizeof (LobbySummary));
		(void) pthread_mutex_unlock (&summaries_mutex);
	}

}

static void requests_wait_error (
	const unsigned int index, const ClientErrorType type, const unsigned int expected
) {

	test_check_unsigned_eq (
		test_wait_counter (&client_errors[index][type], expected, TEST_WAIT_SECONDS),
		expected, "the client did not get the error"
	);

}

#pragma endregion

#pragma region tests

// create, join, search, leave, init, start, app packets, update, disconnect and destroy
static void requests_flow (const CerverHandlerType handler_type) {

	requests_counters_reset ();

	const u16 port = requests_port++;
	RequestsCerver requests;
	requests_cerver_start (&requests, CERVER_TYPE_GAME, handler_type, port, 0);

	RequestsClient clients[REQUESTS_CLIENTS];
	for (unsigned int i = 0; i < REQUESTS_CLIENTS; i++) clients[i] = requests_client_connect (port, i);

	RequestsClient *first = &clients[REQUESTS_FIRST];
	RequestsClient *second = &clients[REQUESTS_SECOND];
	RequestsClient *third = &clients[REQUESTS_THIRD];

	LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
	test_check_ptr (summary);

	// a bad game type
	requests_create (first, REQUESTS_BAD_GAME_TYPE);
	requests_wait_error (REQUESTS_FIRST, CLIENT_ERROR_CREATE_LOBBY, 1);

	// the first client creates and owns the lobby
	requests_create (first, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_CREATE, 1, summary);
	test_check_bool_eq (summary->created, true, NULL);
	test_check_str_eq (summary->game_type, REQUESTS_GAME_TYPE, NULL);
	test_check_unsigned_eq (summary->n_players, 1, NULL);
	test_check_unsigned_eq (summary->max_players, REQUESTS_MAX_PLAYERS, NULL);
	test_check_unsigned_eq (summary->players_count, 1, NULL);
	test_check_bool_eq (summary->players[0].owner, true, NULL);

	char lobby_id[LOBBY_ID_SIZE] = { 0 };
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);

	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_CREATE], 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_unsigned_eq (requests_read (&join_callbacks), 1, NULL);

	// a client is only in one lobby
	requests_create (first, REQUESTS_GAME_TYPE);
	requests_wait_error (REQUESTS_FIRST, CLIENT_ERROR_CREATE_LOBBY, 2);

	// the second client joins by the id
	requests_join (second, lobby_id, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_JOIN, 1, summary);
	test_check_str_eq (summary->id, lobby_id, NULL);
	test_check_unsigned_eq (summary->n_players, 2, NULL);
	// the cerver triggers its event after it sends the summary
	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_JOIN], 1, TEST_WAIT_SECONDS), 1, NULL);

	// the lobby is full, and a lobby that does not exist
	requests_join (third, lobby_id, REQUESTS_GAME_TYPE);
	requests_wait_error (REQUESTS_THIRD, CLIENT_ERROR_JOIN_LOBBY, 1);
	requests_join (third, REQUESTS_BAD_GAME_TYPE, REQUESTS_GAME_TYPE);
	requests_wait_error (REQUESTS_THIRD, CLIENT_ERROR_JOIN_LOBBY, 2);

	// without an id, no lobby has a place, so the third client gets a new one
	requests_join (third, NULL, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_THIRD, CLIENT_EVENT_LOBBY_CREATE, 1, summary);
	test_check_bool_eq (summary->created, true, NULL);
	test_check (strcmp (summary->id, lobby_id) != 0, "the search joined a full lobby");

	// its only player leaves, and the lobby is destroyed
	char other_id[LOBBY_ID_SIZE] = { 0 };
	(void) snprintf (other_id, LOBBY_ID_SIZE, "%s", summary->id);
	requests_lobby (third, GAME_PACKET_TYPE_LOBBY_LEAVE, other_id);
	requests_wait_event (REQUESTS_THIRD, CLIENT_EVENT_LOBBY_LEAVE, 1, NULL);
	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_DESTROY], 1, TEST_WAIT_SECONDS), 1, NULL);
	requests_lobby (third, GAME_PACKET_TYPE_LOBBY_LEAVE, other_id);
	requests_wait_error (REQUESTS_THIRD, CLIENT_ERROR_LEAVE_LOBBY, 1);

	// only the owner inits the game
	requests_lobby (second, GAME_PACKET_TYPE_GAME_INIT, lobby_id);
	requests_wait_error (REQUESTS_SECOND, CLIENT_ERROR_GAME_INIT, 1);
	requests_lobby (first, GAME_PACKET_TYPE_GAME_INIT, lobby_id);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_GAME_INIT, 1, NULL);
	test_check_unsigned_eq (requests_read (&init_callbacks), 1, NULL);

	// only the owner starts the game, and both players see it
	requests_lobby (second, GAME_PACKET_TYPE_GAME_START, lobby_id);
	requests_wait_error (REQUESTS_SECOND, CLIENT_ERROR_GAME_START, 1);
	requests_lobby (first, GAME_PACKET_TYPE_GAME_START, lobby_id);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_START, 1, summary);
	test_check_bool_eq (summary->in_game, true, NULL);
	test_check_bool_eq (summary->running, true, NULL);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_START, 1, NULL);
	test_check_unsigned_eq (requests_read (&start_callbacks), 1, NULL);
	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_START], 1, TEST_WAIT_SECONDS), 1, NULL);

	// a game is not started twice
	requests_lobby (first, GAME_PACKET_TYPE_GAME_START, lobby_id);
	requests_wait_error (REQUESTS_FIRST, CLIENT_ERROR_GAME_START, 1);

	// the update of the lobby runs every frame
	test_check (test_wait_counter (&update_calls, 1, TEST_WAIT_SECONDS) >= 1, "the update of the lobby did not run");

	// the lobby takes the app packets of its players
	requests_app_send (first, REQUESTS_APP_PACKETS);
	requests_app_send (second, REQUESTS_APP_PACKETS);
	test_check_unsigned_eq (
		test_wait_counter (&lobby_app_packets, 2 * REQUESTS_APP_PACKETS, TEST_WAIT_SECONDS),
		2 * REQUESTS_APP_PACKETS, "the lobby did not get the packets of its players"
	);

	test_check_unsigned_eq (requests_read (&cerver_app_packets), 0, NULL);

	// a client outside the lobby is handled by the cerver
	requests_app_send (third, REQUESTS_APP_PACKETS);
	test_check_unsigned_eq (
		test_wait_counter (&cerver_app_packets, REQUESTS_APP_PACKETS, TEST_WAIT_SECONDS),
		REQUESTS_APP_PACKETS, NULL
	);

	// a player gets the state of its lobby, and a client outside it can not
	requests_lobby (second, GAME_PACKET_TYPE_LOBBY_UPDATE, lobby_id);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_UPDATE, 1, summary);
	test_check_unsigned_eq (summary->n_players, 2, NULL);
	requests_lobby (third, GAME_PACKET_TYPE_LOBBY_UPDATE, lobby_id);
	requests_wait_error (REQUESTS_THIRD, CLIENT_ERROR_FIND_LOBBY, 1);

	// an unknown request
	requests_lobby (first, REQUESTS_UNKNOWN_REQUEST, lobby_id);
	requests_wait_error (REQUESTS_FIRST, CLIENT_ERROR_PACKET_ERROR, 1);

	// the second player disconnects: it leaves the lobby, and the owner gets the update
	requests_client_end (second);
	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_LEAVE], 2, TEST_WAIT_SECONDS), 2, NULL);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_UPDATE, 1, summary);
	test_check_unsigned_eq (summary->n_players, 1, NULL);
	test_check_bool_eq (summary->players[0].owner, true, NULL);

	// only the owner destroys the lobby
	requests_lobby (third, GAME_PACKET_TYPE_LOBBY_DESTROY, lobby_id);
	requests_wait_error (REQUESTS_THIRD, CLIENT_ERROR_DESTROY_LOBBY, 1);
	requests_lobby (first, GAME_PACKET_TYPE_LOBBY_DESTROY, lobby_id);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_DESTROY, 1, NULL);
	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_DESTROY], 2, TEST_WAIT_SECONDS), 2, NULL);
	test_check_unsigned_eq (requests_read (&end_callbacks), 1, NULL);

	// the owner is not in a lobby anymore, so it creates another one and starts it
	requests_create (first, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_CREATE, 2, summary);
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);
	requests_lobby (first, GAME_PACKET_TYPE_GAME_START, lobby_id);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_START, 2, NULL);

	// the cerver stops with the lobby running, and the client gets its packets back in the cerver poll
	requests_app_send (first, REQUESTS_APP_PACKETS);
	test_check_unsigned_eq (
		test_wait_counter (&lobby_app_packets, 3 * REQUESTS_APP_PACKETS, TEST_WAIT_SECONDS),
		3 * REQUESTS_APP_PACKETS, NULL
	);

	requests_cerver_stop (&requests);
	test_check_unsigned_eq (requests_read (&end_callbacks), 2, NULL);
	test_check_unsigned_eq (requests_read (&cerver_events[CERVER_EVENT_LOBBY_DESTROY]), 3, NULL);

	requests_client_end (first);
	requests_client_end (third);

	free (summary);

}

static void test_requests_flow_poll (void) {

	requests_flow (CERVER_HANDLER_TYPE_POLL);

}

static void test_requests_flow_threads (void) {

	requests_flow (CERVER_HANDLER_TYPE_THREADS);

}

// the owner disconnects and the other player is the owner, that can start the game
static void requests_owner_disconnects (const CerverHandlerType handler_type) {

	requests_counters_reset ();

	const u16 port = requests_port++;
	RequestsCerver requests;
	requests_cerver_start (&requests, CERVER_TYPE_GAME, handler_type, port, 0);

	RequestsClient first = requests_client_connect (port, REQUESTS_FIRST);
	RequestsClient second = requests_client_connect (port, REQUESTS_SECOND);

	LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
	test_check_ptr (summary);

	requests_join (&first, NULL, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_CREATE, 1, summary);

	char lobby_id[LOBBY_ID_SIZE] = { 0 };
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);

	// the search finds the lobby with a place
	requests_join (&second, NULL, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_JOIN, 1, summary);
	test_check_str_eq (summary->id, lobby_id, NULL);

	requests_lobby (&first, GAME_PACKET_TYPE_GAME_START, lobby_id);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_START, 1, NULL);

	// the owner leaves the first slot of the lobby poll empty
	requests_client_end (&first);

	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_UPDATE, 1, summary);
	test_check_unsigned_eq (summary->n_players, 1, NULL);
	test_check_bool_eq (summary->players[0].owner, true, NULL);

	// the lobby still receives the player after the hole, that is the owner now
	requests_app_send (&second, REQUESTS_APP_PACKETS);
	test_check_unsigned_eq (
		test_wait_counter (&lobby_app_packets, REQUESTS_APP_PACKETS, TEST_WAIT_SECONDS),
		REQUESTS_APP_PACKETS, "the lobby did not receive the player after the hole"
	);

	requests_lobby (&second, GAME_PACKET_TYPE_LOBBY_UPDATE, lobby_id);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_UPDATE, 2, summary);
	test_check_bool_eq (summary->players[0].owner, true, NULL);

	// the last player disconnects in the middle of the game, and the lobby is destroyed
	requests_client_end (&second);
	test_check_unsigned_eq (test_wait_counter (&cerver_events[CERVER_EVENT_LOBBY_DESTROY], 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_unsigned_eq (test_wait_counter (&end_callbacks, 1, TEST_WAIT_SECONDS), 1, NULL);

	requests_cerver_stop (&requests);

	free (summary);

}

static void test_requests_owner_disconnects_poll (void) {

	requests_owner_disconnects (CERVER_HANDLER_TYPE_POLL);

}

static void test_requests_owner_disconnects_threads (void) {

	requests_owner_disconnects (CERVER_HANDLER_TYPE_THREADS);

}

// a player that stops sending leaves the lobby after the player timeout
static void requests_player_timeout (const CerverHandlerType handler_type) {

	requests_counters_reset ();

	const u16 port = requests_port++;
	RequestsCerver requests;
	requests_cerver_start (&requests, CERVER_TYPE_GAME, handler_type, port, REQUESTS_PLAYER_TIMEOUT);

	RequestsClient first = requests_client_connect (port, REQUESTS_FIRST);
	RequestsClient second = requests_client_connect (port, REQUESTS_SECOND);

	LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
	test_check_ptr (summary);

	requests_create (&first, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_CREATE, 1, summary);

	char lobby_id[LOBBY_ID_SIZE] = { 0 };
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);

	requests_join (&second, lobby_id, REQUESTS_GAME_TYPE);
	requests_wait_event (REQUESTS_SECOND, CLIENT_EVENT_LOBBY_JOIN, 1, NULL);

	requests_lobby (&first, GAME_PACKET_TYPE_GAME_START, lobby_id);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_START, 1, NULL);

	// only the owner keeps sending
	const struct timespec interval = { 0, REQUESTS_KEEP_ALIVE_NANOSECONDS };
	for (
		unsigned int round = 0;
		(round < REQUESTS_KEEP_ALIVE_ROUNDS) && !__atomic_load_n (&cerver_events[CERVER_EVENT_LOBBY_LEAVE], __ATOMIC_SEQ_CST);
		round++
	) {
		requests_app_send (&first, 1);
		(void) nanosleep (&interval, NULL);
	}

	test_check_unsigned_eq (requests_read (&cerver_events[CERVER_EVENT_LOBBY_LEAVE]), 1, "the silent player did not leave");
	test_check_unsigned_eq (requests_read (&leave_callbacks), 1, NULL);

	requests_lobby (&first, GAME_PACKET_TYPE_LOBBY_UPDATE, lobby_id);
	requests_wait_event (REQUESTS_FIRST, CLIENT_EVENT_LOBBY_UPDATE, 1, summary);
	test_check_unsigned_eq (summary->n_players, 1, NULL);

	requests_cerver_stop (&requests);

	requests_client_end (&first);
	requests_client_end (&second);

	free (summary);

}

static void test_requests_player_timeout_poll (void) {

	requests_player_timeout (CERVER_HANDLER_TYPE_POLL);

}

static void test_requests_player_timeout_threads (void) {

	requests_player_timeout (CERVER_HANDLER_TYPE_THREADS);

}

// a cerver that is not a game cerver answers the game requests with an error
static void test_requests_not_game_cerver (void) {

	requests_counters_reset ();

	const u16 port = requests_port++;
	RequestsCerver requests;
	requests_cerver_start (&requests, CERVER_TYPE_CUSTOM, CERVER_HANDLER_TYPE_POLL, port, 0);

	RequestsClient first = requests_client_connect (port, REQUESTS_FIRST);

	requests_create (&first, REQUESTS_GAME_TYPE);
	requests_wait_error (REQUESTS_FIRST, CLIENT_ERROR_PACKET_ERROR, 1);

	requests_join (&first, NULL, REQUESTS_GAME_TYPE);
	requests_wait_error (REQUESTS_FIRST, CLIENT_ERROR_PACKET_ERROR, 2);

	// the cerver still works
	requests_app_send (&first, REQUESTS_APP_PACKETS);
	test_check_unsigned_eq (
		test_wait_counter (&cerver_app_packets, REQUESTS_APP_PACKETS, TEST_WAIT_SECONDS),
		REQUESTS_APP_PACKETS, NULL
	);

	requests_cerver_stop (&requests);

	requests_client_end (&first);

}

#pragma endregion

void game_tests_requests (void) {

	(void) printf ("Testing GAME requests...\n");

	test_run (test_requests_not_game_cerver);
	test_run (test_requests_flow_poll);
	test_run (test_requests_flow_threads);
	test_run (test_requests_owner_disconnects_poll);
	test_run (test_requests_owner_disconnects_threads);
	test_run (test_requests_player_timeout_poll);
	test_run (test_requests_player_timeout_threads);

	(void) printf ("Done with GAME requests tests!\n\n");

}
