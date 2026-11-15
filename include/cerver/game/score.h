/**
 * @file cerver/game/score.h
 * @brief A scoreboard: the scores of each player by score type.
 */

#ifndef _CERVER_GAME_SCORE_H_
#define _CERVER_GAME_SCORE_H_

#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

#include "cerver/config.h"

/**
 * @defgroup game_score Scores
 * @ingroup game
 * @brief The scores of the players of a game.
 * @{
 */

/** @brief The players that a scoreboard has room for when it is created with game_score_new (). */
#define SCORE_DEFAULT_SIZE		5

#ifdef __cplusplus
extern "C" {
#endif

struct _Lobby;

/**
 * @brief The scores of each player by score type; the values are clamped to 0.
 *
 * @warning The counts are u8: more than 255 players or score types wrap around.
 */
typedef struct ScoreBoard {

    /** @brief The name of each player to a Htab of its scores (score type to i32). */
    Htab *scores;
    /** @brief The number of players. */
    u8 registeredPlayers;
    /** @brief The names of the players, to apply a new or a removed score type to every player. */
    char **playerNames;
    /** @brief The number of score types. */
    u8 scoresNum;
    /** @brief The score types. */
    char **scoreTypes;

} ScoreBoard;

/**
 * @brief Creates an empty scoreboard.
 *
 * @return the new scoreboard, released with game_score_delete (); NULL if it could not be allocated
 */
CERVER_EXPORT ScoreBoard *game_score_new (void);

/**
 * @brief Creates a scoreboard with its score types.
 *
 * @param[in] playersNum the players that it has room for at first
 * @param[in] scoresNum the number of score types that follow
 * @param[in] ... the score types, as char *, copied
 * @return the new scoreboard, released with game_score_delete (); NULL if it could not be allocated
 */
CERVER_EXPORT ScoreBoard *game_score_create (unsigned int playersNum, unsigned int scoresNum, ...);

/**
 * @brief Releases a scoreboard with its players and its score types.
 *
 * @param[in] sb the scoreboard, it can be NULL
 */
CERVER_EXPORT void game_score_delete (ScoreBoard *sb);

/**
 * @brief Adds a score type with 0 to every player; an existing one is ignored.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] newScore the score type, copied
 */
CERVER_EXPORT void game_score_add_scoreType (ScoreBoard *sb, char *newScore);

/**
 * @brief Adds the players of a lobby by their ids, with the lock of its game cerver; the players without an id are
 * skipped.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] lobby the lobby
 */
CERVER_EXPORT void game_score_add_lobby_players (ScoreBoard *sb, const struct _Lobby *lobby);

/**
 * @brief Removes a score type from every player.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] oldScore the score type
 * @retval 0 the score type was removed
 * @retval 1 a NULL argument or the score type does not exist
 */
CERVER_EXPORT u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore);

/**
 * @brief Adds a player with every score type in 0.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] playerName the name, copied
 * @retval 0 the player was added
 * @retval 1 a NULL argument, the player already exists or it could not be allocated
 */
CERVER_EXPORT u8 game_score_add_player (ScoreBoard *sb, char *playerName);

/**
 * @brief Removes a player and its scores.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] playerName the name
 * @retval 0 the player was removed
 * @retval 1 a NULL argument or the player does not exist
 */
CERVER_EXPORT u8 game_score_remove_player (ScoreBoard *sb, char *playerName);

/**
 * @brief Sets the value of a score of a player; a negative one is set as 0.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] playerName the name
 * @param[in] scoreType the score type
 * @param[in] value the value
 */
CERVER_EXPORT void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);

/**
 * @brief Returns the value of a score of a player.
 *
 * @param[in] sb the scoreboard
 * @param[in] playerName the name
 * @param[in] scoreType the score type
 * @return the value, -1 if the player or the score type does not exist
 */
CERVER_EXPORT i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType);

/**
 * @brief Adds a value, that can be negative, to a score of a player; the result is clamped to 0.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] playerName the name
 * @param[in] scoreType the score type
 * @param[in] value the value to add
 */
CERVER_EXPORT void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);

/**
 * @brief Sets every score of a player to 0.
 *
 * @param[in,out] sb the scoreboard
 * @param[in] playerName the name
 */
CERVER_EXPORT void game_score_reset (ScoreBoard *sb, char *playerName);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
