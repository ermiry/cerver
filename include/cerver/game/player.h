#ifndef _CERVER_GAME_PLAYER_H_
#define _CERVER_GAME_PLAYER_H_

#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Packet;
struct _Lobby;

struct _Player {

	String *id;

	struct _Client *client;		// client network data associated to this player

	// the milliseconds of the monotonic clock when the player sent a packet to its lobby,
	// read and written atomically
	u64 last_activity;

	void *data;
	Action data_delete;

};

typedef struct _Player Player;

CERVER_EXPORT Player *player_new (void);

CERVER_EXPORT void player_delete (void *player_ptr);

// sets the player id
CERVER_EXPORT void player_set_id (Player *player, const char *id);

// sets player data and a way to delete it
CERVER_EXPORT void player_set_data (Player *player, void *data, Action data_delete);

// compares two players by their ids
CERVER_EXPORT int player_comparator_with_id (const void *a, const void *b);

// compares two players by their clients ids
CERVER_EXPORT int player_comparator_client_id (const void *a, const void *b);

// the milliseconds of the monotonic clock that the activity of the players uses
CERVER_PUBLIC u64 player_activity_now (void);

// the player sent a packet now
CERVER_PUBLIC void player_activity_update (Player *player);

// registers each of the player's client connections to the lobby poll structures
CERVER_PRIVATE u8 player_register_to_lobby_poll (struct _Lobby *lobby, Player *player);

// registers a player to the lobby --> add him to lobby's structures,
// with the lock of the game cerver
CERVER_PRIVATE u8 player_register_to_lobby (struct _Lobby *lobby, Player *player);

// unregisters each of the player's client connections from the lobby poll structures
CERVER_PRIVATE u8 player_unregister_from_lobby_poll (struct _Lobby *lobby, Player *player);

// unregisters a player from a lobby --> removes him from lobby's structures,
// gives the owner to the next player, with the lock of the game cerver;
// the player is not released and an empty lobby is not torn down
CERVER_PRIVATE u8 player_unregister_from_lobby (struct _Lobby *lobby, Player *player);

// gets a player from the lobby using the query
CERVER_EXPORT Player *player_get_from_lobby (struct _Lobby *lobby, Player *query);

// gets the list element associated with the player
CERVER_EXPORT ListElement *player_get_le_from_lobby (struct _Lobby *lobby, Player *player);

// gets a player from the lobby player's list suing the sock fd
CERVER_EXPORT Player *player_get_with_sock_fd_list (struct _Lobby *lobby, i32 sock_fd);

// broadcasts a packet to all the players in the lobby
CERVER_EXPORT void player_broadcast_to_all (
	struct _Cerver *cerver, const struct _Lobby *lobby, struct _Packet *packet,
	Protocol protocol, int flags
);

#ifdef __cplusplus
}
#endif

#endif
