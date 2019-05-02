#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <time.h>

#include <poll.h>
#include <errno.h>

#include "types/myTypes.h"

#include "cerver/game/game.h"
#include "cerver/game/player.h"
#include "cerver/game/lobby.h"

#include "collections/dllist.h"
#include "collections/avl.h"

#include "utils/objectPool.h"

#include "utils/myUtils.h"
#include "utils/config.h"
#include "utils/log.h"
#include "utils/sha-256.h"

static void lobby_default_handler (void *data);

/*** Lobby Configuration ***/

// sets the lobby settings and a function to delete it
void lobby_set_game_settings (Lobby *lobby, void *game_settings, Action delete_game_settings) {

    if (lobby) {
        lobby->game_settings = game_settings;
        lobby->delete_lobby_game_settings = delete_game_settings;
    }

}

// sets the lobby game data and a function to delete it
void lobby_set_game_data (Lobby *lobby, void *game_data, Action delete_lobby_game_data) {

    if (lobby) {
        lobby->game_data = game_data;
        lobby->delete_lobby_game_data = delete_lobby_game_data;
    }

}

// set the lobby player handler
void lobby_set_handler (Lobby *lobby, Action handler) { if (lobby) lobby->handler = handler; }

void lobby_default_generate_id (char *lobby_id) {

    time_t rawtime = time (NULL);
    struct tm *timeinfo = localtime (&rawtime);
    // we generate the string timestamp in order to generate the lobby id
    char temp[50];
    size_t len = strftime (temp, 50, "%F - %r", timeinfo);
    printf ("%s\n", temp);

    uint8_t hash[32];
    char hash_string[65];

    sha_256_calc (hash, temp, len);
    sha_256_hash_to_string (hash_string, hash);

    lobby_id = createString ("%s", hash_string);

}

// lobby constructor, it also initializes basic lobby data
Lobby *lobby_new (GameServerData *game_data, unsigned int max_players) {

    Lobby *lobby = (Lobby *) malloc (sizeof (Lobby));
    if (lobby) {
        memset (lobby, 0, sizeof (Lobby));

        lobby->creation_time_stamp = time (NULL);
        game_data->lobby_id_generator ((char *) lobby->id);

        lobby->game_settings = NULL;

        // FIXME: we dont want to delete the players here!! they also have a refrence in the main avl tree!!
        lobby->players = avl_init (game_data->player_comparator, player_delete);
        lobby->owner = NULL; 
        
        lobby->max_players = max_players;
        lobby->players_fds = (struct pollfd *) calloc (max_players, sizeof (struct pollfd));
        lobby->compress_players = false;        // default

        lobby->game_data = NULL;
        lobby->delete_lobby_game_data = NULL;

        lobby->isRunning = false;
        lobby->inGame = false;

        lobby->handler = lobby_default_handler;
    }

    return lobby;

}

// deletes a lobby for ever -- called when we teardown the server
// we do not need to give any feedback to the players if there is any inside
void lobby_delete (void *ptr) {

    if (ptr) {
        Lobby *lobby = (Lobby *) ptr;

        if (lobby->id) free ((void *) lobby->id);

        lobby->inGame = false;          // just to be sure
        lobby->isRunning = false;
        lobby->owner = NULL;            // the owner is destroyed in the avl tree

        if (lobby->game_data) {
            if (lobby->delete_lobby_game_data) lobby->delete_lobby_game_data (lobby->game_data);
            else free (lobby->game_data);
        }

        if (lobby->players_fds) {
            memset (lobby->players_fds, 0, sizeof (struct pollfd) * lobby->max_players);
            free (lobby->players_fds);
        }

        if (lobby->players) {
            avl_clearTree (&lobby->players->root, lobby->players->destroy);
            free (lobby->players);
        }   

        if (lobby->game_settings) {
            if (lobby->delete_lobby_game_settings) lobby->delete_lobby_game_settings (lobby->game_settings);
            else free (lobby->game_settings);
        }

        free (lobby);
    }

}

