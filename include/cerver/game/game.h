/**
 * @file cerver/game/game.h
 * @brief The game cerver: the data of a cerver of type CERVER_TYPE_GAME, its lobbies and its game types, and the
 * game requests of a client.
 */

#ifndef _CERVER_GAME_H_
#define _CERVER_GAME_H_

#include <stdbool.h>

#include <poll.h>
#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/packets/packets.h"
#include "cerver/packets/serializer.h"

#include "cerver/game/lobby.h"
#include "cerver/game/player.h"

/**
 * @defgroup game Game
 * @brief The game cerver: lobbies of players that run a game type, and their scores.
 */

/**
 * @defgroup game_cerver Game cerver
 * @ingroup game
 * @brief The data of a game cerver and its configuration.
 * @{
 */

/**
 * @brief A default player timeout in seconds.
 * @warning cerver does not use it: the game types have their own.
 * @deprecated It goes away in the 3.0.
 */
#define GAME_DEFAULT_PLAYER_TIMEOUT		30

/**
 * @brief A default fps.
 * @warning cerver does not use it: the game types have their own.
 * @deprecated It goes away in the 3.0.
 */
#define GAME_DEFAULT_FPS				20

/**
 * @brief A default of the least players of a game.
 * @warning cerver does not use it.
 * @deprecated It goes away in the 3.0.
 */
#define GAME_DEFAULT_MIN_PLAYERS		2

/**
 * @brief A default of the most players of a game.
 * @warning cerver does not use it: the lobbies use LOBBY_DEFAULT_MAX_PLAYERS.
 * @deprecated It goes away in the 3.0.
 */
#define GAME_DEFAULT_MAX_PLAYERS		4

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;
struct _Lobby;

/**
 * @brief The stats of a game cerver.
 */
typedef struct GameCerverStats {

	/** @brief The lobbies that are registered now. */
	u32 current_active_lobbys;
	/** @brief The lobbies that the players created in the life of the cerver, written with __atomic. */
	u32 lobbys_created;

} GameCerverStats;

/**
 * @brief Prints the stats of a game cerver to stdout, with printf ().
 *
 * @param[in] cerver the cerver, it can be NULL; one that is not a game cerver logs a warning
 */
CERVER_EXPORT void game_cerver_stats_print (struct _Cerver *cerver);

/**
 * @brief The data of a cerver of type CERVER_TYPE_GAME.
 */
struct _GameCerver {

	/** @brief The cerver of the game cerver. */
	struct _Cerver *cerver;

	/**
	 * @brief A recursive lock for the lobbies, their players and the lobbies of the connections; the callbacks of the
	 * game types run with it and can use the lobbies.
	 */
	pthread_mutex_t lock;

	/**
	 * @brief The threads of the lobbies that are alive; a lobby that a thread of its own ended detaches it, so
	 * game_cerver_stop () waits for all of them before the cerver ends.
	 */
	unsigned int lobby_threads;
	/** @brief Protects lobby_threads. */
	pthread_mutex_t lobby_threads_mutex;
	/** @brief Signaled when a thread of a lobby ends. */
	pthread_cond_t lobby_threads_cond;

	/** @brief The lobbies that are registered. */
	DoubleList *current_lobbys;
	/** @brief Generates the id of a new lobby, as a String; lobby_default_id_generator () by default. */
	void *(*lobby_id_generator) (const void *);

	/** @brief The game types. */
	DoubleList *game_types;

	/** @brief A comparator of the players. @warning cerver does not use it. */
	Comparator player_comparator;

	/** @brief Called with NULL by cerver_start (), to load the data of the game (to connect to a db, ...). */
	Action load_game_data;
	/** @brief Releases game_data, free () when it is NULL. */
	Action delete_game_data;
	/** @brief The data of the game. @warning cerver never sets it: only the application can, directly. */
	void *game_data;

	/** @brief Called right before the teardown of the game cerver (to send a message to every player, ...). */
	Action final_game_action;
	/** @brief The argument of the final action. */
	void *final_action_args;

	/** @brief The stats of the game cerver. */
	GameCerverStats *stats;

};

/** @brief The data of a game cerver, see struct _GameCerver. */
typedef struct _GameCerver GameCerver;

/**
 * @brief Allocates the data of a game cerver, with the default lobby id generator; cerver_create () does it for a
 * CERVER_TYPE_GAME.
 *
 * @return the new game cerver, NULL if it could not be allocated
 */
CERVER_EXPORT GameCerver *game_new (void);

/**
 * @brief Releases a game cerver, its lobbies, its game types and its game data.
 *
 * @param[in] game_ptr the game cerver, it can be NULL
 */
CERVER_EXPORT void game_delete (void *game_ptr);

/**
 * @brief Takes the recursive lock of the game cerver of a cerver.
 *
 * @param[in] cerver the cerver, it can be NULL or not be a game cerver (then it does nothing)
 */
CERVER_PUBLIC void game_cerver_lock (const struct _Cerver *cerver);

