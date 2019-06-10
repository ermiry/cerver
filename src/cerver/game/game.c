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
#include "cerver/collections/avl.h"
#include "cerver/collections/htab.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/objectPool.h"
#include "cerver/utils/config.h"
#include "cerver/utils/log.h"

GamePacketInfo *newGamePacketInfo (Server *server, Lobby *lobby, Player *player, 
    char *packetData, size_t packetSize);

/*** Game Server configuration ***/

// option to set the game server lobby id generator
void game_set_lobby_id_generator (GameServerData *game_data, void (*lobby_id_generator)(char *)) {

    if (game_data) game_data->lobby_id_generator = lobby_id_generator;

}

// option to set the main game server player comprator
void game_set_player_comparator (GameServerData *game_data, Comparator player_comparator) {

    if (game_data) game_data->player_comparator = player_comparator;

}

// option to set the a func to be executed only once at the start of the game server
void game_set_load_game_data (GameServerData *game_data, Action load_game_data) {

    if (game_data) game_data->load_game_data = load_game_data;

}

// option to set the func to be executed only once when the game server gets destroyed
void game_set_delete_game_data (GameServerData *game_data, Action delete_game_data) {

    if (game_data) game_data->delete_game_data = delete_game_data;

}

// option to set an action to be performed right before the game server teardown
// the server reference will be passed to the action
// eg. send a message to all players
void game_set_final_action (GameServerData *game_data, Action final_action) {

    if (game_data) game_data->final_game_action = final_action;

}

/*** Game Server Data ***/

// constructor for a new game server data
// initializes game server data structures and sets actions to defaults
GameServerData *game_server_data_new (void) {

    GameServerData *game_server_data = (GameServerData *) malloc (sizeof (GameServerData));
    if (game_server_data) {
        game_server_data->currentLobbys = dlist_init (lobby_delete, lobby_comparator);
        game_server_data->lobby_id_generator = lobby_default_generate_id;
        game_server_data->findLobby = NULL;

        game_server_data->player_comparator = player_comparator_client_id;
        game_init_players (game_server_data, game_server_data->player_comparator);

        game_server_data->load_game_data = game_server_data->delete_game_data = NULL;
        game_server_data->game_data = NULL;

        game_server_data->final_game_action = NULL;
    }

    return game_server_data;

}

void game_server_data_delete (GameServerData *game_server_data) {

    if (game_server_data) {
        // destroy current active lobbies
        dlist_destroy (game_server_data->currentLobbys);

        // destroy game players
        avl_clear_tree (&game_server_data->players->root, game_server_data->players->destroy);

        // delete game server game data
        if (game_server_data->delete_game_data) game_server_data->delete_game_data (game_server_data->game_data);

        free (game_server_data);
    }

}

/*** Game Server ***/

// cleans up all the game structs like lobbys and in game data set by the admin
u8 game_server_teardown (Server *server) {

    int retval = 1;

    if (server) {
        GameServerData *game_server_data = (GameServerData *) server->serverData;
        if (game_server_data) {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stdout, DEBUG_MSG, SERVER, string_create ("Destroying server's: %s game data...", server->name));
            #endif
            
            if (game_server_data->final_game_action) game_server_data->final_game_action (server);

            game_server_data_delete (game_server_data);
        }

        else cerver_log_msg (stderr, WARNING, NO_TYPE, string_create ("Server %s does not have a refernce to a game data.", server->name));
    }

    return retval;

}

/*** THE FOLLOWING AND KIND OF BLACKROCK SPECIFIC ***/
/*** WE NEED TO DECIDE WITH NEED TO BE ON THE FRAMEWORK AND WHICH DOES NOT!! ***/

void blackrock_game_server_final_action () {

    // send server destroy packet to all the players
    // size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData);
    // void *packet = generatePacket (SERVER_PACKET, packetSize);
    // if (packet) {
    //     char *end = packet + sizeof (PacketHeader);
    //     RequestData *req = (RequestData *) end;
    //     req->type = SERVER_TEARDOWN;

    //     // send the packet to players outside the lobby
    //     player_broadcast_to_all (gameData->players->root, server, packet, packetSize);

    //     free (packet);
    // }

    // else cerver_log_msg (stderr, ERROR, PACKET, "Failed to create server teardown packet!");

    // #ifdef CERVER_DEBUG
    //     cerver_log_msg (stdout, DEBUG_MSG, SERVER, "Done sending server teardown packet to players.");
    // #endif

}

// FIXME:
#pragma region GAME SERIALIZATION 

