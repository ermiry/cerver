#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

#include "cerver/types/types.h"

#include "cerver/game/score.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

// a score manager for any game: player name -> (score type -> value)
// the names are keys with their '\0', every value is allocated and owned by its player's table

#pragma region values

static size_t game_score_key_size (const char *key) {

	return strlen (key) + 1;

}

static i32 game_score_clamp (const i64 value) {

	return (value > 0) ? (i32) value : 0;

}

static void game_score_player_delete (void *scores_ptr) {

	htab_destroy ((Htab *) scores_ptr);

}

static Htab *game_score_player_scores (const ScoreBoard *sb, const char *playerName) {

	return (Htab *) htab_get (sb->scores, playerName, game_score_key_size (playerName));

}

static i32 *game_score_value (const ScoreBoard *sb, const char *playerName, const char *scoreType) {

	Htab *scores = game_score_player_scores (sb, playerName);

	return scores ? (i32 *) htab_get (scores, scoreType, game_score_key_size (scoreType)) : NULL;

}

// adds a score type with 0 to the scores of a player
static void game_score_value_add (Htab *scores, const char *scoreType) {

	i32 *value = (i32 *) calloc (1, sizeof (i32));
	if (value && htab_insert (scores, scoreType, game_score_key_size (scoreType), value, sizeof (i32))) {
		free (value);
	}

}

static int game_score_type_idx (const ScoreBoard *sb, const char *scoreType) {

	for (int i = 0; i < sb->scoresNum; i++) {
		if (!strcmp (sb->scoreTypes[i], scoreType)) return i;
	}

	return -1;

}

static int game_score_player_idx (const ScoreBoard *sb, const char *playerName) {

	for (int i = 0; i < sb->registeredPlayers; i++) {
		if (!strcmp (sb->playerNames[i], playerName)) return i;
	}

	return -1;

}

#pragma endregion

#pragma region main

ScoreBoard *game_score_new (void) {

	ScoreBoard *sb = (ScoreBoard *) calloc (1, sizeof (ScoreBoard));
	if (sb) {
		sb->scores = htab_create (DEFAULT_SCORE_SIZE, NULL, game_score_player_delete);
	}

	return sb;

}

ScoreBoard *game_score_create (u8 playersNum, u8 scoresNum, ...) {

	ScoreBoard *sb = (ScoreBoard *) calloc (1, sizeof (ScoreBoard));
	if (sb) {
		sb->scores = htab_create (playersNum, NULL, game_score_player_delete);
		sb->scoreTypes = (char **) calloc (scoresNum ? scoresNum : 1, sizeof (char *));

		va_list valist;
		va_start (valist, scoresNum);

		for (int i = 0; i < scoresNum; i++) {
			sb->scoreTypes[i] = strdup (va_arg (valist, char *));
		}

		va_end (valist);

		sb->scoresNum = scoresNum;
	}

	return sb;

}

void game_score_delete (ScoreBoard *sb) {

	if (sb) {
		if (sb->scores) htab_destroy (sb->scores);

		for (int i = 0; i < sb->scoresNum; i++) free (sb->scoreTypes[i]);
		free (sb->scoreTypes);

		for (int i = 0; i < sb->registeredPlayers; i++) free (sb->playerNames[i]);
		free (sb->playerNames);

		free (sb);
	}

}

#pragma endregion

#pragma region types

void game_score_add_scoreType (ScoreBoard *sb, char *newScore) {

	if (!sb || !newScore || (game_score_type_idx (sb, newScore) >= 0)) return;

	char **types = (char **) realloc (sb->scoreTypes, (sb->scoresNum + 1) * sizeof (char *));
	if (!types) return;

	sb->scoreTypes = types;
	sb->scoreTypes[sb->scoresNum] = strdup (newScore);
	sb->scoresNum += 1;

	for (int i = 0; i < sb->registeredPlayers; i++) {
		game_score_value_add (game_score_player_scores (sb, sb->playerNames[i]), newScore);
	}

}

// FIXME: how do we get the player name?? the server is not interested in knowing a playe's name
// inside his player structure, it is the admin reponsability to determine how to get
// the player's name
void game_score_add_lobby_players (ScoreBoard *sb, AVLNode *playerNode) {

	if (sb && playerNode) {
		game_score_add_lobby_players (sb, playerNode->right);

		// if (playerNode->id) game_score_add_player (sb, ((Player *) playerNode->id));

		game_score_add_lobby_players (sb, playerNode->left);
	}

}

u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore) {

	const int idx = (sb && oldScore) ? game_score_type_idx (sb, oldScore) : -1;
	if (idx < 0) return 1;

	for (int i = 0; i < sb->registeredPlayers; i++) {
		Htab *scores = game_score_player_scores (sb, sb->playerNames[i]);
		free (htab_remove (scores, oldScore, game_score_key_size (oldScore)));
	}

	free (sb->scoreTypes[idx]);

	for (int i = idx; i < (sb->scoresNum - 1); i++) sb->scoreTypes[i] = sb->scoreTypes[i + 1];
	sb->scoresNum -= 1;

	return 0;

}

#pragma endregion

#pragma region players

u8 game_score_add_player (ScoreBoard *sb, char *playerName) {

	if (!sb || !playerName || game_score_player_scores (sb, playerName)) return 1;

	char **names = (char **) realloc (sb->playerNames, (sb->registeredPlayers + 1) * sizeof (char *));
	if (!names) return 1;
	sb->playerNames = names;

	Htab *scores = htab_create (sb->scoresNum, NULL, free);
	if (!scores) return 1;

	for (int i = 0; i < sb->scoresNum; i++) game_score_value_add (scores, sb->scoreTypes[i]);

	if (htab_insert (sb->scores, playerName, game_score_key_size (playerName), scores, sizeof (Htab))) {
		htab_destroy (scores);
		return 1;
	}

	sb->playerNames[sb->registeredPlayers] = strdup (playerName);
	sb->registeredPlayers += 1;

	return 0;

}

u8 game_score_remove_player (ScoreBoard *sb, char *playerName) {

	const int idx = (sb && playerName) ? game_score_player_idx (sb, playerName) : -1;
	if (idx < 0) return 1;

	Htab *scores = (Htab *) htab_remove (sb->scores, playerName, game_score_key_size (playerName));
	if (scores) htab_destroy (scores);

	free (sb->playerNames[idx]);

	for (int i = idx; i < (sb->registeredPlayers - 1); i++) sb->playerNames[i] = sb->playerNames[i + 1];
	sb->registeredPlayers -= 1;

	return 0;

}

#pragma endregion

#pragma region scores

void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value) {

	i32 *current = (sb && playerName && scoreType) ? game_score_value (sb, playerName, scoreType) : NULL;

	if (current) *current = game_score_clamp (value);

}

i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType) {

	const i32 *current = (sb && playerName && scoreType) ? game_score_value (sb, playerName, scoreType) : NULL;

	return current ? *current : -1;

}

void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value) {

	i32 *current = (sb && playerName && scoreType) ? game_score_value (sb, playerName, scoreType) : NULL;

	if (current) *current = game_score_clamp ((i64) *current + (i64) value);

}

void game_score_reset (ScoreBoard *sb, char *playerName) {

	if (!sb || !playerName) return;

	for (int i = 0; i < sb->scoresNum; i++) {
		i32 *current = game_score_value (sb, playerName, sb->scoreTypes[i]);
		if (current) *current = 0;
	}

}

#pragma endregion
