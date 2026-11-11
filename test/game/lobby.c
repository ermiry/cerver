#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/events.h>

#include <cerver/collections/dlist.h>

#include <cerver/game/game.h>
#include <cerver/game/gametype.h>
#include <cerver/game/lobby.h>
#include <cerver/game/player.h>
#include <cerver/game/score.h>

#include "../test.h"

#include "game.h"

// the lobbies of a game cerver that is not started, with clients that are not connected:
// - the ids of the lobbies created in the same second are different
// - a lobby without max players takes the default, and a full lobby is not joined
// - a client is only once in a lobby, and only in one lobby
// - the owner that leaves gives the lobby to the next player, the last one destroys it
// - a reference keeps a lobby that was torn down until it is released
// - the players that stop sending leave the lobby, the last one empties it
// - the summary of a lobby has its players, its owner and its settings
// - the players of a lobby with an id are added to a scoreboard

#define LOBBIES_NAME					"lobbies-cerver"
#define LOBBIES_PORT					7790
#define LOBBIES_ADDRESS					"127.0.0.1"
#define LOBBIES_GAME_TYPE				"lobbies"

#define LOBBIES_IDS						64
#define LOBBIES_MAX_PLAYERS				2
#define LOBBIES_CLIENTS					4
#define LOBBIES_PLAYER_TIMEOUT			2
#define LOBBIES_MILLISECONDS_PER_SECOND	1000
#define LOBBIES_MOMENT_MILLISECONDS		1
#define LOBBIES_PLAYER_ID				"first-player"
#define LOBBIES_SCORE					"kills"

static unsigned int lobby_destroy_events = 0;
static unsigned int lobby_leave_events = 0;
static unsigned int leave_callbacks = 0;

static void *lobbies_destroy_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;
	test_check_ptr (event_data->lobby);

	cerver_event_data_delete (event_data);

	test_counter_add (&lobby_destroy_events);

	return NULL;

}

static void *lobbies_leave_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;
	test_check_ptr (event_data->lobby);
	test_check_ptr (event_data->client);

	cerver_event_data_delete (event_data);

	test_counter_add (&lobby_leave_events);

	return NULL;

}

static void lobbies_on_leave (void *lobby_player_ptr) {

	const LobbyPlayer *lobby_player = (const LobbyPlayer *) lobby_player_ptr;
	test_check_ptr (lobby_player->lobby);
	test_check_ptr (lobby_player->player);

	test_counter_add (&leave_callbacks);

}

