/**
 * @file cerver/game/player.h
 * @brief The players of the lobbies: a client in a lobby, with its id and its data.
 */

#ifndef _CERVER_GAME_PLAYER_H_
#define _CERVER_GAME_PLAYER_H_

#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/core/cerver.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Packet;
struct _Lobby;

/**
 * @defgroup game_player Players
 * @ingroup game
 * @brief A client inside a lobby.
 * @{
 */

/**
 * @brief A player of a lobby.
 */
struct _Player {

	/** @brief The id of the player, NULL until player_set_id (). */
	String *id;

	/** @brief The client of the player, with its connections. */
	struct _Client *client;

	/**
	 * @brief The milliseconds of the monotonic clock (player_activity_now ()) when the player sent a packet to its
	 * lobby, read and written atomically.
	 */
	u64 last_activity;

	/** @brief The data of the player. */
	void *data;
	/** @brief Releases the data, free () when it is NULL. */
	Action data_delete;

};

/** @brief A player, see struct _Player. */
typedef struct _Player Player;

/**
 * @brief Allocates a player without an id or a client, active now.
 *
 * @return the new player, NULL if it could not be allocated
 */
CERVER_EXPORT Player *player_new (void);

/**
 * @brief Releases a player, its id and its data; not its client.
 *
 * @param[in] player_ptr the player, it can be NULL
 */
CERVER_EXPORT void player_delete (void *player_ptr);

/**
 * @brief Sets the id of a player, releasing the previous one.
 *
 * @param[in,out] player the player
 * @param[in] id the id, copied
 */
CERVER_EXPORT void player_set_id (Player *player, const char *id);

/**
 * @brief Sets the data of a player and how to release it.
 *
 * @param[in,out] player the player
 * @param[in] data the data, the player owns it
 * @param[in] data_delete releases the data, NULL to release it with free ()
 * @warning Data that was already set is replaced without being released.
 */
CERVER_EXPORT void player_set_data (Player *player, void *data, Action data_delete);

/**
 * @brief Compares two players by their ids.
 *
 * @param[in] a the first player, it must not be NULL
 * @param[in] b the second player, it must not be NULL
 * @return as str_compare () of their ids
 */
CERVER_EXPORT int player_comparator_with_id (const void *a, const void *b);

/**
 * @brief Compares two players by the ids of their clients.
 *
 * @param[in] a the first player, it must not be NULL
 * @param[in] b the second player, it must not be NULL
 * @return as client_comparator_client_id () of their clients
 */
CERVER_EXPORT int player_comparator_client_id (const void *a, const void *b);

/**
 * @brief Returns the milliseconds of the monotonic clock that the activity of the players uses.
 *
 * @return the milliseconds
 */
CERVER_PUBLIC u64 player_activity_now (void);

/**
 * @brief Records that the player sent a packet now.
 *
 * @param[in,out] player the player, it can be NULL
 * @threadsafety Safe from any thread, it writes the activity atomically.
 */
CERVER_PUBLIC void player_activity_update (Player *player);

/**
 * @brief Registers each connection of the client of the player in the lobby poll.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player, with its client
 * @retval 0 every connection was registered
 * @retval 1 a NULL argument or a connection that was not registered
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 player_register_to_lobby_poll (struct _Lobby *lobby, Player *player);

/**
 * @brief Registers a player in the lobby: adds it to its players and sets the lobby of its connections.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player, with its client; the lobby owns it
 * @retval 0 the player was registered
 * @retval 1 a NULL argument or it could not be inserted
 * @note For the internal methods of cerver, with the lock of the game cerver.
 */
CERVER_PRIVATE u8 player_register_to_lobby (struct _Lobby *lobby, Player *player);

/**
 * @brief Unregisters each connection of the client of the player from the lobby poll.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player, with its client
 * @retval 0 every connection was unregistered
 * @retval 1 a NULL argument or a connection that was not unregistered
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 player_unregister_from_lobby_poll (struct _Lobby *lobby, Player *player);

/**
 * @brief Unregisters a player from the lobby and gives the owner to the next player.
 *
 * The player is not released and an empty lobby is not torn down.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player
 * @retval 0 the player was unregistered
 * @retval 1 a NULL argument or the player is not in the lobby
 * @note For the internal methods of cerver, with the lock of the game cerver.
 */
CERVER_PRIVATE u8 player_unregister_from_lobby (struct _Lobby *lobby, Player *player);

/**
 * @brief Searches a player of the lobby that matches a query, with the comparator of the players of the lobby (the
 * client id).
 *
 * @param[in] lobby the lobby
 * @param[in] query the player to compare
 * @return the player, NULL for a NULL lobby or without a match
 */
CERVER_EXPORT Player *player_get_from_lobby (struct _Lobby *lobby, Player *query);

/**
 * @brief Returns the element of the list of players of the lobby that has the player.
 *
 * @param[in] lobby the lobby
 * @param[in] player the player
 * @return the element, NULL for a NULL lobby or without a match
 */
CERVER_EXPORT ListElement *player_get_le_from_lobby (struct _Lobby *lobby, Player *player);

/**
 * @brief Returns the player of the lobby that has a connection with a socket.
 *
 * @param[in] lobby the lobby
 * @param[in] sock_fd the socket
 * @return the player, NULL for a NULL lobby or without a match
 * @warning It reads the players without the lock of the game cerver.
 */
CERVER_EXPORT Player *player_get_with_sock_fd_list (struct _Lobby *lobby, i32 sock_fd);

/**
 * @brief Sends a packet to every connection of every player in the lobby, with the lock of the game cerver.
 *
 * @param[in] cerver the cerver
 * @param[in] lobby the lobby
 * @param[in,out] packet the packet, its network values are set for each connection
 * @param[in] protocol ignored, the packet goes through the connections of the players
 * @param[in] flags the flags of the send
 */
CERVER_EXPORT void player_broadcast_to_all (
	struct _Cerver *cerver, const struct _Lobby *lobby, struct _Packet *packet,
	Protocol protocol, int flags
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
