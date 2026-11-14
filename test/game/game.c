#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/dlist.h>

#include <cerver/core/cerver.h>

#include <cerver/game/game.h>
#include <cerver/game/lobby.h>
#include <cerver/game/player.h>
#include <cerver/game/score.h>
#include <cerver/game/types.h>

#include "../test.h"
#include "game.h"

// the game module without network: scores, game types, players and lobbies
// bugs this suite fixed:
// - the scores used the size of a pointer as the length of every key, and pointers to stack values
// - adding a score type was ignored and removing one leaked, the scoreboard itself leaked
// - game_score_update () added the value twice, and game_score_add_player () always failed
// - game_type_get_with_name () returned the last game type when none matched
// - game_type_unregister () used the element that it had just released
// - a second player_set_id () leaked the first id

#define GAME_TEST_PLAYERS			2
#define GAME_TEST_SCORES			2

#define GAME_TEST_VALUE				5
#define GAME_TEST_NEGATIVE			-3
#define GAME_TEST_INCREMENT			7
#define GAME_TEST_BIG_DECREMENT		-100
#define GAME_TEST_MAX_PLAYERS		4

// two names that share their first 8 bytes, a key with the size of a pointer can not tell them apart
static char first_player[] = { "long-player-name-1" };
static char second_player[] = { "long-player-name-2" };
static char missing_player[] = { "missing-player" };

static char kills[] = { "kills" };
static char deaths[] = { "deaths" };
static char assists[] = { "assists" };

static const char *first_type = { "deathmatch" };
static const char *second_type = { "capture" };
static const char *missing_type = { "missing" };

#pragma region scores

static ScoreBoard *test_game_scoreboard (void) {

	ScoreBoard *sb = game_score_create (GAME_TEST_PLAYERS, GAME_TEST_SCORES, kills, deaths);
	test_check_ptr (sb);
	test_check_unsigned_eq (sb->scoresNum, GAME_TEST_SCORES, NULL);

	test_check_unsigned_eq (game_score_add_player (sb, first_player), 0, NULL);
	test_check_unsigned_eq (game_score_add_player (sb, second_player), 0, NULL);
	test_check_unsigned_eq (sb->registeredPlayers, GAME_TEST_PLAYERS, NULL);

	return sb;

}

static void test_game_scores_values (void) {

	ScoreBoard *sb = test_game_scoreboard ();

	test_check_int_eq (game_score_get (sb, first_player, kills), 0, NULL);

	game_score_set (sb, first_player, kills, GAME_TEST_VALUE);
	test_check_int_eq (game_score_get (sb, first_player, kills), GAME_TEST_VALUE, NULL);
	test_check_int_eq (game_score_get (sb, second_player, kills), 0, "the players share their scores");

	game_score_set (sb, first_player, kills, GAME_TEST_NEGATIVE);
	test_check_int_eq (game_score_get (sb, first_player, kills), 0, NULL);

	game_score_update (sb, first_player, deaths, GAME_TEST_INCREMENT);
	test_check_int_eq (game_score_get (sb, first_player, deaths), GAME_TEST_INCREMENT, NULL);

	game_score_update (sb, first_player, deaths, GAME_TEST_BIG_DECREMENT);
	test_check_int_eq (game_score_get (sb, first_player, deaths), 0, NULL);

	game_score_set (sb, second_player, kills, GAME_TEST_VALUE);
	game_score_set (sb, second_player, deaths, GAME_TEST_VALUE);
	game_score_reset (sb, second_player);
	test_check_int_eq (game_score_get (sb, second_player, kills), 0, NULL);
	test_check_int_eq (game_score_get (sb, second_player, deaths), 0, NULL);

	test_check_int_eq (game_score_get (sb, missing_player, kills), -1, NULL);
	test_check_int_eq (game_score_get (sb, first_player, assists), -1, NULL);

	game_score_delete (sb);

}

static void test_game_scores_players (void) {

	ScoreBoard *sb = test_game_scoreboard ();

	test_check_unsigned_eq (game_score_add_player (sb, first_player), 1, "a player was added twice");

	test_check_unsigned_eq (game_score_remove_player (sb, first_player), 0, NULL);
	test_check_unsigned_eq (sb->registeredPlayers, GAME_TEST_PLAYERS - 1, NULL);
	test_check_int_eq (game_score_get (sb, first_player, kills), -1, NULL);
	test_check_int_eq (game_score_get (sb, second_player, kills), 0, NULL);

	test_check_unsigned_eq (game_score_remove_player (sb, first_player), 1, NULL);

	game_score_delete (sb);

}

static void test_game_scores_types (void) {

	ScoreBoard *sb = test_game_scoreboard ();

	game_score_add_scoreType (sb, assists);
	test_check_unsigned_eq (sb->scoresNum, GAME_TEST_SCORES + 1, NULL);
	test_check_int_eq (game_score_get (sb, first_player, assists), 0, "the new score type was not added to the players");

	game_score_add_scoreType (sb, assists);
	test_check_unsigned_eq (sb->scoresNum, GAME_TEST_SCORES + 1, "a score type was added twice");

	test_check_unsigned_eq (game_score_remove_scoreType (sb, kills), 0, NULL);
	test_check_unsigned_eq (sb->scoresNum, GAME_TEST_SCORES, NULL);
	test_check_int_eq (game_score_get (sb, first_player, kills), -1, NULL);
	test_check_int_eq (game_score_get (sb, first_player, deaths), 0, NULL);

	test_check_unsigned_eq (game_score_remove_scoreType (sb, kills), 1, NULL);

	game_score_delete (sb);

}

#pragma endregion

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

	test_check_ptr_eq (game_type_get_with_name (game_types, first_type), first);
	test_check_ptr_eq (game_type_get_with_name (game_types, second_type), second);
	test_check_null_ptr (game_type_get_with_name (game_types, missing_type));

	test_check_int_eq (game_type_unregister (game_types, first_type), 0, NULL);
	test_check_null_ptr (game_type_get_with_name (game_types, first_type));
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

	test_check_int_eq (player_comparator_with_id (first, second), 0, NULL);

	player_set_id (second, first_player);
	test_check (player_comparator_with_id (first, second) != 0, NULL);

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
	test_check_unsigned_eq (dlist_size (game->current_lobbys), 2, "a lobby was registered twice");

	Lobby *query = lobby_new ();
	query->id = str_new (second_type);
	test_check_ptr_eq (lobby_get (game, query), second);
	lobby_delete (query);

	game_cerver_unregister_lobby (game, second);
	test_check_unsigned_eq (dlist_size (game->current_lobbys), 1, NULL);
	lobby_delete (second);

	game_delete (game);
	cerver_delete (cerver);

}

#pragma endregion

int main (int argc, char **argv) {

	test_output_init ();

	(void) argc;
	(void) argv;

	(void) printf ("Testing GAME...\n");

	test_game_scores_values ();
	test_game_scores_players ();
	test_game_scores_types ();

	test_game_types ();
	test_game_players ();
	test_game_lobbies ();

	cerver_init ();

	game_tests_lobbies ();
	game_tests_requests ();

	cerver_end ();

	(void) printf ("\nDone with GAME tests!\n\n");

	return 0;

}

