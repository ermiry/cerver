#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/game/gameType.h"
#include "cerver/collections/dllist.h"

static DoubleList *game_types = NULL;       // registered game types

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
            type->name = str_new (name);
            type->data = data;
            type->delete_data = delete_data;
            type->start = start;
            type->end = end;
        }
    }

    return type;

}

void game_type_delete (void *ptr) {

    if (ptr) {
        GameType *type = (GameType *) ptr;
        str_delete (type->name);
        if (type->delete_data) type->delete_data (type->data);
        else free (type->delete_data);

        free (type);
    }

}

// registers a new game type, returns 0 on LOG_SUCCESS, 1 on error
int game_type_register (GameType *game_type) {

    int retval = 1;

    if (game_type) {
        retval = dlist_insert_after (game_types, 
            dlist_end (game_types), game_type) == true ? 0 : 1;
    }

    return retval;

}

// unregister a game type, returns 0 on LOG_SUCCESS, 1 on error
int game_type_unregister (const char *name) {

    int retval = 1;

    if (name) {
        GameType *game_type = NULL;
        for (ListElement *le = dlist_start (game_types); le; le = le->next) {
            game_type = (GameType *) le->data;
            if (!strcmp (name, game_type->name->str)) {
                game_type_delete (dlist_remove_element (game_types, le));
                retval =0;
            }
        }
    }

    return retval;

}

// gets a registered game type by its name
GameType *game_type_get_by_name (const char *name) {

    GameType *game_type = NULL;

    if (name) {
        for (ListElement *le = dlist_start (game_types); le; le = le->next) {
            game_type = (GameType *) le->data;
            if (!strcmp (name, game_type->name->str)) break;
        }
    }

    return game_type;

}