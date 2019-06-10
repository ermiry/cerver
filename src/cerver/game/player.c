#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "cerver/game/game.h"
#include "cerver/game/player.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

// TODO: better id handling and management
u16 nextPlayerId = 0;

void player_set_delete_player_data (Player *player, Action destroy) {

    if (player) player->destroy_player_data = destroy;

}

// TODO: maybe later the id can be how we store it inside our databse
// FIXME: player ids, we cannot have infinite ids!!
// constructor for a new player
Player *player_new (Client *client, const char *session_id, void *player_data) {

    Player *new_player = (Player *) malloc (sizeof (Player));

    new_player->client = client;
    if (session_id) {
        new_player->session_id = (char *) calloc (strlen (session_id) + 1, sizeof (char));
        strcpy ((char *) new_player->session_id, session_id);
    }

    else new_player->session_id = NULL;

    new_player->alive = false;
    new_player->inLobby = false;

    new_player->components = NULL;
    new_player->data = player_data;

    new_player->id = nextPlayerId;
    nextPlayerId++;

    return new_player;

}

// FIXME: correctly destroy the game components
// TODO: what happens with the client data??
// deletes a player struct for ever
void player_delete (void *data) {

    if (data) {
        Player *player = (Player *) data;
        if (player->destroy_player_data) player->destroy_player_data (player->data);
        else if (player->data) free (player->data);

        free (player);
    }

}

// this is our default player comparator
// comparator for players's avl tree
int player_comparator_client_id (const void *a, const void *b) {

    if (a && b) {
        Player *player_a = (Player *) a;
        Player *player_b = (Player *) b;

        return strcmp (player_a->client->clientID, player_b->client->clientID);
    }

}

// compare two players' session id
int player_comparator_by_session_id (const void *a, const void *b) {

    if (a && b) return strcmp (((Player *) a)->session_id, ((Player *) b)->session_id);

}

// adds a player to the game server data main structures
void player_register_to_server (Server *server, Player *player) {

    if (server && player) {
        if (server->type == GAME_SERVER) {
            GameServerData *gameData = (GameServerData *) server->serverData;
            if (gameData->players) avl_insert_node (gameData->players, player);

            #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, DEBUG_MSG, GAME, "Registered a player to the server.");
            #endif
        }

        else {
            #ifdef CERVER_DEBUG
                cerver_log_msg (stdout, WARNING, SERVER, 
                    "Trying to add a player to a server of incompatible type!");
            #endif
        }
    }

}

// FIXME: client socket!!
// removes a player from the server's players struct (avl) and also removes the player
// client from the main server poll
void player_unregister_to_server (Server *server, Player *player) {

    // if (server && player) {
    //     if (server->type == GAME_SERVER) {
    //         GameServerData *gameData = (GameServerData *) server->serverData;
    //         if (gameData) {
    //             // remove the player client from the main server poll
    //             Client *c; // = getClientBySock (server->clients, player->client->clientSock);
    //             if (c) client_unregisterFromServer (server, player->client);

    //             // remove the player from the servers players
    //             avl_remove_node (gameData->players, player);
    //         }
    //     }
    // }

}

// get a player from an avl tree using a comparator and a query
Player *player_get (AVLNode *node, Comparator comparator, void *query) {

    if (node && query) {
        Player *player = NULL;

        player = player_get (node->right, comparator, query);

        if (!player) 
            if (!comparator (node->id, query)) return (Player *) node->id;

        if (!player) player = player_get (node->left, comparator, query)   ;

        return player;
    }

    return NULL;

}

// recursively get the player associated with the socket
Player *player_get_by_socket (AVLNode *node, i32 socket_fd) {

    if (node) {
        Player *player = NULL;

        player = player_get_by_socket (node->right, socket_fd);

        if (!player) {
            if (node->id) {
                player = (Player *) node->id;
                
                // search the socket fd in the clients active connections
                for (int i = 0; i < player->client->n_active_cons; i++)
                    if (socket_fd == player->client->active_connections[i])
                        return player;

            }
        }

        if (!player) player = player_get_by_socket (node->left, socket_fd);

        return player;
    }

    return NULL;

}

// check if a player is inside a lobby using a comparator and a query
bool player_is_in_lobby (Lobby *lobby, Comparator comparator, void *query) {

    if (lobby && query) {
        Player *player = player_get (lobby->players->root, comparator, query);
        if (player) return true;
    }

    return false;

}

// FIXME: client socket!!
// broadcast a packet/msg to all clients/players inside an avl structure
void player_broadcast_to_all (AVLNode *node, Server *server, void *packet, size_t packetSize) {

    if (node && server && packet && (packetSize > 0)) {
        player_broadcast_to_all (node->right, server, packet, packetSize);

        // send packet to curent player
        if (node->id) {
            Player *player = (Player *) node->id;
            if (player) 
                server_sendPacket (server, player->client->active_connections[0],
                    player->client->address, packet, packetSize);
        }

        player_broadcast_to_all (node->left, server, packet, packetSize);
    }

}

// performs an action on every player in an avl tree 
void player_traverse (AVLNode *node, Action action, void *data) {

    if (node && action) {
        player_traverse (node->right, action, data);

        if (node->id) {
            PlayerAndData pd = { .playerData = node->id, .data = data };
            action (&pd);
        } 

        player_traverse (node->left, action, data);
    }

}

// inits the players server's structures
u8 game_init_players (GameServerData *gameData, Comparator player_comparator) {

    if (!gameData) {
        cerver_log_msg (stderr, ERROR, SERVER, "Can't init players in a NULL game data!");
        return 1;
    }

    if (gameData->players)
        cerver_log_msg (stdout, WARNING, SERVER, "The server already has an avl of players!");
    else {
        gameData->players = avl_init (player_comparator, player_delete);
        if (!gameData->players) {
            cerver_log_msg (stderr, ERROR, NO_TYPE, "Failed to init server's players avl!");
            return 1;
        }
    } 

    #ifdef CERVER_DEBUG
        cerver_log_msg (stdout, DEBUG_MSG, GAME, "Players have been init in the game server.");
    #endif

    return 0;

}


// TODO:
// this is used to clean disconnected players inside a lobby
// if we haven't recieved any kind of input from a player, disconnect it 
void checkPlayerTimeouts (void) {}

// TODO: 10/11/2018 -- do we need this?
// when a client creates a lobby or joins one, it becomes a player in that lobby
void tcpAddPlayer (Server *server) {}

// TODO: 10/11/2018 -- do we need this?
// TODO: as of 22/10/2018 -- we only have support for a tcp connection
// when we recieve a packet from a player that is not in the lobby, we add it to the game session
void udpAddPlayer () {}

// TODO: 10/11/2018 -- do we need this?
// TODO: this is used in space shooters to add a new player using a udp protocol
// FIXME: handle a limit of players!!
void addPlayer (struct sockaddr_storage address) {

    // TODO: handle ipv6 ips
    /* char addrStr[IP_TO_STR_LEN];
    sock_ip_to_string ((struct sockaddr *) &address, addrStr, sizeof (addrStr));
    cerver_log_msg (stdout, SERVER, PLAYER, string_create ("New player connected from ip: %s @ port: %d.\n", 
        addrStr, sock_ip_port ((struct sockaddr *) &address))); */

    // TODO: init other necessarry game values
    // add the new player to the game
    // Player newPlayer;
    // newPlayer.id = nextPlayerId;
    // newPlayer.address = address;

    // vector_push (&players, &newPlayer);

    // FIXME: this is temporary
    // spawnPlayer (&newPlayer);

}