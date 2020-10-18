#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/client.h"
#include "cerver/packets.h"
#include "cerver/game/game.h"
#include "cerver/game/player.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

ListElement *player_get_le_from_lobby (Lobby *lobby, Player *player);

Player *player_new (void) {

    Player *player = (Player *) malloc (sizeof (Player));
    if (player) {
        player->id = NULL;
        player->client = NULL;
        player->data = NULL;
        player->data_delete = NULL;
    }

    return player;

}

void player_delete (void *player_ptr) {

    if (player_ptr) {
        Player *player = (Player *) player_ptr;

        str_delete (player->id);

        player->client = NULL;

        if (player->data) {
            if (player->data_delete)
                player->data_delete (player->data);
            else free (player->data);
        }

        free (player);
    }

}

// sets the player id
void player_set_id (Player *player, const char *id) {

    if (player) player->id = str_new (id);

}

// sets player data and a way to delete it
void player_set_data (Player *player, void *data, Action data_delete) {

    if (player) {
        player->data = data;
        player->data_delete = data_delete;
    }

}

int player_comparator_by_id (const void *a, const void *b) {

    return str_compare (((Player *) a)->id, ((Player *) b)->id);

}

int player_comparator_client_id (const void *a, const void *b) {

    return client_comparator_client_id (((Player *) a)->client, ((Player *) b)->client);

}

// registers each of the player's client connections to the lobby poll structures
u8 player_register_to_lobby_poll (Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            lobby_poll_register_connection (lobby, player, connection);
        }
    }

    return retval;

}

// registers a player to the lobby --> add him to lobby's structures
u8 player_register_to_lobby (Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {
        if (player->client) {
            // bool failed = false;

            // // register all the player's client connections to the lobby poll
            // Connection *connection = NULL;
            // for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
            //     connection = (Connection *) le->data;
            //     failed = lobby_poll_register_connection (lobby, player, connection);
            // }

            // if (!failed) {
                dlist_insert_after (lobby->players, dlist_end (lobby->players), player);

                #ifdef CERVER_DEBUG
                char *s = c_string_create ("Registered a new player to lobby %s.",
                    lobby->id->str);
                if (s) {
                    cerver_log_msg (stdout, LOG_TYPE_SUCCESS, LOG_TYPE_PLAYER, s);
                    free (s);
                }
                #endif

                lobby->n_current_players++;
                #ifdef CERVER_STATS
                char *status = c_string_create ("Registered players to lobby %s: %i.",
                    lobby->id->str, lobby->n_current_players);
                if (status) {
                    cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_GAME, status);
                    free (status);
                }
                #endif

                retval = 0;
            // }
        }
    }

    return retval;

}

// unregisters each of the player's client connections from the lobby poll structures
u8 player_unregister_from_lobby_poll (Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
            connection = (Connection *) le->data;
            lobby_poll_unregister_connection (lobby, player, connection);
        }
    }

    return retval;

}

// FIXME: we need to be sure that if the one that we are removing is the owner, we need to find a new owner
// unregisters a player from a lobby --> removes him from lobby's structures
u8 player_unregister_from_lobby (Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {
        if (player->client) {
            // printf ("\nplayer_unregister_from_lobby client id: %li\n", player->client->id);
            if (lobby->default_handler) {
                // unregister all the player's client connections from the lobby
                player_unregister_from_lobby_poll (lobby, player);
            }

            if (!dlist_remove (lobby->players, player, NULL)) {
                lobby->n_current_players--;

                #ifdef CERVER_DEBUG
                char *s = c_string_create ("Unregistered a player from lobby %s.",
                    lobby->id->str);
                if (s) {
                    cerver_log_msg (stdout, LOG_TYPE_SUCCESS, LOG_TYPE_PLAYER, s);
                    free (s);
                }
                #endif

                #ifdef CERVER_STATS
                char *status = c_string_create ("Registered players to lobby %s: %i.",
                    lobby->id->str, lobby->n_current_players);
                if (status) {
                    cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_GAME, status);
                    free (status);
                }
                #endif

                // check if there are players left inside the lobby
                if (lobby->n_current_players <= 0) {
                    // destroy the lobby
                    #ifdef CERVER_DEBUG
                    char *s = c_string_create ("Destroying lobby %s -- it is empty.",
                        lobby->id->str);
                    if (s) {
                        cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_GAME, s);
                        free (s);
                    }
                    #endif

                    // teardown the cerver
                    Cerver *cerver = lobby->cerver;
                    GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;
                    lobby_teardown (game_cerver, lobby);
                }

                retval = 0;
            }
        }
    }

    return retval;

}

// gets a player from the lobby using the query
Player *player_get_from_lobby (Lobby *lobby, Player *query) {

    return (lobby ? (Player *) dlist_search (lobby->players, query, NULL) : NULL);

}

// get sthe list element associated with the player
ListElement *player_get_le_from_lobby (Lobby *lobby, Player *player) {

    return (lobby ? dlist_get_element (lobby->players, player, NULL) : NULL);

}

// gets a player from the lobby player's list suing the sock fd
Player *player_get_by_sock_fd_list (Lobby *lobby, i32 sock_fd) {

    if (lobby) {
        Player *player = NULL;
        Connection *connection = NULL;
        for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
            player = (Player *) le->data;
            for (ListElement *le_sub = dlist_start (player->client->connections); le_sub; le_sub = le_sub->next) {
                connection = (Connection *) le_sub->data;
                if (connection->socket->sock_fd == sock_fd) return player;
            }
        }
    }

    return NULL;

}

// broadcasts a packet to all the players in the lobby
void player_broadcast_to_all (Cerver *cerver, const Lobby *lobby, Packet *packet, 
    Protocol protocol, int flags) {

    if (lobby && packet) {
        Player *player = NULL;
        for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
            player = (Player *) le->data;

            Connection *connection = NULL;
            for (ListElement *le_sub = dlist_start (player->client->connections); le_sub; le_sub = le_sub->next) {
                connection = (Connection *) le_sub->data;
                packet_set_network_values (packet, cerver, player->client, connection, (Lobby *) lobby);
                packet_send (packet, flags, NULL, false);
            }
        }
    }

}

// TODO:
// this is used to clean disconnected players inside a lobby
// if we haven't recieved any kind of input from a player, disconnect it 
void checkPlayerTimeouts (void) {}