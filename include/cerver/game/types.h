/**
 * @file cerver/game/types.h
 * @brief The game types of a game cerver: what a lobby of each game is like.
 */

#ifndef _CERVER_GAME_TYPES_H_
#define _CERVER_GAME_TYPES_H_

#include <stdbool.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

/**
 * @defgroup game_types Game types
 * @ingroup game
 * @brief The games that the lobbies of a game cerver can run.
 * @{
 */

/** @brief The default seconds without packets after which a player leaves its lobby: 0 keeps them. */
#define GAME_TYPE_DEFAULT_PLAYER_TIMEOUT	0

/** @brief The default frames per second of the update of a lobby. */
#define GAME_TYPE_DEFAULT_FPS				20

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A game type: its name, its data, its callbacks and the configuration of its lobbies.
 */
struct _GameType {

	/** @brief The name, that the requests of the clients use. */
	String *name;

	/** @brief The data of the game type. */
	void *data;
	/** @brief Releases the data, free () when it is NULL. */
	void (*delete_data)(void *);

	/** @brief Called with the lobby when the game starts, it can be NULL. */
	void *(*start)(void *);
	/** @brief Called with the lobby when the game ends, it can be NULL. */
	void *(*end) (void *);

	/** @brief The lobbies use the lobby poll (the default handler). */
	bool use_default_handler;
	/** @brief The handler of the lobbies when they do not use the default one. */
	Action custom_handler;
	/** @brief The max players of the lobbies, 0 for LOBBY_DEFAULT_MAX_PLAYERS. */
	u32 max_players;

	/** @brief Called when a player joins a lobby, with the player and the lobby. */
	Action on_lobby_join;
	/** @brief Called when a player leaves a lobby, with the player and the lobby. */
	Action on_lobby_leave;

	/** @brief Called with a LobbyPlayer of the owner when it requests GAME_INIT, before the game starts. */
	Action init;

	/** @brief The seconds without packets after which a player leaves the lobby, 0 never. */
	u32 player_timeout;

	/** @brief The frames per second of the update of the lobby. */
	u32 fps;

};

/** @brief A game type, see struct _GameType. */
typedef struct _GameType GameType;

/**
 * @brief Allocates a game type with the default player timeout and fps.
 *
 * @return the new game type, NULL if it could not be allocated
 */
CERVER_EXPORT GameType *game_type_new (void);

/**
 * @brief Creates a game type that uses the default handler.
 *
 * @param[in] name the name, required
 * @param[in] data the data, the game type owns it
 * @param[in] delete_data releases the data, NULL to release it with free ()
 * @param[in] start called with the lobby when the game starts, it can be NULL
 * @param[in] end called with the lobby when the game ends, it can be NULL
 * @return the new game type, released with game_type_delete (); NULL without a name or if it could not be allocated
 */
CERVER_EXPORT GameType *game_type_create (
	const char *name, void *data, void (*delete_data)(void *),
	void *(*start)(void *), void *(*end) (void *)
);

/**
 * @brief Releases a game type and its data.
 *
 * @param[in] ptr the game type, it can be NULL
 */
CERVER_EXPORT void game_type_delete (void *ptr);

/**
 * @brief Sets how a lobby of the game type is created when it is requested.
 *
 * @param[in,out] game_type the game type
 * @param[in] use_default_handler true for the lobby poll
 * @param[in] custom_handler the handler of the lobbies without the default one
 * @param[in] max_players the max players, 0 takes LOBBY_DEFAULT_MAX_PLAYERS
 * @retval 0 the configuration was set
 * @retval 1 a NULL game type
 */
CERVER_EXPORT int game_type_add_lobby_config (
	GameType *game_type,
	bool use_default_handler, Action custom_handler, u32 max_players
);

/**
 * @brief Sets the action that is called when a player joins a lobby of the game type.
 *
 * @param[in,out] game_type the game type
 * @param[in] on_lobby_join gets a reference to the player and to the lobby
 */
CERVER_EXPORT void game_type_set_on_lobby_join (GameType *game_type, Action on_lobby_join);

/**
 * @brief Sets the action that is called when a player leaves a lobby of the game type.
 *
 * @param[in,out] game_type the game type
 * @param[in] on_lobby_leave gets a reference to the player and to the lobby
 */
CERVER_EXPORT void game_type_set_on_lobby_leave (GameType *game_type, Action on_lobby_leave);

/**
 * @brief Sets the action that is called when the owner of a lobby requests GAME_INIT.
 *
 * @param[in,out] game_type the game type
 * @param[in] init gets a LobbyPlayer with the owner and the lobby
 */
CERVER_EXPORT void game_type_set_init (GameType *game_type, Action init);

/**
 * @brief Sets the seconds without packets after which a player leaves its lobby.
 *
 * @param[in,out] game_type the game type
 * @param[in] player_timeout the seconds, 0 never; the default is #GAME_TYPE_DEFAULT_PLAYER_TIMEOUT
 */
CERVER_EXPORT void game_type_set_player_timeout (GameType *game_type, const u32 player_timeout);

/**
 * @brief Sets the frames per second of the update of the lobbies of the game type.
 *
 * @param[in,out] game_type the game type
 * @param[in] fps the frames per second, 0 is ignored; the default is #GAME_TYPE_DEFAULT_FPS
 */
CERVER_EXPORT void game_type_set_fps (GameType *game_type, const u32 fps);

/**
 * @brief Registers a game type in a list.
 *
 * @param[in,out] game_types the list of the game types of the game cerver
 * @param[in] game_type the game type, the list owns it
 * @retval 0 the game type was registered
 * @retval 1 a NULL game type or it could not be inserted
 */
CERVER_EXPORT int game_type_register (DoubleList *game_types, GameType *game_type);

/**
 * @brief Unregisters and releases the game type with a name.
 *
 * @param[in,out] game_types the list of the game types
 * @param[in] name the name
 * @retval 0 the game type was unregistered
 * @retval 1 a NULL name or no game type has it
 */
CERVER_EXPORT int game_type_unregister (DoubleList *game_types, const char *name);

/**
 * @brief Returns the registered game type with a name.
 *
 * @param[in] game_types the list of the game types
 * @param[in] name the name
 * @return the game type, NULL for a NULL name or if no game type has it
 */
CERVER_EXPORT GameType *game_type_get_with_name (DoubleList *game_types, const char *name);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
