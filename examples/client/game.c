#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <unistd.h>

#include <cerver/version.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/client/events.h>
#include <cerver/connection/connection.h>
#include <cerver/packets/packets.h>

#include <cerver/game/game.h>
#include <cerver/game/lobby.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

// a client of the game example: it joins a lobby of the arcade game,
// starts it when it is its owner, sends some packets to the lobby and leaves it

#define GAME_CLIENT_ADDRESS				"127.0.0.1"
#define GAME_CLIENT_PORT				7000
#define GAME_CLIENT_TYPE				"arcade"

#define GAME_CLIENT_PACKETS				4
#define GAME_CLIENT_WAIT_SECONDS		2

static Client *game_client = NULL;
static Connection *game_connection = NULL;

static char lobby_id[LOBBY_ID_SIZE] = { 0 };
static bool lobby_owner = false;

static void game_client_summary_print (const char *action, const LobbySummary *summary) {

	cerver_log_success (
		"%s lobby %s of %s - %u / %u players - %s",
		action, summary->id, summary->game_type,
		summary->n_players, summary->max_players,
		summary->in_game ? "in game" : "waiting"
	);

}

static void *game_client_lobby_create (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;
	const LobbySummary *summary = (const LobbySummary *) event_data->response_data;

	game_client_summary_print ("Created", summary);
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);
	lobby_owner = true;

	client_event_data_delete (event_data);

	return NULL;

}

static void *game_client_lobby_join (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;
	const LobbySummary *summary = (const LobbySummary *) event_data->response_data;

	game_client_summary_print ("Joined", summary);
	(void) snprintf (lobby_id, LOBBY_ID_SIZE, "%s", summary->id);

	client_event_data_delete (event_data);

	return NULL;

}

static void *game_client_lobby_event (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	game_client_summary_print ("Updated", (const LobbySummary *) event_data->response_data);

	client_event_data_delete (event_data);

	return NULL;

}

static void *game_client_error (void *error_data_ptr) {

	ClientErrorData *error_data = (ClientErrorData *) error_data_ptr;

	cerver_log_error (
		"Game error: %s",
		error_data->error_message ? error_data->error_message->str : "without a message"
	);

	client_error_data_delete (error_data);

	return NULL;

}

static void game_client_events_register (Client *client) {

	(void) client_event_register (client, CLIENT_EVENT_LOBBY_CREATE, game_client_lobby_create, NULL, NULL, false, false);
	(void) client_event_register (client, CLIENT_EVENT_LOBBY_JOIN, game_client_lobby_join, NULL, NULL, false, false);
	(void) client_event_register (client, CLIENT_EVENT_LOBBY_START, game_client_lobby_event, NULL, NULL, false, false);
	(void) client_event_register (client, CLIENT_EVENT_LOBBY_UPDATE, game_client_lobby_event, NULL, NULL, false, false);
	(void) client_event_register (client, CLIENT_EVENT_LOBBY_LEAVE, game_client_lobby_event, NULL, NULL, false, false);

	(void) client_error_register (client, CLIENT_ERROR_FIND_LOBBY, game_client_error, NULL, NULL, false, false);
	(void) client_error_register (client, CLIENT_ERROR_GAME_START, game_client_error, NULL, NULL, false, false);
	(void) client_error_register (client, CLIENT_ERROR_LEAVE_LOBBY, game_client_error, NULL, NULL, false, false);

}

static void game_client_lobby_request (const u32 request_type) {

	LobbyRequest request;
	serializer_object_init (&lobby_request_schema, &request);
	(void) snprintf (request.lobby_id, LOBBY_ID_SIZE, "%s", lobby_id);

	(void) game_request_send (game_client, game_connection, request_type, &lobby_request_schema, &request);

}

static void game_client_play (void) {

	// joins a lobby with a place, or the cerver creates one
	LobbyJoin join;
	serializer_object_init (&lobby_join_request_schema, &join);
	(void) snprintf (join.game_type, LOBBY_GAME_TYPE_SIZE, "%s", GAME_CLIENT_TYPE);
	(void) game_request_send (game_client, game_connection, GAME_PACKET_TYPE_LOBBY_JOIN, &lobby_join_request_schema, &join);

	(void) sleep (GAME_CLIENT_WAIT_SECONDS);

	if (lobby_id[0]) {
		if (lobby_owner) game_client_lobby_request (GAME_PACKET_TYPE_GAME_START);

		(void) sleep (GAME_CLIENT_WAIT_SECONDS);

		for (unsigned int i = 0; i < GAME_CLIENT_PACKETS; i++) {
			Packet *packet = packet_create_request (PACKET_TYPE_APP, 0);
			if (packet) {
				packet_set_network_values (packet, NULL, game_client, game_connection, NULL);
				(void) packet_send (packet, 0, NULL, false);
				packet_delete (packet);
			}
		}

		game_client_lobby_request (GAME_PACKET_TYPE_LOBBY_UPDATE);
		(void) sleep (GAME_CLIENT_WAIT_SECONDS);

		game_client_lobby_request (GAME_PACKET_TYPE_LOBBY_LEAVE);
		(void) sleep (GAME_CLIENT_WAIT_SECONDS);
	}

}

int main (void) {

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Game Client Example");
	cerver_log_line_break ();

	game_client = client_create ();
	if (game_client) {
		client_set_name (game_client, "game-client");
		game_client_events_register (game_client);

		game_connection = client_connection_create (
			game_client, GAME_CLIENT_ADDRESS, GAME_CLIENT_PORT, PROTOCOL_TCP, false
		);

		if (game_connection && !client_connect_and_start (game_client, game_connection)) {
			game_client_play ();

			(void) client_connection_end (game_client, game_connection);
		}

		else {
			cerver_log_error ("Failed to connect to the game cerver!");
		}

		(void) client_teardown (game_client);
	}

	cerver_end ();

	return 0;

}
