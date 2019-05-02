#ifndef _CERVER_PLAYER_H_
#define _CERVER_PLAYER_H_

#include "types/myTypes.h"

#include "cerver/cerver.h"
#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "collections/avl.h"

struct _Server;
struct _GameServerData;
struct _Client;

typedef uint16_t PlayerId;

struct _Player {

    // we may select one of these
	struct _Client *client;		// client network data associated to this player
    const char *session_id;     // unique session id associated with the client

	PlayerId id;
	bool inLobby;
	bool alive;

	// 15/11/2018 -- we spec the player to be ecs based
	// the server admin can add its own components
	void *components;

    // 20/04/2019 -- app specific data, eg a profile from a db
    void *data;     
    Action destroy_player_data;

};

typedef struct _Player Player;

// inits the players server's structures
extern u8 game_init_players (struct _GameServerData *gameData, Comparator player_comparator);

extern void player_set_delete_player_data (Player *player, Action destroy);

// constructor for a new player, option to directly pass player data
extern Player *player_new (Client *client, const char *session_id, void *player_data);
// deletes a player struct for ever
extern void player_delete (void *data);

// comparator for players's avl tree
extern int player_comparator_client_id (const void *a, const void *b);
// compare two players' session id
extern int player_comparator_by_session_id (const void *a, const void *b);

// adds a player to the game server data main structures
extern void player_register_to_server (struct _Server *server, Player *player);
// removes a player from the server's players struct (avl) and also removes the player
// client from the main server poll
extern void player_unregister_to_server (struct _Server *server, Player *player);

// get a player from an avl tree using a comparator and a query
extern Player *player_get (AVLNode *node, Comparator comparator, void *query);

// recursively get the player associated with the socket
extern Player *player_get_by_socket (AVLNode *node, i32 socket_fd);

// check if a player is inside a lobby using a comparator and a query
extern bool player_is_in_lobby (Lobby *lobby, Comparator comparator, void *query);

// broadcast a packet/msg to all clients/players inside an avl structure
extern void player_broadcast_to_all (AVLNode *node, struct _Server *server, void *packet, size_t packetSize);

// performs an action on every player in an avl tree 
extern void player_traverse (AVLNode *node, Action action, void *data);

#endif