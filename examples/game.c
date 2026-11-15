#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/game/game.h>
#include <cerver/game/lobby.h>
#include <cerver/game/player.h>
#include <cerver/game/types.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include <app/events.h>

#define GAME_EXAMPLE_PORT				7000
#define GAME_EXAMPLE_CONNECTION_QUEUE	2
#define GAME_EXAMPLE_BUFFER_SIZE		4096
#define GAME_EXAMPLE_POLL_TIMEOUT		2000

#define GAME_EXAMPLE_TYPE				"arcade"
#define GAME_EXAMPLE_MAX_PLAYERS		4
#define GAME_EXAMPLE_PLAYER_TIMEOUT		60
#define GAME_EXAMPLE_FPS				10

static Cerver *my_cerver = NULL;

// the signals only stop the cerver, it is released when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	if (my_cerver) (void) cerver_shutdown (my_cerver);

}

static void finish (void) {

	cerver_stats_print (my_cerver, true, true);
	cerver_log_msg ("\nGame Stats:\n");
	game_cerver_stats_print (my_cerver);

	(void) cerver_teardown (my_cerver);

}

// the app packets of the clients that are not playing
static void my_game_packet_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	cerver_log_debug ("Got an app packet from client %lu", packet->client->id);

}

// the app packets of the players while their lobby runs
static void arcade_lobby_packet_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	cerver_log_debug (
		"Got a game packet from client %lu in lobby %s",
		packet->client->id, packet->lobby->id->str
	);

}

// called every frame while the lobby runs
static void arcade_lobby_update (void *cerver_lobby_ptr) {

	(void) cerver_lobby_ptr;

}

// prepares the data of the game before it starts, the owner requests it
static void arcade_game_init (void *lobby_player_ptr) {

	const LobbyPlayer *lobby_player = (const LobbyPlayer *) lobby_player_ptr;

	cerver_log_debug ("Initializing the game of lobby %s", lobby_player->lobby->id->str);

}

static void *arcade_game_start (void *lobby_ptr) {

	Lobby *lobby = (Lobby *) lobby_ptr;

	lobby_set_packet_handler (lobby, arcade_lobby_packet_handler);
	lobby_set_update (lobby, arcade_lobby_update);

	cerver_log_success ("The game of lobby %s has started!", lobby->id->str);

	return NULL;

}

static void *arcade_game_end (void *lobby_ptr) {

	const Lobby *lobby = (const Lobby *) lobby_ptr;

	cerver_log_msg ("The game of lobby %s has ended", lobby->id->str);

	return NULL;

}

// this method gets executed every time a new player joins the lobby
static void arcade_game_join (void *lobby_player_ptr) {

	const LobbyPlayer *lobby_player = (const LobbyPlayer *) lobby_player_ptr;

	cerver_log_msg (
		"Client %lu joined lobby %s",
		lobby_player->player->client->id, lobby_player->lobby->id->str
	);

}

static void arcade_game_leave (void *lobby_player_ptr) {

	const LobbyPlayer *lobby_player = (const LobbyPlayer *) lobby_player_ptr;

	cerver_log_msg (
		"Client %lu left lobby %s",
		lobby_player->player->client->id, lobby_player->lobby->id->str
	);

}

static void arcade_game_type_register (Cerver *cerver) {

	GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

	GameType *arcade_game_type = game_type_create (
		GAME_EXAMPLE_TYPE, NULL, NULL,
		arcade_game_start, arcade_game_end
	);

	(void) game_type_add_lobby_config (arcade_game_type, true, NULL, GAME_EXAMPLE_MAX_PLAYERS);
	game_type_set_on_lobby_join (arcade_game_type, arcade_game_join);
	game_type_set_on_lobby_leave (arcade_game_type, arcade_game_leave);
	game_type_set_init (arcade_game_type, arcade_game_init);
	game_type_set_player_timeout (arcade_game_type, GAME_EXAMPLE_PLAYER_TIMEOUT);
	game_type_set_fps (arcade_game_type, GAME_EXAMPLE_FPS);

	(void) game_type_register (game_cerver->game_types, arcade_game_type);

}

int main (void) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_line_break ();
	cerver_version_print_full ();
	cerver_log_line_break ();

	cerver_log_debug ("Simple Game Cerver Example");
	cerver_log_line_break ();

	my_cerver = cerver_create (
		CERVER_TYPE_GAME,
		"game-cerver",
		GAME_EXAMPLE_PORT,
		PROTOCOL_TCP,
		false,
		GAME_EXAMPLE_CONNECTION_QUEUE
	);

	if (my_cerver) {
		cerver_set_welcome_msg (my_cerver, "Welcome - Simple Game Cerver Example");

		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (my_cerver, GAME_EXAMPLE_BUFFER_SIZE);

		cerver_set_handler_type (my_cerver, CERVER_HANDLER_TYPE_POLL);
		cerver_set_poll_time_out (my_cerver, GAME_EXAMPLE_POLL_TIMEOUT);

		Handler *app_handler = handler_create (my_game_packet_handler);
		handler_set_direct_handle (app_handler, true);
		cerver_set_app_handlers (my_cerver, app_handler, NULL);

		/*** game configuration ***/
		arcade_game_type_register (my_cerver);

		app_cerver_events_register (my_cerver);

		if (!cerver_start (my_cerver)) {
			finish ();
		}

		else {
			cerver_log_error ("Failed to start %s!", my_cerver->info->name);

			(void) cerver_teardown (my_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
