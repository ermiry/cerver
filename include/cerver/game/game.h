#ifndef _CERVER_GAME_H_
#define _CERVER_GAME_H_

#include <stdbool.h>

#include <poll.h>
#include <pthread.h>

#include "cerver/config.h"

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/packets.h"
#include "cerver/serializer.h"

#include "cerver/game/player.h"
#include "cerver/game/lobby.h"

#define GAME_DEFAULT_PLAYER_TIMEOUT		30
#define GAME_DEFAULT_FPS				20
#define GAME_DEFAULT_MIN_PLAYERS		2
#define GAME_DEFAULT_MAX_PLAYERS		4

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _Lobby;

typedef struct GameCerverStats {

	u32 current_active_lobbys;      // total current lobbys
	u32 lobbys_created;             // total amount of lobbys that were created in cerver life span

} GameCerverStats;

CERVER_EXPORT void game_cerver_stats_print (struct _Cerver *cerver);

struct _GameCerver {

	struct _Cerver *cerver;                         // refernce to the cerver this is located

	// a recursive lock for the lobbies, their players and the lobbies of the connections,
	// the callbacks of the game types run with it and can use the lobbies
	pthread_mutex_t lock;

	// the threads of the lobbies that are alive, a lobby that a thread of its own ended
	// detaches it, so game_cerver_stop () waits for all of them before the cerver ends
	unsigned int lobby_threads;
	pthread_mutex_t lobby_threads_mutex;
	pthread_cond_t lobby_threads_cond;

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

CERVER_EXPORT GameCerver *game_new (void);

CERVER_EXPORT void game_delete (void *game_ptr);

// takes and releases the lock of the game cerver of the cerver, NULL is safe
CERVER_PUBLIC void game_cerver_lock (const struct _Cerver *cerver);

CERVER_PUBLIC void game_cerver_unlock (const struct _Cerver *cerver);

/*** Game Cerver Configuration ***/

// sets the game cerver's cerver reference
CERVER_EXPORT void game_set_cerver_reference (GameCerver *game_cerver, struct _Cerver *cerver);

// the default id of a lobby: the hash of the creation time and a counter,
// so the lobbies created in the same second have different ids
CERVER_EXPORT void *lobby_default_id_generator (const void *data_ptr);

// option to set the game cerver lobby id generator
CERVER_EXPORT void game_set_lobby_id_generator (
	GameCerver *game_cerver, void *(*lobby_id_generator) (const void *)
);

// option to set the game cerver comparator
CERVER_EXPORT void game_set_player_comparator (
	GameCerver *game_cerver, Comparator player_comparator
);

// sets a way to get and destroy game cerver game data
CERVER_EXPORT void game_set_load_game_data (
	GameCerver *game_cerver, Action load_game_data, Action delete_game_data
);

// option to set an action to be performed right before the game cerver teardown
// eg. send a message to all players
CERVER_EXPORT void game_set_final_action (
	GameCerver *game_cerver, Action final_action, void *final_action_args
);

// handles a game type packet, a cerver that is not a game cerver answers it with an error
CERVER_PRIVATE void game_packet_handler (struct _Packet *packet);

// passes an app packet of a player to the packet handler of its lobby
// returns true if the lobby took the packet, false to handle it as any app packet
CERVER_PRIVATE bool game_lobby_app_packet (struct _Packet *packet);

// the player of the client of the connection leaves its lobby, when the connection ends
CERVER_PRIVATE void game_connection_end (
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection
);

// tears down every lobby before the cerver ends its connections and its handlers,
// and waits for the threads of the lobbies
CERVER_PRIVATE void game_cerver_stop (struct _Cerver *cerver);

// counts a thread of a lobby that starts, and one that ends as the last thing it does
CERVER_PRIVATE void game_cerver_lobby_thread_start (const struct _Cerver *cerver);

CERVER_PRIVATE void game_cerver_lobby_thread_end (const struct _Cerver *cerver);

/*** Game Cerver Lobbys ***/

// registers a new lobby to the game cerver
CERVER_EXPORT void game_cerver_register_lobby (GameCerver *game_cerver, struct _Lobby *lobby);

// unregisters a lobby from the game cerver
CERVER_EXPORT void game_cerver_unregister_lobby (GameCerver *game_cerver, struct _Lobby *lobby);

typedef struct LobbyPlayer {

	struct _Lobby *lobby;
	struct _Player *player;

} LobbyPlayer;

/*** Game Client ***/

// sends a game request with its object to the game cerver of the connection
// returns 0 on success, 1 on error
CERVER_EXPORT u8 game_request_send (
	struct _Client *client, struct _Connection *connection,
	const u32 request_type, const SerializerSchema *schema, const void *object
);

// handles a GAME packet that a client receives,
// the lobby events get a LobbySummary as their response data
CERVER_PRIVATE void game_client_packet_handler (struct _Packet *packet);

#ifdef __cplusplus
}
#endif

#endif
