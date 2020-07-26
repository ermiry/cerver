#ifndef _CENGINE_GAME_TYPE_H_
#define _CENGINE_GAME_TYPE_H_

#include <stdbool.h>

#include "cerver/types/estring.h"
#include "cerver/collections/dlist.h"

struct _GameType {

	estring *name;

	void *data;
	void (*delete_data)(void *);

	void *(*start)(void *);             // called when the game starts
	void *(*end) (void *);              // called when the game ends

	// required lobby configuration
	bool use_default_handler;           // whether to use the lobby poll or not
	Action custom_handler;
	u32 max_players;

	// optional lobby configuration
	Action on_lobby_join;               // called when a player joins the lobby (game)
	Action on_lobby_leave;              // called when a player leaves the lobby (game)

};

typedef struct _GameType GameType;

extern GameType *game_type_new (void);

// creates a new game type with the passed parameters
// name is required
// NULL on delete_data to use free by default
// NULL on start or end is safe
extern GameType *game_type_create (const char *name, void *data, void (*delete_data)(void *),
	void *(*start)(void *), void *(*end) (void *));

extern void game_type_delete (void *ptr);

// add the required configuration that is needed to create a new lobby for the game type when requested
// returns 0 on success, 1 on error
extern int game_type_add_lobby_config (GameType *game_type,
	bool use_default_handler, Action custom_handler, u32 max_players);

// sets an action to be called when a player joins the lobby (game)
// a reference to the player and lobby is passed as an argument
extern void game_type_set_on_lobby_join (GameType *game_type, Action on_lobby_join);

// sets an action to be called when a player leaves the lobby (game)
// a reference to the player and lobby is passed as an argument
extern void game_type_set_on_lobby_leave (GameType *game_type, Action on_lobby_leave);

// registers a new game type
// returns 0 on success, 1 on error
extern int game_type_register (DoubleList *game_types, GameType *game_type);

// unregister a game type
// returns 0 on success, 1 on error
extern int game_type_unregister (DoubleList *game_types, const char *name);

// gets a registered game type by its name
extern GameType *game_type_get_by_name (DoubleList *game_types, const char *name);

#endif