int lobby_comparator (void *one, void *two) {

    if (one && two) {
        Lobby *lobby_one = (Lobby *) one;
        Lobby *lobby_two = (Lobby *) two;

        if (lobby_one->id < lobby_two->id) return -1;
        else if (lobby_one->id <= lobby_two->id) return 0;
        else return 1;
    }

}

// TODO: add the option to set a comparator in the game data
// searchs a lobby in the game data and returns a reference to it
Lobby *lobby_get (GameServerData *game_data, Lobby *query) {

    Lobby *found = NULL;
    for (ListElement *le = dlist_start (game_data->currentLobbys); le; le = le->next) {
        found = (Lobby *) le->data;
        if (!lobby_comparator (found, query)) break;
    }

    return found;

}

// we remove any fd that was set to -1 for what ever reason
static void lobby_players_compress (Lobby *lobby) {

    if (lobby) {
        lobby->compress_players = false;
        
        for (u16 i = 0; i < lobby->players_nfds; i++) {
            if (lobby->players_fds[i].fd == -1) {
                for (u16 j = i; j < lobby->players_nfds; j++)
                    lobby->players_fds[i].fd = lobby->players_fds[j + 1].fd;

                i--;
                lobby->players_nfds--;
            }
        }
    }

}

// create a list to manage the server lobbys
// called when we init the game server
u8 game_init_lobbys (GameServerData *game_data, u8 n_lobbys) {

    u8 retval = 1;

    if (game_data) {
        if (game_data->currentLobbys) logMsg (stdout, WARNING, SERVER, "The server has already a list of lobbys.");
        else {
            game_data->currentLobbys = dlist_init (lobby_delete, lobby_comparator);
            if (game_data->currentLobbys) retval = 0;
            else logMsg (stderr, ERROR, NO_TYPE, "Failed to init server's lobby list!");
        }
    }

    return retval;

}

// FIXME:
// add a player to a lobby
static u8 lobby_add_player (Lobby *lobby, Player *player) {

    if (lobby && player) {
        // if (!player_is_in_lobby (player, lobby)) {

        // }
    }

}

// FIXME:
// removes a player from the lobby
static u8 lobby_remove_player (Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {

    }

    return retval;

}

// FIXME:
// add a player to the lobby structures
u8 player_add_to_lobby (Server *server, Lobby *lobby, Player *player) {

    /* if (lobby && player) {
        if (server->type == GAME_SERVER) {
            GameServerData *gameData = (GameServerData *) server->serverData;
            if (gameData) {
                if (!player_is_in_lobby (player, lobby)) {
                    Player *p = avl_removeNode (gameData->players, player);
                    if (p) {
                        i32 client_sock_fd = player->client->active_connections[0];

                        avl_insertNode (lobby->players, p);

                        // for (u32 i = 0; i < poll_n_fds; i++) {
                        //     if (server->fds[i].fd == client_sock_fd) {
                        //         server->fds[i].fd = -1;
                        //         server->fds[i].events = -1;
                        //     }
                        // } 

                        lobby->players_fds[lobby->players_nfds].fd = client_sock_fd;
                        lobby->players_fds[lobby->players_nfds].events = POLLIN;
                        lobby->players_nfds++;

                        player->inLobby = true;

                        return 0;
                    }

                    else logMsg (stderr, ERROR, GAME, "Failed to get player from avl!");
                }
            }
        }
    } */

    return 1;    

}

