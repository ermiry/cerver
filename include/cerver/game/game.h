#ifndef _CERVER_GAME_H_
#define _CERVER_GAME_H_

#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"
#include "cerver/packets.h"
#include "cerver/game/player.h"
#include "cerver/game/lobby.h"

#include "cerver/collections/dllist.h"
#include "cerver/collections/avl.h"

#include "cerver/utils/config.h"
#include "cerver/utils/objectPool.h"

struct _Cerver;
struct _GameServerData;
struct _Client;
struct _Packet;

#define DEFAULT_PLAYER_TIMEOUT      30
#define DEFAULT_FPS                 20
#define DEFAULT_MIN_PLAYERS         2
#define DEFAULT_MAX_PLAYERS         4    

struct _GameCerver {

    DoubleList *current_lobbys;                     // a list of the current lobbys
    void *(*lobby_id_generator) (const void *);

    Comparator player_comparator;

    // we can define a function to load game data at start, 
    // for example to connect to a db or something like that
    Action load_game_data;
    Action delete_game_data;
    void *game_data;

    // action to be performed right before the game server teardown
    Action final_game_action;
    void *final_action_args;

};

typedef struct _GameCerver GameCerver;

extern GameCerver *game_new (void);

extern void game_delete (void *game_ptr);

/*** Game Cerver Configuration ***/

// option to set the game cerver lobby id generator
extern void game_set_lobby_id_generator (GameCerver *game_cerver, 
    void *(*lobby_id_generator) (const void *));

// option to set the game cerver comparator
extern void game_set_player_comparator (GameCerver *game_cerver, 
    Comparator player_comparator);

// sets a way to get and destroy game cerver game data
extern void game_set_load_game_data (GameCerver *game_cerver, 
    Action load_game_data, Action delete_game_data);

// option to set an action to be performed right before the game cerver teardown
// eg. send a message to all players
extern void game_set_final_action (GameCerver *game_cerver, 
    Action final_action, void *final_action_args);

// handles a game type packet
extern void game_packet_handler (struct _Packet *packet);

#endif