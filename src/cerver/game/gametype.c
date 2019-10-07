#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/types/estring.h"
#include "cerver/game/gametype.h"
#include "cerver/collections/dllist.h"

GameType *game_type_get_by_name (DoubleList *game_types, const char *name);

GameType *game_type_new (void) {

    GameType *type = (GameType *) malloc (sizeof (GameType));
    if (type) {
        memset (type, 0, sizeof (GameType));
        type->name = NULL;
        type->data = NULL;
        type->delete_data = NULL;
        type->start = type->end = NULL;
    }

    return type;

}

// creates a new game type with the passed parameters
// name is required
// NULL on delete_data to use free by default
// NULL on start or end is safe
GameType *game_type_create (const char *name, void *data, void (*delete_data)(void *),
    void *(*start)(void *), void *(*end) (void *)) {

    GameType *type = NULL;
    if (name) {
        type = game_type_new ();
        if (type) {
            type->name = estring_new (name);
            type->data = data;
            type->delete_data = delete_data;
            type->start = start;
            type->end = end;

            type->use_default_handler = true;
            type->custom_handler = NULL;
            type->max_players = 0;
        }
    }

    return type;

}

void game_type_delete (void *ptr) {

    if (ptr) {
        GameType *type = (GameType *) ptr;
        estring_delete (type->name);
        if (type->delete_data) type->delete_data (type->data);
        else free (type->data);

        free (type);
    }

}

// add the required configuration that is needed to create a new lobby for the game type when requested
// returns 0 on success, 1 on error
int game_type_add_lobby_config (GameType *game_type,
    bool use_default_handler, Action custom_handler, u32 max_players) {

    int retval = 0;

    if (game_type) {
        game_type->use_default_handler = use_default_handler;
        game_type->custom_handler = custom_handler;
        game_type->max_players = max_players;
    }

    return retval;

}

// sets an action to be called when a player joins the lobby (game)
// a reference to the player and lobby is passed as an argument
void game_type_set_on_lobby_join (GameType *game_type, Action on_lobby_join) {

    if (game_type) game_type->on_lobby_join = on_lobby_join;

}

// sets an action to be called when a player leaves the lobby (game)
// a reference to the player and lobby is passed as an argument
void game_type_set_on_lobby_leave (GameType *game_type, Action on_lobby_leave) {
    
    if (game_type) game_type->on_lobby_leave = on_lobby_leave;

}

// registers a new game type, returns 0 on LOG_SUCCESS, 1 on error
int game_type_register (DoubleList *game_types, GameType *game_type) {

    int retval = 1;

    if (game_type) {
        retval = dlist_insert_after (game_types, 
            dlist_end (game_types), game_type) == true ? 0 : 1;
    }

    return retval;

}

// unregister a game type, returns 0 on LOG_SUCCESS, 1 on error
int game_type_unregister (DoubleList *game_types, const char *name) {

    int retval = 1;

    if (name) {
        GameType *game_type = NULL;
        for (ListElement *le = dlist_start (game_types); le; le = le->next) {
            game_type = (GameType *) le->data;
            if (!strcmp (name, game_type->name->str)) {
                game_type_delete (dlist_remove_element (game_types, le));
                retval = 0;
            }
        }
    }

    return retval;

}

// gets a registered game type by its name
GameType *game_type_get_by_name (DoubleList *game_types, const char *name) {

    GameType *game_type = NULL;

    if (name) {
        for (ListElement *le = dlist_start (game_types); le; le = le->next) {
            game_type = (GameType *) le->data;
            if (!strcmp (name, game_type->name->str)) break;
        }
    }

    return game_type;

}