// FIXME: client socket!!
// removes a player from the lobby's players structures and sends it to the game server's players
u8 player_remove_from_lobby (Server *server, Lobby *lobby, Player *player) {

    // if (server && lobby && player) {
    //     if (server->type == GAME_SERVER) {
    //         GameServerData *gameData = (GameServerData *) gameData;
    //         if (gameData) {
    //             // make sure that the player is inside the lobby...
    //             if (player_is_in_lobby (player, lobby)) {
    //                 // create a new player and add it to the server's players
    //                 // Player *p = newPlayer (gameData->playersPool, NULL, player);

    //                 // remove from the poll fds
    //                 for (u8 i = 0; i < lobby->players_nfds; i++) {
    //                     /* if (lobby->players_fds[i].fd == player->client->clientSock) {
    //                         lobby->players_fds[i].fd = -1;
    //                         lobby->players_nfds--;
    //                         lobby->compress_players = true;
    //                         break;
    //                     } */
    //                 }

    //                 // delete the player from the lobby
    //                 avl_removeNode (lobby->players, player);

    //                 // p->inLobby = false;

    //                 // player_registerToServer (server, p);

    //                 return 0;
    //             }
    //         }
    //     }
    // }

    return 1;

}

// starts the lobby in a separte thread using its hanlder
u8 lobby_start (Server *server, Lobby *lobby) {

    u8 retval = 1;

    if (server && lobby) {
        if (lobby->handler) {
            ServerLobby *sl = (ServerLobby *) malloc (sizeof (ServerLobby));
            sl->server = server;
            sl->lobby = lobby;
            if (thpool_add_work (server->thpool, (void *) lobby->handler, sl) < 0)
                logMsg (stderr, ERROR, GAME, "Failed to start lobby - failed to add to thpool!");
            else retval = 0;        // success
        } 

        else logMsg (stderr, ERROR, GAME, "Failed to start lobby - no reference to lobby handler.");
    }

    return retval;

}

// creates a new lobby and inits his values with an owner
Lobby *lobby_create (Server *server, Player *owner, unsigned int max_players) {

    Lobby *lobby = NULL;

    if (server && owner) {
        GameServerData *game_data = (GameServerData *) server->serverData;

        // we create a timestamp of the creation of the lobby
        lobby = lobby_new (game_data, max_players);
        if (lobby) {
            if (!lobby_add_player (lobby, owner)) {
                lobby->owner = owner;
                lobby->current_players = 1;

                // add the lobby the server active ones
                dlist_insert_after (game_data->currentLobbys, dlist_end (game_data->currentLobbys), lobby);
            }

            else {
                logMsg (stderr, ERROR, GAME, "Failed to add owner to lobby!");
                lobby_delete (lobby);
                lobby = NULL;
            }
        }
    }

    return lobby;

}

// called by a registered player that wants to join a lobby on progress
// the lobby model gets updated with new values
u8 lobby_join (GameServerData *game_data, Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {
        // check if for whatever reason a player al ready inside the lobby wants to join...
        if (!player_is_in_lobby (lobby, game_data->player_comparator, player)) {
            // check if the lobby is al ready running the game
            if (!lobby->inGame) {
                // check lobby capacity
                if (lobby->current_players < lobby->max_players) {
                    // the player is clear to join the lobby
                    if (!lobby_add_player (lobby, player)) {
                        lobby->current_players += 1;
                        retval = 0;
                    }
                }

                else logMsg (stdout, DEBUG_MSG, GAME, "A player tried to join a full lobby.");
            }

            else logMsg (stdout, DEBUG_MSG, GAME, "A player tried to join a lobby that is in game.");
        }

        else logMsg (stderr, ERROR, GAME, "A player tries to join the same lobby he is in.");
    }

    return retval;

}

// called when a player requests to leave the lobby
u8 lobby_leave (GameServerData *game_data, Lobby *lobby, Player *player) {

    u8 retval = 1;

    if (lobby && player) {
        // first check if the player is inside the lobby
        if (player_is_in_lobby (lobby, game_data->player_comparator, player)) {
            // FIXME:
            // now check if the player is the owner

            // remove the player from the lobby
            if (!lobby_remove_player (lobby, player)) lobby->current_players -= 1;

            // check if there are still players in the lobby
            if (lobby->current_players > 0) {
                // TODO: what do we do to the players??
            }

            else {  
                // the player was the last one in, so we can safely delete the lobby
                lobby_delete (lobby);
                retval = 0;
            } 
        }
    }

    return retval;

}

u8 lobby_destroy (Server *server, Lobby *lobby) {}

