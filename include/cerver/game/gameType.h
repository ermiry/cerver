#ifndef _CENGINE_GAME_TYPE_H_
#define _CENGINE_GAME_TYPE_H_

#include "cerver/types/string.h"

struct _GameType {

    String *name;

    void *data;
    void (*delete_data)(void *);

    void *(*start)(void *);         // called when the game starts
    void *(*end) (void *);          // called when the game ends

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

// registers a new game type, returns 0 on LOG_SUCCESS, 1 on error
extern int game_type_register (GameType *game_type);

// unregister a game type, returns 0 on LOG_SUCCESS, 1 on error
extern int game_type_unregister (const char *name);

// gets a registered game type by its name
extern GameType *game_type_get_by_name (const char *name);

#endif