static Cerver *lobbies_cerver_create (void) {

	lobby_destroy_events = 0;
	lobby_leave_events = 0;
	leave_callbacks = 0;

	Cerver *cerver = cerver_create (
		CERVER_TYPE_GAME, LOBBIES_NAME, LOBBIES_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_ptr (cerver->cerver_data);

	test_check_unsigned_eq (
		cerver_event_register (cerver, CERVER_EVENT_LOBBY_DESTROY, lobbies_destroy_event, NULL, NULL, false, false),
		0, NULL
	);

	test_check_unsigned_eq (
		cerver_event_register (cerver, CERVER_EVENT_LOBBY_LEAVE, lobbies_leave_event, NULL, NULL, false, false),
		0, NULL
	);

	return cerver;

}

static GameType *lobbies_game_type (void) {

	GameType *game_type = game_type_create (LOBBIES_GAME_TYPE, NULL, NULL, NULL, NULL);
	test_check_ptr (game_type);

	game_type_set_on_lobby_leave (game_type, lobbies_on_leave);

	return game_type;

}

static void lobbies_cerver_delete (Cerver *cerver, GameType *game_type) {

	game_type_delete (game_type);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

static void lobbies_clients_create (Client **clients, const unsigned int count) {

	for (unsigned int i = 0; i < count; i++) {
		clients[i] = client_create ();
		test_check_ptr (clients[i]);
	}

}

static void lobbies_clients_delete (Client **clients, const unsigned int count) {

	for (unsigned int i = 0; i < count; i++) {
		client_delete (clients[i]);
	}

}

// the ids of the lobbies created in the same second are different
static void test_lobbies_ids (void) {

	String *ids[LOBBIES_IDS] = { NULL };

	for (unsigned int i = 0; i < LOBBIES_IDS; i++) {
		ids[i] = (String *) lobby_default_id_generator (NULL);
		test_check_ptr (ids[i]);

		for (unsigned int j = 0; j < i; j++) {
			test_check (strcmp (ids[i]->str, ids[j]->str) != 0, "two lobbies have the same id");
		}
	}

	for (unsigned int i = 0; i < LOBBIES_IDS; i++) str_delete (ids[i]);

}

// a lobby without max players takes the default, and a full lobby is not joined
static void test_lobbies_max_players (void) {

	Cerver *cerver = lobbies_cerver_create ();

	Client *clients[LOBBIES_CLIENTS] = { NULL };
	lobbies_clients_create (clients, LOBBIES_CLIENTS);

	Lobby *defaults = lobby_create (cerver, clients[0], true, NULL, 0);
	test_check_ptr (defaults);
	test_check_unsigned_eq (defaults->max_players, LOBBY_DEFAULT_MAX_PLAYERS, NULL);
	test_check_unsigned_eq (lobby_teardown (NULL, defaults), 0, NULL);

	Lobby *lobby = lobby_create (cerver, clients[0], true, NULL, LOBBIES_MAX_PLAYERS);
	test_check_ptr (lobby);
	test_check_unsigned_eq (lobby->max_players, LOBBIES_MAX_PLAYERS, NULL);

	test_check_unsigned_eq (lobby_join (cerver, clients[1], lobby), 0, NULL);
	test_check_unsigned_eq (lobby->n_current_players, LOBBIES_MAX_PLAYERS, NULL);

	// the lobby is full
	test_check_unsigned_eq (lobby_join (cerver, clients[2], lobby), 1, "a full lobby was joined");
	test_check_unsigned_eq (lobby->n_current_players, LOBBIES_MAX_PLAYERS, NULL);

	// a lobby in game is not joined
	test_check_unsigned_eq (lobby_leave (cerver, lobby, lobby_player_of_client (lobby, clients[1])), 0, NULL);
	lobby->in_game = true;
	test_check_unsigned_eq (lobby_join (cerver, clients[2], lobby), 1, "a lobby in game was joined");
	lobby->in_game = false;

	test_check_unsigned_eq (lobby_teardown (NULL, lobby), 0, NULL);
	test_check_unsigned_eq (lobby_destroy_events, 2, NULL);

	lobbies_clients_delete (clients, LOBBIES_CLIENTS);
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// a client is only once in a lobby, and only in one lobby
static void test_lobbies_duplicated_players (void) {

	Cerver *cerver = lobbies_cerver_create ();

	Client *clients[LOBBIES_CLIENTS] = { NULL };
	lobbies_clients_create (clients, LOBBIES_CLIENTS);

	// the connections tell in which lobby the client is
	Connection *connection = client_connection_create (clients[1], LOBBIES_ADDRESS, LOBBIES_PORT, PROTOCOL_TCP, false);
	test_check_ptr (connection);

	Lobby *first = lobby_create (cerver, clients[0], true, NULL, LOBBIES_CLIENTS);
	Lobby *second = lobby_create (cerver, clients[2], true, NULL, LOBBIES_CLIENTS);
	test_check_ptr (first);
	test_check_ptr (second);

	test_check_unsigned_eq (lobby_join (cerver, clients[0], first), 1, "the owner joined its lobby again");

	test_check_unsigned_eq (lobby_join (cerver, clients[1], first), 0, NULL);
	test_check_ptr_eq (connection->lobby, first);
	test_check_unsigned_eq (lobby_join (cerver, clients[1], first), 1, "a player joined its lobby twice");
	test_check_unsigned_eq (lobby_join (cerver, clients[1], second), 1, "a player joined a second lobby");
	test_check_unsigned_eq (first->n_current_players, 2, NULL);

	// a player that leaves can join another lobby
	test_check_unsigned_eq (lobby_leave (cerver, first, lobby_player_of_client (first, clients[1])), 0, NULL);
	test_check_null_ptr (connection->lobby);
	test_check_unsigned_eq (lobby_join (cerver, clients[1], second), 0, NULL);
	test_check_ptr_eq (connection->lobby, second);

	// the teardown takes the lobby out of the connections
	test_check_unsigned_eq (lobby_teardown (NULL, second), 0, NULL);
	test_check_null_ptr (connection->lobby);

	test_check_unsigned_eq (lobby_teardown (NULL, first), 0, NULL);

	lobbies_clients_delete (clients, LOBBIES_CLIENTS);
	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the owner that leaves gives the lobby to the next player, the last one destroys it
static void test_lobbies_owner (void) {

	Cerver *cerver = lobbies_cerver_create ();
	GameType *game_type = lobbies_game_type ();

	Client *clients[LOBBIES_CLIENTS] = { NULL };
	lobbies_clients_create (clients, LOBBIES_CLIENTS);

	Lobby *lobby = lobby_create (cerver, clients[0], true, NULL, LOBBIES_CLIENTS);
	test_check_ptr (lobby);
	lobby->game_type = game_type;

	for (unsigned int i = 1; i < LOBBIES_CLIENTS; i++) {
		test_check_unsigned_eq (lobby_join (cerver, clients[i], lobby), 0, NULL);
	}

	(void) lobby_reference (lobby);

	for (unsigned int i = 0; i < LOBBIES_CLIENTS; i++) {
		test_check_ptr_eq (lobby->owner, lobby_player_of_client (lobby, clients[i]));
		test_check_unsigned_eq (lobby_leave (cerver, lobby, lobby->owner), 0, NULL);
		test_check_unsigned_eq (lobby->n_current_players, LOBBIES_CLIENTS - i - 1, NULL);
	}

	// the last player destroyed the lobby, the reference keeps it
	test_check_null_ptr (lobby->owner);
	test_check_bool_eq (lobby->ended, true, NULL);
	test_check_unsigned_eq (lobby_destroy_events, 1, NULL);
	test_check_unsigned_eq (lobby_leave_events, LOBBIES_CLIENTS, NULL);
	test_check_unsigned_eq (leave_callbacks, LOBBIES_CLIENTS, NULL);

	// an ended lobby is not joined nor started, and a second teardown does nothing
	test_check_unsigned_eq (lobby_join (cerver, clients[0], lobby), 1, NULL);
	test_check_unsigned_eq (lobby_start (cerver, lobby), 1, NULL);
	test_check_unsigned_eq (lobby_teardown (NULL, lobby), 0, NULL);
	test_check_unsigned_eq (lobby_destroy_events, 1, NULL);

	lobby_release (lobby);

	lobbies_clients_delete (clients, LOBBIES_CLIENTS);
	lobbies_cerver_delete (cerver, game_type);

}

// the players that stop sending leave the lobby, the last one empties it
static void test_lobbies_timeouts (void) {

	Cerver *cerver = lobbies_cerver_create ();
	GameType *game_type = lobbies_game_type ();
	game_type_set_player_timeout (game_type, LOBBIES_PLAYER_TIMEOUT);

	Client *clients[LOBBIES_CLIENTS] = { NULL };
	lobbies_clients_create (clients, LOBBIES_CLIENTS);

	Lobby *lobby = lobby_create (cerver, clients[0], true, NULL, LOBBIES_CLIENTS);
	test_check_ptr (lobby);
	lobby->game_type = game_type;

	test_check_unsigned_eq (lobby_join (cerver, clients[1], lobby), 0, NULL);

	const u64 now = player_activity_now ();
	const u64 timeout = (u64) LOBBIES_PLAYER_TIMEOUT * LOBBIES_MILLISECONDS_PER_SECOND;

	// the players sent at the same moment, a slow run (valgrind) does not move it
	lobby_player_of_client (lobby, clients[0])->last_activity = now;
	lobby_player_of_client (lobby, clients[1])->last_activity = now;

	// nobody is late yet, even a moment before the timeout
	test_check_bool_eq (lobby_players_check_timeouts (lobby, now), false, NULL);
	test_check_bool_eq (lobby_players_check_timeouts (lobby, now + timeout - LOBBIES_MOMENT_MILLISECONDS), false, NULL);
	test_check_unsigned_eq (lobby->n_current_players, 2, NULL);

	// the first player stopped sending, the other one sent just now
	Player *late = lobby_player_of_client (lobby, clients[0]);
	late->last_activity = now - timeout;

	test_check_bool_eq (lobby_players_check_timeouts (lobby, now), false, NULL);
	test_check_unsigned_eq (lobby->n_current_players, 1, NULL);
	test_check_null_ptr (lobby_player_of_client (lobby, clients[0]));
	test_check_ptr_eq (lobby->owner, lobby_player_of_client (lobby, clients[1]));

	// a game type without a timeout keeps its players
	game_type_set_player_timeout (game_type, 0);
	test_check_bool_eq (lobby_players_check_timeouts (lobby, now + (2 * timeout)), false, NULL);
	game_type_set_player_timeout (game_type, LOBBIES_PLAYER_TIMEOUT);

	// the last player leaves an empty lobby
	test_check_bool_eq (lobby_players_check_timeouts (lobby, now + (2 * timeout)), true, NULL);
	test_check_unsigned_eq (lobby->n_current_players, 0, NULL);
	test_check_unsigned_eq (leave_callbacks, 2, NULL);

	test_check_unsigned_eq (lobby_teardown (NULL, lobby), 0, NULL);

	lobbies_clients_delete (clients, LOBBIES_CLIENTS);
	lobbies_cerver_delete (cerver, game_type);

}

// the summary of a lobby has its players, its owner and its settings
static void test_lobbies_summary (void) {

	Cerver *cerver = lobbies_cerver_create ();
	GameType *game_type = lobbies_game_type ();
	game_type_set_player_timeout (game_type, LOBBIES_PLAYER_TIMEOUT);

	Client *clients[LOBBIES_CLIENTS] = { NULL };
	lobbies_clients_create (clients, LOBBIES_CLIENTS);

	Lobby *lobby = lobby_create (cerver, clients[0], true, NULL, LOBBIES_CLIENTS);
	test_check_ptr (lobby);
	lobby->game_type = game_type;

	test_check_unsigned_eq (lobby_join (cerver, clients[1], lobby), 0, NULL);
	player_set_id (lobby_player_of_client (lobby, clients[1]), LOBBIES_PLAYER_ID);

	LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
	test_check_ptr (summary);

	lobby_summary_set (summary, lobby, true);
	test_check_str_eq (summary->id, lobby->id->str, NULL);
	test_check_str_eq (summary->game_type, LOBBIES_GAME_TYPE, NULL);
	test_check_bool_eq (summary->created, true, NULL);
	test_check_bool_eq (summary->running, false, NULL);
	test_check_unsigned_eq (summary->max_players, LOBBIES_CLIENTS, NULL);
	test_check_unsigned_eq (summary->n_players, 2, NULL);
	test_check_unsigned_eq (summary->owner_client_id, clients[0]->id, NULL);
	test_check_unsigned_eq (summary->players_count, 2, NULL);
	test_check_bool_eq (summary->players[0].owner, true, NULL);
	test_check_bool_eq (summary->players[1].owner, false, NULL);
	test_check_str_eq (summary->players[1].player_id, LOBBIES_PLAYER_ID, NULL);
	test_check_unsigned_eq (summary->settings.player_timeout, LOBBIES_PLAYER_TIMEOUT, NULL);
	test_check_unsigned_eq (summary->settings.fps, GAME_TYPE_DEFAULT_FPS, NULL);

	// the summary survives the wire
	Packet *packet = lobby_summary_packet_create (lobby, GAME_PACKET_TYPE_LOBBY_UPDATE, false);
	test_check_ptr (packet);

	LobbySummary *decoded = (LobbySummary *) malloc (sizeof (LobbySummary));
	test_check_ptr (decoded);
	test_check_unsigned_eq (packet_get_object (packet, &lobby_summary_schema, NULL, decoded), SERIALIZER_RESULT_NONE, NULL);
	test_check_str_eq (decoded->id, lobby->id->str, NULL);
	test_check_bool_eq (decoded->created, false, NULL);
	test_check_unsigned_eq (decoded->players_count, 2, NULL);
	test_check_str_eq (decoded->players[1].player_id, LOBBIES_PLAYER_ID, NULL);

	free (decoded);
	packet_delete (packet);
	free (summary);

	// the players with an id are added to a scoreboard
	ScoreBoard *scores = game_score_create (LOBBIES_CLIENTS, 1, LOBBIES_SCORE);
	test_check_ptr (scores);
	game_score_add_lobby_players (scores, lobby);
	test_check_unsigned_eq (scores->registeredPlayers, 1, NULL);
	game_score_delete (scores);

	test_check_unsigned_eq (lobby_teardown (NULL, lobby), 0, NULL);

	lobbies_clients_delete (clients, LOBBIES_CLIENTS);
	lobbies_cerver_delete (cerver, game_type);

}

void game_tests_lobbies (void) {

	(void) printf ("Testing GAME lobbies...\n");

	test_run (test_lobbies_ids);
	test_run (test_lobbies_max_players);
	test_run (test_lobbies_duplicated_players);
	test_run (test_lobbies_owner);
	test_run (test_lobbies_timeouts);
	test_run (test_lobbies_summary);

	(void) printf ("Done with GAME lobbies tests!\n\n");

}