// FIXME: 19/april/2019 -- do we still need this?
// a lobby should only be destroyed when there are no players left or if we teardown the server
u8 destroyLobby (Server *server, Lobby *lobby) {

    /* if (server && lobby) {
        if (server->type == GAME_SERVER) {
            GameServerData *gameData = (GameServerData *) server->serverData;
            if (gameData) {
                // the game function must have a check for this every loop!
                if (lobby->inGame) lobby->inGame = false;
                if (lobby->gameData) {
                    if (lobby->deleteLobbyGameData) lobby->deleteLobbyGameData (lobby->gameData);
                    else free (lobby->gameData);
                }

                if (lobby->players_nfds > 0) {
                    // send the players the correct package so they can handle their own logic
                    // expected player behaivor -> leave the lobby 
                    size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData);
                    void *destroyPacket = generatePacket (GAME_PACKET, packetSize);
                    if (destroyPacket) {
                        char *end = destroyPacket + sizeof (PacketHeader);
                        RequestData *reqdata = (RequestData *) end;
                        reqdata->type = LOBBY_DESTROY;

                        broadcastToAllPlayers (lobby->players->root, server, destroyPacket, packetSize);
                        free (destroyPacket);
                    }

                    else 
                        logMsg (stderr, ERROR, PACKET, "Failed to create lobby destroy packet!");

                    // this should stop the lobby poll thread
                    lobby->isRunning = false;

                    // remove the players from this structure and send them to the server's players
                    Player *tempPlayer = NULL;
                    while (lobby->players_nfds > 0) {
                        tempPlayer = (Player *) lobby->players->root->id;
                        if (tempPlayer) player_remove_from_lobby (server, lobby, tempPlayer);
                    }
                }

                lobby->owner = NULL;
                if (lobby->settings) free (lobby->settings);

                // we are safe to clear the lobby structure
                // first remove the lobby from the active ones, then send it to the inactive ones
                ListElement *le = dlist_get_element (gameData->currentLobbys, lobby);
                if (le) {
                    void *temp = dlist_remove_element (gameData->currentLobbys, le);
                    if (temp) pool_push (gameData->lobbyPool, temp);
                }

                else {
                    logMsg (stdout, WARNING, GAME, "A lobby wasn't found in the current lobby list.");
                    deleteLobby (lobby);   // destroy the lobby forever
                } 
                
                return 0;   // success
            }

            else logMsg (stderr, ERROR, SERVER, "No game data found in the server!");
        }
    }

    return 1; */

}

// FIXME: client socket!!
// FIXME: send packet!
// TODO: add a timestamp when the player leaves

// FIXME: finish refcatoring lobby_leaver with this!!
u8 leaveLobby (Server *server, Lobby *lobby, Player *player) {

    // make sure that the player is inside the lobby
    // if (player_is_in_lobby (player, lobby)) {
    //     // check to see if the player is the owner of the lobby
    //     bool wasOwner = false;
    //     // TODO: we should be checking for the player's id instead
    //     // if (lobby->owner->client->clientSock == player->client->clientSock) 
    //     //     wasOwner = true;

    //     if (player_remove_from_lobby (server, lobby, player)) return 1;

    //     // there are still players in the lobby
    //     if (lobby->players_nfds > 0) {
    //         if (lobby->inGame) {
    //             // broadcast the other players that the player left
    //             // we need to send an update lobby packet and the players handle the logic
    //             sendLobbyPacket (server, lobby);

    //             // if he was the owner -> set a new owner of the lobby -> first one in the array
    //             if (wasOwner) {
    //                 Player *temp = NULL;
    //                 u8 i = 0;
    //                 do {
    //                     temp = getPlayerBySock (lobby->players->root, lobby->players_fds[i].fd);
    //                     if (temp) {
    //                         lobby->owner = temp;
    //                         size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData);
    //                         void *packet = generatePacket (GAME_PACKET, packetSize);
    //                         if (packet) {
    //                             // server->protocol == IPPROTO_TCP ?
    //                             // tcp_sendPacket (temp->client->clientSock, packet, packetSize, 0) :
    //                             // udp_sendPacket (server, packet, packetSize, temp->client->address);
    //                             free (packet);
    //                         }
    //                     }

    //                     // we got a NULL player in the structures -> we don't expect this to happen!
    //                     else {
    //                         logMsg (stdout, WARNING, GAME, 
    //                             "Got a NULL player when searching for new owner!");
    //                         lobby->players_fds[i].fd = -1;
    //                         lobby->compress_players = true; 
    //                         i++;
    //                     }
    //                 } while (!temp);
    //             }
    //         }

    //         // players are in the lobby screen -> owner destroyed the lobby
    //         else destroyLobby (server, lobby);
    //     }

    //     // player that left was the last one 
    //     // 21/11/2018 -- destroy lobby is in charge of correctly ending the game
    //     else destroyLobby (server, lobby);
        
    //     return 0;   // the player left the lobby successfully
    // }

    // else {
    //     #ifdef DEBUG
    //         logMsg (stderr, ERROR, GAME, "The player doesn't belong to the lobby!");
    //     #endif
    // }

    // return 1;

}

