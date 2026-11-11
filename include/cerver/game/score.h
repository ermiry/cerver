#ifndef _CERVER_GAME_SCORE_H_
#define _CERVER_GAME_SCORE_H_

#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

// the players that a scoreboard has room for when it is created
#define SCORE_DEFAULT_SIZE		5

#ifdef __cplusplus
extern "C" {
#endif

// the scores of each player by score type, the values are clamped to 0
typedef struct ScoreBoard {

    Htab *scores;               // player name -> Htab (score type -> i32)
    u8 registeredPlayers;
    char **playerNames;         // to apply a new or a removed score type to every player
    u8 scoresNum;
    char **scoreTypes;

} ScoreBoard;

// scoreboard constructor
extern ScoreBoard *game_score_new (void);

// creates a scoreboard with scoresNum score types (char *) as the variable arguments
extern ScoreBoard *game_score_create (u8 playersNum, u8 scoresNum, ...);

extern void game_score_delete (ScoreBoard *sb);

// adds a score type with 0 to every player, an existing one is ignored
extern void game_score_add_scoreType (ScoreBoard *sb, char *newScore);

extern void game_score_add_lobby_players (ScoreBoard *sb, AVLNode *playerNode);

// removes a score type from every player
// returns 0 on success, 1 if the score type does not exist
extern u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore);

// adds a player with every score type in 0
// returns 0 on success, 1 if the player already exists
extern u8 game_score_add_player (ScoreBoard *sb, char *playerName);

// returns 0 on success, 1 if the player does not exist
extern u8 game_score_remove_player (ScoreBoard *sb, char *playerName);

// sets the score's value, a negative one is set as 0
extern void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);

// returns the value of the player's score type, -1 if it does not exist
extern i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType);

// adds the value (that can be negative) to the score, the result is clamped to 0
extern void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);

// sets every score of the player to 0
extern void game_score_reset (ScoreBoard *sb, char *playerName);

#ifdef __cplusplus
}
#endif

#endif