// TODO: what info do we want to send?
void serialize_player (SPlayer *splayer, Player *player) {

    if (splayer && player) {
        
    }

}

// FIXME:
// TODO: what treatment do we want to give the serialized data?

// traverse each player in the lobby's avl and serialize it
void serialize_lobby_players (AVLNode *node, SArray *sarray) {

    if (node && sarray) {
        serialize_lobby_players (node->right, sarray);

        if (node->id) {

        }

        serialize_lobby_players (node->left, sarray);
    }

}

#pragma endregion

/*** GAME THREAD ***/

#pragma region GAME LOGIC

// TODO: 13/11/2018 - do we want to start the game logic in a new thread?
    // remeber that the lobby is listening in its own thread that can handle imput logic
    // but what about a thread in the lobby for listenning and another for game logic and sending packets?

// FIXME: how do we hanlde the creation of different types of games?
// the game server starts a new game with the function set for the dessired game type
u8 gs_startGame (Server *server, Lobby *lobby) {

    if (server && lobby) {
        GameServerData *gameData = (GameServerData *) server->serverData;

        // FIXME:
        // if (!gameData->gameInitFuncs) {
        //     cerver_log_msg (stderr, ERROR, GAME, "Init game functions not set!");
        //     return 1;
        // }

        // delegate temp = gameData->gameInitFuncs[lobby->settings->gameType];
        /* if (temp) {
            // 14/11/2018 -- al game functions will get a reference to the server and lobby
            // where they are requested to init
            ServerLobby *sl = (ServerLobby *) malloc (sizeof (ServerLobby));
            sl->server = server;
            sl->lobby = lobby;

            #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, DEBUG_MSG, GAME, "Starting the game...");
            #endif

            // we expect the game function to sync players and send game packets directly 
            // using the framework
            if (!temp (sl)) {
                #ifdef CERVER_DEBUG
                    cerver_log_msg (stdout, SUCCESS, GAME, "A new game has started!");
                #endif
                return 0;
            }

            else {
                #ifdef CERVER_DEBUG
                    cerver_log_msg (stderr, ERROR, GAME, "Failed to start a new game!");
                #endif
                return 1;
            }
        }
        
        else {
            cerver_log_msg (stderr, ERROR, GAME, "No init function set for the desired game type!");
            return 1;
        } */
        
    }

    else return 1;

}

// FIXME:
// stops the current game in progress
// called when the players don't finish the current game
void stopGame (void) {

    // logic to stop the current game in progress

}

// FIXME:
// called when the win condition has been reached inside a lobby
void endGame (void) {

    // logic to stop the current game in the server
    // send feedback to the players that the game has finished
    // send players the scores and leaderboard
    // the lobby owner can retry the game or exit to the lobby screen

}

#pragma endregion

// FIXME: where do we want to put this?
/*** REQUESTS FROM INSIDE THE LOBBY ***/

// a player wants to send a message in the lobby chat
void msgtoLobbyPlayers () {

}

/*** GAME UPDATES ***/

// handle a player input, like movement -> TODO: we need to check for collisions between enemies
void handlePlayerInput () {

}

#pragma region GAME PACKETS

// FIXME: serialize players inside the lobby
// creates a lobby packet with the passed lobby info
void *createLobbyPacket (RequestType reqType, Lobby *lobby, size_t packetSize) {

    void *packetBuffer = malloc (packetSize);
    void *begin = packetBuffer;
    char *end = begin; 

    PacketHeader *header = (PacketHeader *) end;
    initPacketHeader (header, GAME_PACKET, packetSize); 
    end += sizeof (PacketHeader);

    RequestData *reqdata = (RequestData *) end;
    reqdata->type = reqType;
    end += sizeof (RequestType);

    // serialized lobby data
    SLobby *slobby = (SLobby *) end;
    end += sizeof (SLobby);

    // slobby->settings.gameType = lobby->settings->gameType;
    // slobby->settings.fps = lobby->settings->fps;
    // slobby->settings.minPlayers = lobby->settings->minPlayers;
    // slobby->settings.maxPlayers = lobby->settings->maxPlayers;
    // slobby->settings.playerTimeout = lobby->settings->playerTimeout;

    slobby->inGame = lobby->inGame;

    // FIXME: 
    // we serialize the players using a vector
    // slobby->players.n_elems = 0;

    return begin;

}