/*** BLACKROCK SPECIFIC ***/

// FIXME: 20/04/2019 - we need to check this - this is kind of blackrock specific
// TODO: maybe the admin can add a custom ptr to a custom function?
// FIXME:
// TODO: pass the correct game type and maybe create a more advance algorithm
// finds a suitable lobby for the player
/* Lobby *findLobby (Server *server) {

    // FIXME: how do we want to handle these errors?
    // perform some check here...
    if (!server) return NULL;
    if (server->type != GAME_SERVER) {
        logMsg (stderr, ERROR, SERVER, "Can't search for lobbys in non game server.");
        return NULL;
    }
    
    GameServerData *gameData = (GameServerData *) server->serverData;
    if (!gameData) {
        logMsg (stderr, ERROR, SERVER, "NULL reference to game data in game server!");
        return NULL;
    }

    // 11/11/2018 -- we have a simple algorithm that only searches for the first available lobby
    Lobby *lobby = NULL;
    for (ListElement *e = dlist_start (gameData->currentLobbys); e != NULL; e = e->next) {
        lobby = (Lobby *) e->data;
        if (lobby) {
            if (!lobby->inGame) {
                if (lobby->players_nfds < lobby->settings->maxPlayers) {
                    // we have found a suitable lobby
                    break;
                }
            }
        }
    }

    // TODO: what do we do if we do not found a lobby? --> create a new one?
    if (!lobby) {

    }

    return lobby;

} */

// FIXME: 20/04/2019 - we need to check this - this is kind of blackrock specific
// recieves packages from players inside the lobby
static void lobby_default_handler (void *data) {

    if (data) {
        ServerLobby *sl = (ServerLobby *) data;
        Server *server = sl->server;
        Lobby *lobby = sl->lobby;

        // 21/04/2019 -- 6:09 -- these must go here!!
        lobby->inGame = false;
        lobby->isRunning = true;

        /* ssize_t rc;                                  // retval from recv -> size of buffer
        char packetBuffer[MAX_UDP_PACKET_SIZE];      // buffer for data recieved from fd
        GamePacketInfo *info = NULL;

        #ifdef CERVER_DEBUG
        logMsg (stdout, SUCCESS, SERVER, "New lobby has started!");
        #endif

        int poll_retval;    // ret val from poll function
        int currfds;        // copy of n active server poll fds
        while (lobby->isRunning) {
            poll_retval = poll (lobby->players_fds, lobby->players_nfds, lobby->poll_timeout);

            // poll failed
            if (poll_retval < 0) {
                logMsg (stderr, ERROR, SERVER, "Lobby poll failed!");
                perror ("Error");
                lobby->isRunning = false;
                break;
            }

            // if poll has timed out, just continue to the next loop... 
            if (poll_retval == 0) {
                #ifdef DEBUG
                logMsg (stdout, DEBUG_MSG, SERVER, "Lobby poll timeout.");
                #endif
                continue;
            }

            // one or more fd(s) are readable, need to determine which ones they are
            currfds = lobby->players_nfds;
            for (u8 i = 0; i < currfds; i++) {
                if (lobby->players_fds[i].fd <= -1) continue;

                if (lobby->players_fds[i].revents == 0) continue;

                if (lobby->players_fds[i].revents != POLLIN) 
                    logMsg (stderr, ERROR, GAME, "Lobby poll - Unexpected poll result!");

                do {
                    rc = recv (lobby->players_fds[i].fd, packetBuffer, sizeof (packetBuffer), 0);
                    
                    if (rc < 0) {
                        if (errno != EWOULDBLOCK) {
                            logMsg (stderr, ERROR, SERVER, "On hold recv failed!");
                            perror ("Error:");
                        }

                        break;  // there is no more data to handle
                    }

                    if (rc == 0) break;

                    info = newGamePacketInfo (server, lobby, 
                        getPlayerBySock (lobby->players->root, lobby->players_fds[i].fd), packetBuffer, rc);

                    thpool_add_work (server->thpool, (void *) handleGamePacket, info);
                } while (true);
            }

            if (lobby->compress_players) compressPlayers (lobby);
        } */

        // this must go here!!
        free (sl);
    } 

}

