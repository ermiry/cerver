#ifndef _CERVER_GAME_GAMETYPE_H_
#define _CERVER_GAME_GAMETYPE_H_

#include <stdbool.h>

#include "cerver/config.h"

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

// 0 keeps the players that do not send packets
#define GAME_TYPE_DEFAULT_PLAYER_TIMEOUT	0

// the frames per second of the update of a lobby
#define GAME_TYPE_DEFAULT_FPS				20

#ifdef __cplusplus
extern "C" {
#endif

struct _GameType {

	String *name;

	void *data;
	void (*delete_data)(void *);

	void *(*start)(void *);             // called with the lobby when the game starts, NULL is safe
	void *(*end) (void *);              // called with the lobby when the game ends, NULL is safe

	// required lobby configuration
	bool use_default_handler;           // whether to use the lobby poll or not
	Action custom_handler;
	u32 max_players;

	// optional lobby configuration
	Action on_lobby_join;               // called when a player joins the lobby (game)
	Action on_lobby_leave;              // called when a player leaves the lobby (game)

	// called with a LobbyPlayer of the owner when it requests GAME_INIT, before the game starts
	Action init;

	// the seconds without packets after which a player leaves the lobby, 0 never
	u32 player_timeout;

	// the frames per second of the update of the lobby
	u32 fps;

};

typedef struct _GameType GameType;

CERVER_EXPORT GameType *game_type_new (void);

// creates a new game type with the passed parameters
// name is required
// NULL on delete_data to use free by default
// NULL on start or end is safe
CERVER_EXPORT GameType *game_type_create (
	const char *name, void *data, void (*delete_data)(void *),
	void *(*start)(void *), void *(*end) (void *)
);

CERVER_EXPORT void game_type_delete (void *ptr);

// add the required configuration that is needed to create a new lobby for the game type when requested
// max_players 0 takes LOBBY_DEFAULT_MAX_PLAYERS
// returns 0 on success, 1 on error
CERVER_EXPORT int game_type_add_lobby_config (
	GameType *game_type,
	bool use_default_handler, Action custom_handler, u32 max_players
);

// sets an action to be called when a player joins the lobby (game)
// a reference to the player and lobby is passed as an argument
CERVER_EXPORT void game_type_set_on_lobby_join (GameType *game_type, Action on_lobby_join);

// sets an action to be called when a player leaves the lobby (game)
// a reference to the player and lobby is passed as an argument
CERVER_EXPORT void game_type_set_on_lobby_leave (GameType *game_type, Action on_lobby_leave);

// sets an action to be called when the owner of a lobby requests GAME_INIT
// a LobbyPlayer with the owner and the lobby is passed as an argument
CERVER_EXPORT void game_type_set_init (GameType *game_type, Action init);

// sets the seconds without packets after which a player leaves its lobby, 0 never
// the default value is GAME_TYPE_DEFAULT_PLAYER_TIMEOUT
CERVER_EXPORT void game_type_set_player_timeout (GameType *game_type, const u32 player_timeout);

// sets the frames per second of the update of the lobbies of the game type, 0 is ignored
// the default value is GAME_TYPE_DEFAULT_FPS
CERVER_EXPORT void game_type_set_fps (GameType *game_type, const u32 fps);

// registers a new game type
// returns 0 on success, 1 on error
CERVER_EXPORT int game_type_register (DoubleList *game_types, GameType *game_type);

// unregister a game type
// returns 0 on success, 1 on error
CERVER_EXPORT int game_type_unregister (DoubleList *game_types, const char *name);

// gets a registered game type by its name
CERVER_EXPORT GameType *game_type_get_by_name (DoubleList *game_types, const char *name);

#ifdef __cplusplus
}
#endif

#endif
