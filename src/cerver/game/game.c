#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <poll.h>
#include <errno.h>

#include "cerver/types/types.h"

#include "cerver/game/game.h"
#include "cerver/game/player.h"
#include "cerver/game/lobby.h"

#include "cerver/collections/dllist.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

GameCerver *game_new (void) {

    GameCerver *game = (GameCerver *) malloc (sizeof (GameCerver));
    if (game) {
        game->current_lobbys = dlist_init (lobby_delete, lobby_comparator);

        game->lobby_id_generator = NULL;
        game->player_comparator = NULL;

        game->load_game_data = NULL;
        game->delete_game_data = NULL;
        game->game_data = NULL;

        game->final_game_action = NULL;
        game->final_action_args = NULL;
    }

    return game;

}

void game_delete (void *game_ptr) {

    if (game_ptr) {
        GameCerver *game = (GameCerver *) game_ptr;

        dlist_destroy (game->current_lobbys);

        if (game->game_data) {
            if (game->delete_game_data)
                game->delete_game_data (game->game_data);
            else free (game->game_data);
        }

        free (game);
    }

}

/*** Game Cerver configuration ***/

// option to set the game cerver lobby id generator
void game_set_lobby_id_generator (GameCerver *game_cerver, void *(*lobby_id_generator) (const void *)) {

    if (game_cerver) game_cerver->lobby_id_generator = lobby_id_generator;

}

// option to set the game cerver comparator
void game_set_player_comparator (GameCerver *game_cerver, Comparator player_comparator) {

    if (game_cerver) game_cerver->player_comparator = player_comparator;

}

// sets a way to get and destroy game cerver game data
void game_set_load_game_data (GameCerver *game_cerver, 
    Action load_game_data, Action delete_game_data) {

    if (game_cerver) {
        game_cerver->load_game_data = load_game_data;
        game_cerver->delete_game_data = delete_game_data;
    }

}

// option to set an action to be performed right before the game cerver teardown
// eg. send a message to all players
void game_set_final_action (GameCerver *game_cerver, 
    Action final_action, void *final_action_args) {

    if (game_cerver) {
        game_cerver->final_game_action = final_action;
        game_cerver->final_action_args = final_action_args;
    }

}

// handles a game type packet
void game_packet_handler (Packet *packet) {

    if (packet) {
        if (packet->packet_size >= (sizeof (PacketHeader) + sizeof (RequestData))) {
            char *end = packet->packet;
            RequestData *req = (RequestData *) (end += sizeof (PacketHeader));

            switch (req->type) {
                case LOBBY_CREATE: break;

                case LOBBY_JOIN: break;

                case LOBBY_LEAVE: break;

                case LOBBY_UPDATE: break;

                case LOBBY_DESTROY: break;

                default: {
                    #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, LOG_WARNING, LOG_GAME, 
                        "game_packet_hanlder () -- got a packet with unknwon request.");
                    #endif
                } break;
            }
        }
    }

}