// sends a lobby update packet to all the players inside a lobby
void sendLobbyPacket (Server *server, Lobby *lobby) {

    if (lobby) {
        size_t packetSize = sizeof (PacketHeader) + sizeof (RequestData) +
            sizeof (SLobby) + lobby->players_nfds * sizeof (SPlayer);
        void *lobbyPacket = createLobbyPacket (LOBBY_UPDATE, lobby, packetSize);
        if (lobbyPacket) {
            player_broadcast_to_all (lobby->players->root, server, lobbyPacket, packetSize);
            free (lobbyPacket);
        }

        else cerver_log_msg (stderr, ERROR, PACKET, "Failed to create lobby update packet!");
    }

}

/*** Prototypes of public game server functions ***/

/*** reqs outside a lobby ***/
void gs_createLobby (Server *, Client *, i32, GameType);
void gs_joinLobby (Server *, Client *, GameType);

/*** reqs from inside a lobby ***/
void gs_leaveLobby (Server *, Player *, Lobby *);
void gs_initGame (Server *, Player *, Lobby *);
void gs_sendMsg (Server *, Player *, Lobby *, char *msg);

// this is called from the main poll in a new thread
void gs_handlePacket (PacketInfo *pack_info) {

    cerver_log_msg (stdout, DEBUG_MSG, GAME, "gs_handlePacket ()");

    RequestData *reqData = (RequestData *) (pack_info->packetData + sizeof (PacketHeader));
    switch (reqData->type) {
        // TODO: get the correct game type from the packet
        case LOBBY_CREATE: 
            gs_createLobby (pack_info->server, pack_info->client, pack_info->clientSock, ARCADE); break;
        case LOBBY_JOIN: gs_joinLobby (pack_info->server, pack_info->client, ARCADE); break;
        default: break;
    }

}

GamePacketInfo *newGamePacketInfo (Server *server, Lobby *lobby, Player *player, 
    char *packetData, size_t packetSize) {

    // if (server && lobby && player && packetData && (packetSize > 0)) {
    //     GamePacketInfo *info = NULL;

    //     if (lobby->gamePacketsPool) {
    //         info = (GamePacketInfo *) pool_pop (lobby->gamePacketsPool);
    //         if (!info) info = (GamePacketInfo *) malloc (sizeof (GamePacketInfo));
    //     }

    //     else info = (GamePacketInfo *) malloc (sizeof (GamePacketInfo));

    //     if (info) {
    //         info->server = server;
    //         info->lobby = lobby;
    //         info->player = player;
    //         strcpy (info->packetData, packetData);
    //         info->packetSize = packetSize;

    //         return info;
    //     }
    // }

    // return NULL;

}

void deleteGamePacketInfo (void *data) {

    if (data) {
        GamePacketInfo *info = (GamePacketInfo *) info;

        info->server = NULL;
        info->lobby = NULL;
        info->player = NULL;

        free (info);
    }

}

// handles all valid requests a player can make from inside a lobby
void handleGamePacket (void *data) {

    // if (data) {
    //     GamePacketInfo *packet = (GamePacketInfo *) data;
    //     if (!checkPacket (packet->packetSize, packet->packetData, GAME_PACKET)) {
    //         RequestData *rdata = (RequestData *) packet->packetData + sizeof (PacketHeader);

    //         switch (rdata->type) {
    //             case LOBBY_LEAVE: 
    //                 gs_leaveLobby (packet->server, packet->player, packet->lobby); break;

    //             case GAME_START: 
    //                 gs_initGame (packet->server, packet->player, packet->lobby); break;
    //             case GAME_INPUT_UPDATE:  break;
    //             case GAME_SEND_MSG: break;

    //             // dispose the packet -> send it to the pool
    //             default: break;
    //         }

    //         // after we have use the packet, send it to the pool
    //         pool_push (packet->lobby->gamePacketsPool, packet);
    //     }

    //     // invalid packet -> dispose -> send it to the pool
    //     else pool_push (packet->lobby->gamePacketsPool, packet);
    // }

}

#pragma endregion

/*** PUBLIC FUNCTIONS ***/

// These functions are the ones that handle the request from the client/player
// They provide an interface to interact with the server
// Apart from this functions, all other logic must be PRIVATE to the client/player

#pragma region PUBLIC FUNCTIONS

/*** FROM OUTSIDE A LOBBY ***/

/*** 19/04/2019 -- the following seem to be blackrock specific!!! ***/

