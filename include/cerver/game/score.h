#ifndef _CERVER_GAME_SCORE_H_
#define _CERVER_GAME_SCORE_H_

#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

#define DEFAULT_SCORE_SIZE      5   // default players inside the scoreboard

typedef struct ScoreBoard {

	Htab *scores;
	u8 registeredPlayers;
	u8 scoresNum;
	char **scoreTypes;

} ScoreBoard;

// scoreboard constructor
extern ScoreBoard *game_score_new (void);
extern ScoreBoard *game_score_create (u8 playersNum, u8 scoresNum, ...);
extern void game_score_delete (ScoreBoard *sb);

extern void game_score_add_scoreType (ScoreBoard *sb, char *newScore);
extern void game_score_add_lobby_players (ScoreBoard *sb, AVLNode *playerNode);
extern u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore);

extern u8 game_score_add_player (ScoreBoard *sb, char *playerName);
extern u8 game_score_remove_player (ScoreBoard *sb, char *playerName);

extern void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);
extern i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType);
extern void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);
extern void game_score_reset (ScoreBoard *sb, char *playerName);

#endif