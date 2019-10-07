#ifndef _CERVER_GAME_PLAYER_H_
#define _CERVER_GAME_PLAYER_H_

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/cerver.h"
#include "cerver/packets.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#include "cerver/collections/dllist.h"

struct _Cerver;
struct _Client;
struct _Packet;
struct _Lobby;

struct _Player {

	estring *id;

	struct _Client *client;		// client network data associated to this player

    void *data;     
    Action data_delete;

};

typedef struct _Player Player;

extern Player *player_new (void);

extern void player_delete (void *player_ptr);

// sets the player id
extern void player_set_id (Player *player, const char *id);

// sets player data and a way to delete it
extern void player_set_data (Player *player, void *data, Action data_delete);

// compares two players by their ids
extern int player_comparator_by_id (const void *a, const void *b);

// compares two players by their clients ids
extern int player_comparator_client_id (const void *a, const void *b);

// registers each of the player's client connections to the lobby poll structures
extern u8 player_register_to_lobby_poll (struct _Lobby *lobby, Player *player);

// registers a player to the lobby --> add him to lobby's structures
extern u8 player_register_to_lobby (struct _Lobby *lobby, Player *player);

// unregisters each of the player's client connections from the lobby poll structures
extern u8 player_unregister_from_lobby_poll (struct _Lobby *lobby, Player *player);

// unregisters a player from a lobby --> removes him from lobby's structures
extern u8 player_unregister_from_lobby (struct _Lobby *lobby, Player *player);

// gets a player from the lobby using the query
extern Player *player_get_from_lobby (struct _Lobby *lobby, Player *query);

// get sthe list element associated with the player
extern ListElement *player_get_le_from_lobby (struct _Lobby *lobby, Player *player);

// gets a player from the lobby player's list suing the sock fd
extern Player *player_get_by_sock_fd_list (struct _Lobby *lobby, i32 sock_fd);

// broadcasts a packet to all the players in the lobby
extern void player_broadcast_to_all (struct _Cerver *cerver, const struct _Lobby *lobby, struct _Packet *packet, 
    Protocol protocol, int flags);

#endif