// request from a from client to create a new lobby 
void gs_createLobby (Server *server, Client *client, i32 sock_fd, GameType gameType) {

    // if (server && client) {
    //     #ifdef CERVER_DEBUG
    //         cerver_log_msg (stdout, DEBUG_MSG, GAME, "Creating a new lobby...");
    //     #endif

    //     GameServerData *gameData = (GameServerData *) server->serverData;

    //     // check if the client is associated with a player
    //     Player *owner = getPlayerBySock (gameData->players->root, client->active_connections[0]);

    //     // create new player data for the client
    //     if (!owner) {
    //         owner = newPlayer (gameData->playersPool, client);
    //         player_registerToServer (server, owner);
    //     } 

    //     // check that the owner isn't already in a lobby or game
    //     if (owner->inLobby) {
    //         #ifdef CERVER_DEBUG
    //         cerver_log_msg (stdout, DEBUG_MSG, GAME, "A player inside a lobby wanted to create a new lobby.");
    //         #endif
    //         if (sendErrorPacket (server, sock_fd, client->address, 
    //             ERR_CREATE_LOBBY, "Player is already in a lobby!")) {
    //             #ifdef CERVER_DEBUG
    //             cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //             #endif
    //         }
    //         return;
    //     }

    //     Lobby *lobby = createLobby (server, owner, gameType);
    //     if (lobby) {
    //         #ifdef CERVER_DEBUG
    //             cerver_log_msg (stdout, SUCCESS, GAME, "New lobby created!");
    //         #endif 

    //         // send the lobby info to the owner -- we only have one player inside the lobby
    //         size_t lobby_packet_size = sizeof (PacketHeader) + sizeof (RequestData) +
    //             sizeof (SLobby) + lobby->players_nfds * sizeof (SPlayer);
    //         void *lobby_packet = createLobbyPacket (LOBBY_UPDATE, lobby, lobby_packet_size);
    //         if (lobby_packet) {
    //             if (server_sendPacket (server, sock_fd, owner->client->address, 
    //                 lobby_packet, lobby_packet_size))
    //                 cerver_log_msg (stderr, ERROR, PACKET, "Failed to send back lobby packet to owner!");
    //             else cerver_log_msg (stdout, SUCCESS, PACKET, "Sent lobby packet to owner!");
    //             free (lobby_packet);
    //         }

    //         // TODO: do we wait for an ack packet from the client?
    //     }

    //     // there was an error creating the lobby
    //     else {
    //         cerver_log_msg (stderr, ERROR, GAME, "Failed to create a new game lobby.");
    //         // send feedback to the player
    //         sendErrorPacket (server, sock_fd, client->address, ERR_SERVER_ERROR, 
    //             "Game server failed to create new lobby!");
    //     }
    // }

}

// FIXME: use the custom action find lobby set by the admin
// TODO: in a larger game, a player migth wanna join a lobby with his friend
// or we would have an algorithm to find a suitable lobby based on player stats
// as of 10/11/2018 -- a player just joins the first lobby that we find that is not full
void gs_joinLobby (Server *server, Client *client, GameType gameType) {

    // if (server && client) {
    //     GameServerData *gameData = (GameServerData *) server->serverData;

    //     // check if the client is associated with a player
    //     // Player *player = getPlayerBySock (gameData->players, client->clientSock);
    //     Player *player;

    //     // create new player data for the client
    //     if (!player) {
    //         player = newPlayer (gameData->playersPool, client);
    //         player_registerToServer (server, player);
    //     } 

    //     // check that the owner isn't already in a lobby or game
    //     if (player->inLobby) {
    //         #ifdef CERVER_DEBUG
    //         cerver_log_msg (stdout, DEBUG_MSG, GAME, "A player inside a lobby wanted to join a new lobby.");
    //         #endif
    //         // FIXME:
    //         /* if (sendErrorPacket (server, player->client, ERR_CREATE_LOBBY, "Player is already in a lobby!")) {
    //             #ifdef CERVER_DEBUG
    //             cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //             #endif
    //         } */
    //         return;
    //     }

    //     Lobby *lobby = findLobby (server);

    //     if (lobby) {
    //         // add the player to the lobby
    //         if (!joinLobby (server, lobby, player)) {
    //             // the player joined successfully
    //             #ifdef CERVER_DEBUG
    //                 cerver_log_msg (stdout, DEBUG_MSG, GAME, "A new player has joined the lobby");
    //             #endif
    //         }

    //         // errors are handled inisde the join lobby function
    //     }

    //     // FIXME:
    //     // failed to find a lobby, so create a new one
    //     // else gs_createLobby (server, client, gameType);
    // }

}

/*** FROM INSIDE A LOBBY ***/

