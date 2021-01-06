#ifndef _CERVER_GAME_H_
#define _CERVER_GAME_H_

#include <poll.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/packets.h"

#include "cerver/game/player.h"
#include "cerver/game/lobby.h"

#define DEFAULT_PLAYER_TIMEOUT      30
#define DEFAULT_FPS                 20
#define DEFAULT_MIN_PLAYERS         2
#define DEFAULT_MAX_PLAYERS         4

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Packet;

typedef struct GameCerverStats {

    u32 current_active_lobbys;      // total current lobbys 
    u32 lobbys_created;             // total amount of lobbys that were created in cerver life span

} GameCerverStats;

extern void game_cerver_stats_print (struct _Cerver *cerver);

struct _GameCerver {

    struct _Cerver *cerver;                         // refernce to the cerver this is located

    DoubleList *current_lobbys;                     // a list of the current lobbys
    void *(*lobby_id_generator) (const void *);

    DoubleList *game_types;

    Comparator player_comparator;

    // we can define a function to load game data at start, 
    // for example to connect to a db or something like that
    Action load_game_data;
    Action delete_game_data;
    void *game_data;

    // action to be performed right before the game server teardown
    Action final_game_action;
    void *final_action_args;

    GameCerverStats *stats;

};

typedef struct _GameCerver GameCerver;

extern GameCerver *game_new (void);

extern void game_delete (void *game_ptr);

/*** Game Cerver Configuration ***/

// sets the game cerver's cerver reference
extern void game_set_cerver_reference (GameCerver *game_cerver, struct _Cerver *cerver);

extern void *lobby_default_id_generator (const void *data_ptr);

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

/*** Game Cerver Lobbys ***/

// registers a new lobby to the game cerver
extern void game_cerver_register_lobby (GameCerver *game_cerver, struct _Lobby *lobby);

// unregisters a lobby from the game cerver
extern void game_cerver_unregister_lobby (GameCerver *game_cerver, struct _Lobby *lobby);

typedef struct LobbyPlayer {

    struct _Lobby *lobby;
    struct _Player *player;

} LobbyPlayer;

#ifdef __cplusplus
}
#endif

#endif