/**
 * @brief Releases the lock of the game cerver of a cerver.
 *
 * @param[in] cerver the cerver, it can be NULL or not be a game cerver (then it does nothing)
 */
CERVER_PUBLIC void game_cerver_unlock (const struct _Cerver *cerver);

/**
 * @brief Sets the cerver of a game cerver.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] cerver the cerver
 */
CERVER_EXPORT void game_set_cerver_reference (GameCerver *game_cerver, struct _Cerver *cerver);

/**
 * @brief The default id of a lobby: the SHA-256, in hex characters, of the creation time and a counter, so the
 * lobbies created in the same second have different ids.
 *
 * @param[in] data_ptr ignored
 * @return a new String with the id
 */
CERVER_EXPORT void *lobby_default_id_generator (const void *data_ptr);

/**
 * @brief Sets how the ids of the lobbies are generated.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] lobby_id_generator gets the lobby and returns a new String; it must not be NULL
 */
CERVER_EXPORT void game_set_lobby_id_generator (
	GameCerver *game_cerver, void *(*lobby_id_generator) (const void *)
);

/**
 * @brief Sets the comparator of the players.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] player_comparator the comparator
 * @warning cerver does not use it: the players of each lobby are compared by the ids of their clients.
 * @deprecated It goes away in the 3.0.
 */
DEPRECATED (CERVER_EXPORT void game_set_player_comparator (
	GameCerver *game_cerver, Comparator player_comparator
));

/**
 * @brief Sets how the data of the game is loaded and released.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] load_game_data called with NULL by cerver_start ()
 * @param[in] delete_game_data releases the game data, NULL to release it with free ()
 */
CERVER_EXPORT void game_set_load_game_data (
	GameCerver *game_cerver, Action load_game_data, Action delete_game_data
);

/**
 * @brief Sets an action that runs right before the teardown of the game cerver, like a message to every player.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] final_action the action
 * @param[in] final_action_args its argument
 */
CERVER_EXPORT void game_set_final_action (
	GameCerver *game_cerver, Action final_action, void *final_action_args
);

/**
 * @brief Handles a packet of type PACKET_TYPE_GAME; a cerver that is not a game cerver answers it with an error.
 *
 * @param[in] packet the packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void game_packet_handler (struct _Packet *packet);

/**
 * @brief Passes an app packet of a player to the packet handler of its lobby.
 *
 * @param[in] packet the packet
 * @return true if the lobby took the packet, false to handle it as any app packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool game_lobby_app_packet (struct _Packet *packet);

/**
 * @brief The player of the client of a connection leaves its lobby, when the connection ends.
 *
 * @param[in] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void game_connection_end (
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection
);

/**
 * @brief Tears down every lobby before the cerver ends its connections and its handlers, and waits for the threads
 * of the lobbies.
 *
 * @param[in] cerver the cerver
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void game_cerver_stop (struct _Cerver *cerver);

/**
 * @brief Counts a thread of a lobby that starts.
 *
 * @param[in] cerver the cerver
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void game_cerver_lobby_thread_start (const struct _Cerver *cerver);

/**
 * @brief Counts a thread of a lobby that ends, as the last thing that it does.
 *
 * @param[in] cerver the cerver
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void game_cerver_lobby_thread_end (const struct _Cerver *cerver);

/**
 * @brief Registers a lobby in the game cerver; a lobby that is already registered is logged and ignored.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] lobby the lobby, the game cerver keeps its reference
 */
CERVER_EXPORT void game_cerver_register_lobby (GameCerver *game_cerver, struct _Lobby *lobby);

/**
 * @brief Unregisters a lobby from the game cerver.
 *
 * @param[in,out] game_cerver the game cerver
 * @param[in] lobby the lobby, it is not released
 */
CERVER_EXPORT void game_cerver_unregister_lobby (GameCerver *game_cerver, struct _Lobby *lobby);

/**
 * @brief A player and its lobby, the argument of the callbacks of the game types.
 */
typedef struct LobbyPlayer {

	/** @brief The lobby. */
	struct _Lobby *lobby;
	/** @brief The player. */
	struct _Player *player;

} LobbyPlayer;

/** @} */

/**
 * @defgroup game_client Game client
 * @ingroup game
 * @brief The game requests of a client.
 * @{
 */

/**
 * @brief Sends a game request with its object to the game cerver of a connection.
 *
 * @param[in] client the client
 * @param[in] connection the connection
 * @param[in] request_type the type of the request
 * @param[in] schema the schema of the object
 * @param[in] object the object, encoded with the schema
 * @retval 0 the request was sent
 * @retval 1 a NULL argument, or the packet could not be created or sent
 */
CERVER_EXPORT u8 game_request_send (
	struct _Client *client, struct _Connection *connection,
	const u32 request_type, const SerializerSchema *schema, const void *object
);

/**
 * @brief Handles a GAME packet that a client receives; the lobby events get a LobbySummary as their response data.
 *
 * @param[in] packet the packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void game_client_packet_handler (struct _Packet *packet);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