void gs_leaveLobby (Server *server, Player *player, Lobby *lobby) {

    // if (server && player && lobby) {
    //     GameServerData *gameData = (GameServerData *) server->serverData;

    //     if (player->inLobby) {
    //         if (!leaveLobby (server, lobby, player)) {
    //             #ifdef CERVER_DEBUG
    //                 cerver_log_msg (stdout, DEBUG_MSG, GAME, "PLayer left the lobby successfully!");
    //             #endif
    //         }

    //         else {
    //             #ifdef CERVER_DEBUG
    //             cerver_log_msg (stdout, DEBUG_MSG, GAME, "There was a problem with a player leaving a lobby!");
    //             #endif
    //             // FIXME:
    //             /* if (sendErrorPacket (server, player->client, ERR_LEAVE_LOBBY, "Problem with player leaving the lobby!")) {
    //                 #ifdef CERVER_DEBUG
    //                 cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //                 #endif
    //             } */
    //         }
    //     }

    //     else {
    //         #ifdef CERVER_DEBUG
    //         cerver_log_msg (stdout, DEBUG_MSG, GAME, "A player tries to leave a lobby but he is not inside one!");
    //         #endif
    //         // FIXME:
    //         /* if (sendErrorPacket (server, player->client, ERR_LEAVE_LOBBY, "Player is not inside a lobby!")) {
    //             #ifdef CERVER_DEBUG
    //             cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //             #endif
    //         } */
    //     }
    // }

}

void gs_initGame (Server *server, Player *player, Lobby *lobby) {

    // if (server && player && lobby) {
    //     GameServerData *gameData = (GameServerData *) server->serverData;

    //     if (player->inLobby) {
    //         if (lobby->owner->id == player->id) {
    //             if (lobby->players_nfds >= lobby->settings->minPlayers) {
    //                 if (gs_startGame (server, lobby)) {
    //                     cerver_log_msg (stderr, ERROR, GAME, "Failed to start a new game!");
    //                     // send feedback to the players
    //                     size_t packetSize = sizeof (PacketHeader) + sizeof (ErrorData);
    //                     void *errpacket = generateErrorPacket (ERR_GAME_INIT, 
    //                         "Server error. Failed to init the game.");
    //                     if (errpacket) {
    //                         player_broadcast_to_all (lobby->players->root, server, errpacket, packetSize);
    //                         free (errpacket);
    //                     }

    //                     else {
    //                         #ifdef CERVER_DEBUG
    //                         cerver_log_msg (stderr, ERROR, PACKET, 
    //                             "Failed to create & send error packet to client!");
    //                         #endif
    //                     }
    //                 }

    //                 // upon success, we expect the start game func to send the packages
    //                 // we start the game on the new thread
    //             }

    //             else {
    //                 #ifdef CERVER_DEBUG
    //                 cerver_log_msg (stdout, WARNING, GAME, "Need more players to start the game.");
    //                 #endif
    //                 // FIXME: select client socket!!
    //                 /* if (sendErrorPacket (server, player->client, ERR_GAME_INIT, 
    //                     "We need more players to start the game!")) {
    //                     #ifdef CERVER_DEBUG
    //                     cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //                     #endif
    //                 } */
    //             }
    //         }

    //         else {
    //             #ifdef CERVER_DEBUG
    //             cerver_log_msg (stdout, WARNING, GAME, "Player is not the lobby owner.");
    //             #endif
    //             // FIXME:
    //             /* if (sendErrorPacket (server, player->client, ERR_GAME_INIT, 
    //                 "Player is not the lobby owner!")) {
    //                 #ifdef CERVER_DEBUG
    //                 cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //                 #endif
    //             } */
    //         }
    //     }

    //     else {
    //         #ifdef CERVER_DEBUG
    //         cerver_log_msg (stdout, WARNING, GAME, "Player must be inside a lobby and be the owner to start a game.");
    //         #endif
    //         // FIXME:
    //         /* if (sendErrorPacket (server, player->client, ERR_GAME_INIT, 
    //             "The player is not inside a lobby!")) {
    //             #ifdef CERVER_DEBUG
    //             cerver_log_msg (stderr, ERROR, PACKET, "Failed to create & send error packet to client!");
    //             #endif
    //         } */
    //     }
    // }

}

// TODO:
// a player wants to send a msg to the players inside the lobby
void gs_sendMsg (Server *server, Player *player, Lobby *lobby, char *msg) {

    // if (server && player && lobby && msg) {

    // }

}

#pragma endregion