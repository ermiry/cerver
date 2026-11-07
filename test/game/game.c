#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/cerver.h>

#include <cerver/collections/dlist.h>

#include <cerver/game/game.h>
#include <cerver/game/gametype.h>
#include <cerver/game/lobby.h>
#include <cerver/game/player.h>

#include "../test.h"

// the game module without network: game types, players and lobbies
// bugs this suite pinned:
// - game_type_get_by_name () returned the last game type when none matched
// - game_type_unregister () used the element that it had just released
// - a second player_set_id () leaked the first id
// - game_cerver_unregister_lobby () with a lobby that was not registered removed the first one

#define GAME_TEST_VALUE				5
#define GAME_TEST_MAX_PLAYERS		4
#define GAME_TEST_LOBBIES			2

static char first_player[] = { "long-player-name-1" };
static char second_player[] = { "long-player-name-2" };

static const char *first_type = { "deathmatch" };
static const char *second_type = { "capture" };
static const char *missing_type = { "missing" };

#pragma region types

static void test_game_types (void) {

	DoubleList *game_types = dlist_init (game_type_delete, NULL);
	test_check_ptr (game_types);

	GameType *first = game_type_create (first_type, NULL, NULL, NULL, NULL);
	GameType *second = game_type_create (second_type, NULL, NULL, NULL, NULL);
	test_check_ptr (first);
	test_check_ptr (second);

	test_check_int_eq (game_type_add_lobby_config (second, false, NULL, GAME_TEST_MAX_PLAYERS), 0, NULL);
	test_check_bool_eq (second->use_default_handler, false, NULL);
	test_check_unsigned_eq (second->max_players, GAME_TEST_MAX_PLAYERS, NULL);

	test_check_int_eq (game_type_register (game_types, first), 0, NULL);
	test_check_int_eq (game_type_register (game_types, second), 0, NULL);

	test_check_ptr_eq (game_type_get_by_name (game_types, first_type), first);
	test_check_ptr_eq (game_type_get_by_name (game_types, second_type), second);
	test_check_null_ptr (game_type_get_by_name (game_types, missing_type));

	test_check_int_eq (game_type_unregister (game_types, first_type), 0, NULL);
	test_check_null_ptr (game_type_get_by_name (game_types, first_type));
	test_check_int_eq (game_type_unregister (game_types, missing_type), 1, NULL);

	dlist_delete (game_types);

}

#pragma endregion

#pragma region players

static void test_game_players (void) {

	Player *first = player_new ();
	Player *second = player_new ();
	test_check_ptr (first);
	test_check_ptr (second);

	player_set_id (first, first_player);
	player_set_id (first, second_player);
	player_set_id (second, second_player);
	test_check_str_eq (first->id->str, second_player, NULL);

	test_check_int_eq (player_comparator_by_id (first, second), 0, NULL);

	player_set_id (second, first_player);
	test_check (player_comparator_by_id (first, second) != 0, NULL);

	player_set_data (first, malloc (GAME_TEST_VALUE), NULL);

	player_delete (first);
	player_delete (second);

}

#pragma endregion

#pragma region lobbies

static void test_game_lobbies (void) {

	Cerver *cerver = cerver_create (CERVER_TYPE_CUSTOM, "game-test", CERVER_DEFAULT_PORT, PROTOCOL_TCP, false, 1);
	test_check_ptr (cerver);

	GameCerver *game = game_new ();
	test_check_ptr (game);
	game_set_cerver_reference (game, cerver);

	Lobby *first = lobby_new ();
	Lobby *second = lobby_new ();
	test_check_ptr (first);
	test_check_ptr (second);

	first->id = str_new (first_type);
	second->id = str_new (second_type);

	lobby_set_game_settings (first, malloc (GAME_TEST_VALUE), NULL);
	lobby_set_game_data (first, malloc (GAME_TEST_VALUE), NULL);

	test_check (lobby_comparator (first, second) != 0, NULL);
	test_check_int_eq (lobby_comparator (first, first), 0, NULL);

	game_cerver_register_lobby (game, first);
	game_cerver_register_lobby (game, second);
	game_cerver_register_lobby (game, first);
	test_check_unsigned_eq (dlist_size (game->current_lobbys), GAME_TEST_LOBBIES, "a lobby was registered twice");

	Lobby *query = lobby_new ();
	query->id = str_new (second_type);
	test_check_ptr_eq (lobby_get (game, query), second);
	lobby_delete (query);

	// a lobby that is not registered must not remove the first one
	Lobby *missing = lobby_new ();
	missing->id = str_new (missing_type);
	game_cerver_unregister_lobby (game, missing);
	test_check_unsigned_eq (dlist_size (game->current_lobbys), GAME_TEST_LOBBIES, "a missing lobby removed another one");
	lobby_delete (missing);

	game_cerver_unregister_lobby (game, second);
	test_check_unsigned_eq (dlist_size (game->current_lobbys), GAME_TEST_LOBBIES - 1, NULL);
	lobby_delete (second);

	game_delete (game);
	cerver_delete (cerver);

}

#pragma endregion

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) printf ("Testing GAME...\n");

	test_game_types ();
	test_game_players ();
	test_game_lobbies ();

	(void) printf ("\nDone with GAME tests!\n\n");

	return 0;

}