// FIXME: this is blackrcock specific code!!
// loads the settings for the selected game type from the game server data
/* GameSettings *getGameSettings (Config *gameConfig, GameType gameType) {

    ConfigEntity *cfgEntity = config_get_entity_with_id (gameConfig, gameType);
	if (!cfgEntity) {
        logMsg (stderr, ERROR, GAME, "Problems with game settings config!");
        return NULL;
    } 

    GameSettings *settings = (GameSettings *) malloc (sizeof (GameSettings));

    char *playerTimeout = config_get_entity_value (cfgEntity, "playerTimeout");
    if (playerTimeout) {
        settings->playerTimeout = atoi (playerTimeout);
        free (playerTimeout);
    } 
    else {
        logMsg (stdout, WARNING, GAME, "No player timeout found in cfg. Using default.");        
        settings->playerTimeout = DEFAULT_PLAYER_TIMEOUT;
    }

    #ifdef DEBUG
    logMsg (stdout, DEBUG_MSG, GAME, createString ("Player timeout: %i", settings->playerTimeout));
    #endif

    char *fps = config_get_entity_value (cfgEntity, "fps");
    if (fps) {
        settings->fps = atoi (fps);
        free (fps);
    } 
    else {
        logMsg (stdout, WARNING, GAME, "No fps found in cfg. Using default.");        
        settings->fps = DEFAULT_FPS;
    }

    #ifdef DEBUG
    logMsg (stdout, DEBUG_MSG, GAME, createString ("FPS: %i", settings->fps));
    #endif

    char *minPlayers = config_get_entity_value (cfgEntity, "minPlayers");
    if (minPlayers) {
        settings->minPlayers = atoi (minPlayers);
        free (minPlayers);
    } 
    else {
        logMsg (stdout, WARNING, GAME, "No min players found in cfg. Using default.");        
        settings->minPlayers = DEFAULT_MIN_PLAYERS;
    }

    #ifdef DEBUG
    logMsg (stdout, DEBUG_MSG, GAME, createString ("Min players: %i", settings->minPlayers));
    #endif

    char *maxPlayers = config_get_entity_value (cfgEntity, "maxPlayers");
    if (maxPlayers) {
        settings->maxPlayers = atoi (maxPlayers);
        free (maxPlayers);
    } 
    else {
        logMsg (stdout, WARNING, GAME, "No max players found in cfg. Using default.");        
        settings->maxPlayers = DEFAULT_MIN_PLAYERS;
    }

    #ifdef DEBUG
    logMsg (stdout, DEBUG_MSG, GAME, createString ("Max players: %i", settings->maxPlayers));
    #endif

    return settings;

} */