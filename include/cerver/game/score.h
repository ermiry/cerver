#ifndef _CERVER_GAME_SCORE_H_
#define _CERVER_GAME_SCORE_H_

#include "cerver/config.h"

#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

// the players that a scoreboard has room for when it is created
#define SCORE_DEFAULT_SIZE		5

#ifdef __cplusplus
extern "C" {
#endif

struct _Lobby;

// the scores of each player by score type, the values are clamped to 0
typedef struct ScoreBoard {

    Htab *scores;               // player name -> Htab (score type -> i32)
    u8 registeredPlayers;
    char **playerNames;         // to apply a new or a removed score type to every player
    u8 scoresNum;
    char **scoreTypes;

} ScoreBoard;

// scoreboard constructor
CERVER_EXPORT ScoreBoard *game_score_new (void);

// creates a scoreboard with scoresNum score types (char *) as the variable arguments
CERVER_EXPORT ScoreBoard *game_score_create (unsigned int playersNum, unsigned int scoresNum, ...);

CERVER_EXPORT void game_score_delete (ScoreBoard *sb);

// adds a score type with 0 to every player, an existing one is ignored
CERVER_EXPORT void game_score_add_scoreType (ScoreBoard *sb, char *newScore);

// adds the players of the lobby by their ids, the players without an id are skipped
CERVER_EXPORT void game_score_add_lobby_players (ScoreBoard *sb, const struct _Lobby *lobby);

// removes a score type from every player
// returns 0 on success, 1 if the score type does not exist
CERVER_EXPORT u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore);

// adds a player with every score type in 0
// returns 0 on success, 1 if the player already exists
CERVER_EXPORT u8 game_score_add_player (ScoreBoard *sb, char *playerName);

// returns 0 on success, 1 if the player does not exist
CERVER_EXPORT u8 game_score_remove_player (ScoreBoard *sb, char *playerName);

// sets the score's value, a negative one is set as 0
CERVER_EXPORT void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);

// returns the value of the player's score type, -1 if it does not exist
CERVER_EXPORT i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType);

// adds the value (that can be negative) to the score, the result is clamped to 0
CERVER_EXPORT void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);

// sets every score of the player to 0
CERVER_EXPORT void game_score_reset (ScoreBoard *sb, char *playerName);

#ifdef __cplusplus
}
#endif

#endif
