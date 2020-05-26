#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <stdarg.h>

#include "cerver/types/types.h"

#include "cerver/game/score.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

// 15/11/2018 - starting to implement an easy to use score manager for any porpouse game
// implementing a C# Dictionary <string, Dictionary <string, int>> using hastables

// FIXME: 17/11/2018 -- we need a way of retrieving the scores in order!!

// FIXME: we need to sreliaze and send the scores to the players in the best way

// FIXME: we still need to fix some functions...

// scoreboard constructor
ScoreBoard *game_score_new (void) {

    ScoreBoard *sb = (ScoreBoard *) malloc (sizeof (ScoreBoard));
    if (sb) memset (sb, 0, sizeof (ScoreBoard));
    return sb;

}

// 16/11/2018 -- we can now create a scoreboard and pass variable num of score types
// to be added each time we add a new player!!
ScoreBoard *game_score_create (u8 playersNum, u8 scoresNum, ...) {

    ScoreBoard *sb = (ScoreBoard *) malloc (sizeof (ScoreBoard));
    if (sb) {
        va_list valist;
        va_start (valist, scoresNum);

        sb->scoresNum = scoresNum;
        sb->scoreTypes = (char **) calloc (scoresNum, sizeof (char *));
        char *temp = NULL;
        for (int i = 0; i < scoresNum; i++) {
            temp = va_arg (valist, char *);
            sb->scoreTypes[i] = (char *) calloc (strlen (temp) + 1, sizeof (char));
            strcpy (sb->scoreTypes[i], temp);
        }

        sb->registeredPlayers = 0;
        sb->scores = htab_init (playersNum > 0 ? playersNum : DEFAULT_SCORE_SIZE,
            NULL, NULL, NULL, false, NULL, NULL);

        va_end (valist);
    }

    return sb;

}

// destroy the scoreboard
void game_score_delete (ScoreBoard *sb) {

    if (sb) {
        if (sb->scores) htab_destroy (sb->scores);
        if (sb->scoreTypes) {
            for (int i = 0; i < sb->scoresNum; i++) 
                if (sb->scoreTypes[i]) free (sb->scoreTypes[i]);
            
        }
    }

}

// FIXME:
// add a new score type to all current players
void game_score_add_scoreType (ScoreBoard *sb, char *newScore) {

    if (sb && newScore) {
        // first check if we have that score type
        bool found = false;
        for (int i = 0; i < sb->scoresNum; i++) {
            if (!strcmp (sb->scoreTypes[i], newScore)) {
                found = true;
                break;
            }
        }

        if (found) {
            // add the new score to all the players
            // FIXME:

            // add the score to the scoretypes
            sb->scoreTypes = (char **) realloc (sb->scores, sb->scoresNum);
            if (sb->scoreTypes) {
                sb->scoreTypes[sb->scoresNum] = (char *) calloc (strlen (newScore) + 1, sizeof (char));
                strcpy (sb->scoreTypes[sb->scoresNum], newScore);
                sb->scoresNum++;
            } 
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, 
                "Can't add score type! It already exists in the scoreboard.");
            #endif
        }
    }

}

// FIXME:
// remove a score type from all current players
u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore) {

    u8 retval = 1;

    if (sb && oldScore) {
        // first check if we have that score type
        int found = -1;
        for (int i = 0; i < sb->scoresNum; i++) {
            if (!strcmp (sb->scoreTypes[i], oldScore)) {
                found = i;
                break;
            }
        }

        if (found > 0) {
            // remove the score from all the players
            // FIXME:

            // manually copy old scores array and delete it
            // this is done because we allocate exat space for each score type!
            char **new_scoreTypes = (char **) calloc (sb->scoresNum - 1, sizeof (char *));
            int i = 0;
            while (i < found) {
                new_scoreTypes[i] = (char *) calloc (strlen (sb->scoreTypes[i]) + 1, sizeof (char));
                strcpy (new_scoreTypes[i], sb->scoreTypes[i]);
                i++;
            }

            for (i = found + 1; i < sb->scoresNum; i++) {
                new_scoreTypes[i - 1] = (char *) calloc (strlen (sb->scoreTypes[i]) + 1, sizeof (char));
                strcpy (new_scoreTypes[i - 1], sb->scoreTypes[i]);
            }

            sb->scoresNum--;

            retval = 0;
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Can't remove %s scoretype, doesn't exist in the scoreboard!");
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
        }
    }

    return retval;

}

