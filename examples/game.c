#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include "cerver/cerver.h"
#include "cerver/game/game.h"
#include "cerver/game/gametype.h"
#include "cerver/utils/log.h"

static Cerver *my_cerver = NULL;

// correctly closes any on-going server and process when quitting the appplication
static void my_game_end (int dummy) {
	
	if (my_cerver) {
		cerver_stats_print (my_cerver);
		printf ("\nGame Stats:\n");
		game_cerver_stats_print (my_cerver);
		cerver_teardown (my_cerver);
	} 

	exit (0);

}

static int my_game_init (void) {

	// method where you will init global values for your game / app
	// like connecting to a database or loading some resources

	return 0;

}

static void my_game_packet_handler (void *data) {

	// method to handle APP_PACKET type packets that are specific for your application 

}

static void my_game_on_client_connected (void *data) {

	// an action to be executed every time a new client connects to the cerver

}

static void *arcade_game_start (void *data) {

	// method to start the arcade game
	// here you can init some data or load additional resources

	return NULL;

}

static void *arcade_game_end (void *data) {

	// method to end the arcade game
	// here you can destroy all the data that you created for the game

	return NULL;

}

static void arcade_game_join (void *data) {

	// this method gets executed every time a new player joins the lobby
	// here you can create a new player for example

}

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	signal (SIGINT, my_game_end);

	if (!my_game_init ()) {
		my_cerver = cerver_create (GAME_CERVER, "game-cerver", 8007, PROTOCOL_TCP, false, 2, 2000);
		if (my_cerver) {
			/*** cerver configuration ***/
			cerver_set_receive_buffer_size (my_cerver, 16384);
			cerver_set_thpool_n_threads (my_cerver, 4);
			cerver_set_app_handlers (my_cerver, my_game_packet_handler, NULL);
			cerver_set_on_client_connected (my_cerver, my_game_on_client_connected);

			/*** game configuration ***/
			GameCerver *game_cerver = (GameCerver *) my_cerver->cerver_data;

			// register the arcade game type
			GameType *arcade_game_type = game_type_create ("arcade", NULL, NULL, 
				arcade_game_start, arcade_game_end);
			game_type_add_lobby_config (arcade_game_type, true, NULL, 4);
			game_type_set_on_lobby_join (arcade_game_type, arcade_game_join);
			game_type_register (game_cerver->game_types, arcade_game_type);

			if (!cerver_start (my_cerver)) {
				cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
					"Failed to start magic cerver!");
			}
		}

		else {
			cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE, 
				"Failed to create game cerver!");
		}

		my_game_end (0);
	}

	else {
		cerver_log_msg (stderr, LOG_ERROR, LOG_NO_TYPE,
			"Failed to init my game!");
	}

	return 0;

}