// adds a new player to the score table
u8 game_score_add_player (ScoreBoard *sb, char *playerName) {

    if (sb && playerName) {
        if (htab_contains_key (sb->scores, playerName, sizeof (playerName))) {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Scores table already contains player: %s", playerName);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
            return 1;
        }

        // associate the player with his own scores dictionary
        else {
            Htab *newHt = htab_init (sb->scoresNum, NULL, NULL, NULL, false, NULL, NULL);
            if (newHt) {
                if (!htab_insert (sb->scores, playerName, sizeof (playerName), newHt, sizeof (Htab))) {
                    // insert each score type in the player's dictionary
                    u32 zero = 0;
                    for (int i = 0; i < sb->scoresNum; i++) 
                        htab_insert (newHt, sb->scoreTypes[i], 
                            sizeof (sb->scoreTypes[i]), &zero, sizeof (int));

                    sb->registeredPlayers++;

                    return 0;   // LOG_SUCCESS adding new player and its scores
                }
            }
        }
    }

    return 1;

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

// removes a player from the score table
u8 game_score_remove_player (ScoreBoard *sb, char *playerName) {

    if (sb && playerName) {
        if (htab_contains_key (sb->scores, playerName, sizeof (playerName))) {
            size_t htab_size = sizeof (Htab);
            void *htab = htab_get_data (sb->scores, playerName, sizeof (playerName));
            
            // destroy player's scores htab
            if (htab) htab_destroy ((Htab *) htab);

            // remove th player from the scoreboard
            htab_remove (sb->scores, playerName, sizeof (playerName));

            sb->registeredPlayers--;
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Scores table doesn't contains player: %s", playerName);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
        }
    }

    return 1;   // error

}

// sets the score's value, negative values not allowed
void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value) {

    if (sb && playerName && scoreType) {
        size_t htab_size = sizeof (Htab);
        void *playerScores = htab_get_data (sb->scores, playerName, sizeof (playerName));
        if (playerScores) {
            size_t int_size = sizeof (unsigned int);
            void *currValue = htab_get_data ((Htab *) playerScores, 
                scoreType, sizeof (scoreType));

            // replace the old value with the new one
            if (currValue) {
                u32 *current = (u32 *) currValue;
                if (value > 0) *current = value;
                else *current = 0;
            }
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Player: %s has not scores in the table!", playerName);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
        }
    }

}

// gets the value of the player's score type
i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType) {

    if (sb && playerName && scoreType) {
        size_t htab_size = sizeof (Htab);
        void *playerScores = htab_get_data (sb->scores, playerName, sizeof (playerName));
        if (playerScores) {
            size_t int_size = sizeof (unsigned int);
            void *value = htab_get_data ((Htab *) playerScores, 
                scoreType, sizeof (scoreType));

            if (value) {
                u32 *retval = (u32 *) value;
                return *retval;
            }
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Player: %s has not scores in the table!", playerName);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
        }
    }

    return -1;  // error

}

// updates the value of the player's score type, clamped to 0
void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value) {

    if (sb && playerName && scoreType) {
        size_t htab_size = sizeof (Htab);
        void *playerScores = htab_get_data (sb->scores, playerName, sizeof (playerName));
        if (playerScores) {
            size_t int_size = sizeof (unsigned int);
            void *currValue = htab_get_data ((Htab *) playerScores, 
                scoreType, sizeof (scoreType));
            
            // directly update the score value adding the new value
            if (value) {
                u32 *current = (u32 *) currValue;
                if ((*current += value) < 0) *current = 0;
                else *current += value;
            }
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Player: %s has not scores in the table!", playerName);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
        }
    }

}

// reset all the player's scores
void game_score_reset (ScoreBoard *sb, char *playerName) {

    if (sb && playerName) {
        size_t htab_size = sizeof (Htab);
        void *data = htab_get_data (sb->scores, playerName, sizeof (playerName));
        if (data) {
            Htab *playerScores = (Htab *) data;
            void *scoreData = NULL;
            size_t score_size = sizeof (u32);
            for (int i = 0; i < sb->scoresNum; i++) {
                scoreData = htab_get_data (playerScores, 
                    sb->scoreTypes[i], sizeof (sb->scoreTypes[i]));

                if (scoreData) {
                    u32 *current = (u32 *) scoreData;
                    *current = 0;
                }
            }
        }

        else {
            #ifdef CERVER_DEBUG
            char *s = c_string_create ("Scores table already contains player: %s", playerName);
            if (s) {
                cerver_log_msg (stderr, LOG_ERROR, LOG_GAME, s);
                free (s);
            }
            #endif
        }
    }

}