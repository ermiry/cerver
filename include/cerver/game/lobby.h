/**
 * @file cerver/game/lobby.h
 * @brief The lobbies of a game cerver: their players, their poll, their threads and the objects that they send.
 */

#ifndef _CERVER_GAME_LOBBY_H_
#define _CERVER_GAME_LOBBY_H_

#include <stdbool.h>
#include <time.h>

#include <poll.h>
#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/packets/serializer.h"

#include "cerver/game/game.h"
#include "cerver/game/player.h"
#include "cerver/game/types.h"

#include "cerver/threads/thread.h"

/**
 * @defgroup game_lobby Lobbies
 * @ingroup game
 * @brief A group of players that runs a game type.
 * @{
 */

/** @brief The default timeout in milliseconds of the lobby poll. */
#define LOBBY_DEFAULT_POLL_TIMEOUT			2000

/** @brief The default max players of a lobby. */
#define LOBBY_DEFAULT_MAX_PLAYERS			4

/** @brief The size of the id of a lobby in its objects, with its terminator. */
#define LOBBY_ID_SIZE						80

/** @brief The size of the name of the game type in the lobby objects, with its terminator. */
#define LOBBY_GAME_TYPE_SIZE				64

/** @brief The size of the id of a player in the lobby objects, with its terminator. */
#define LOBBY_PLAYER_ID_SIZE				64

/** @brief The players that a LobbySummary sends, a lobby can have more. */
#define LOBBY_SUMMARY_PLAYERS_MAX			32

/** @brief The version of the schemas of the lobby objects. */
#define LOBBY_OBJECTS_VERSION				1

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _GameCerver;
struct _Player;
struct _Lobby;
struct _Packet;
struct _PacketsPerType;

/**
 * @brief The settings of a game, that the application sets with lobby_set_game_settings ().
 *
 * @warning cerver does not read them: the lobbies take their configuration from their game type.
 */
struct _GameSettings {

	/** @brief The game type. */
	GameType *game_type;
	/** @brief The seconds until a player is dropped. */
	u8 player_timeout;
	/** @brief The frames per second. */
	u8 fps;

	/** @brief The least players. */
	u8 min_players;
	/** @brief The most players. */
	u8 max_players;
	/** @brief The duration in seconds, -1 for an infinite duration. */
	int duration;

};

/** @brief The settings of a game, see struct _GameSettings. */
typedef struct _GameSettings GameSettings;

/**
 * @brief The stats of a lobby.
 */
typedef struct LobbyStats {

	/**
	 * @brief How often the stats should be reset.
	 *
	 * @warning It is only stored and printed: it starts at 0 and cerver does not reset the stats with it.
	 * @deprecated Only the stats of the cerver are reset (CerverStats::threshold_time); it goes away in the 3.0.
	 */
	time_t threshold_time;

	/** @brief The cerver packets received (packet header and data). */
	u64 n_packets_received;
	/** @brief The calls to recv (). */
	u64 n_receives_done;
	/** @brief The bytes received. */
	u64 bytes_received;

	/** @brief The packets sent. */
	u64 n_packets_sent;
	/** @brief The bytes sent. */
	u64 bytes_sent;

	/** @brief The packets received, by packet type. */
	struct _PacketsPerType *received_packets;
	/** @brief The packets sent, by packet type. */
	struct _PacketsPerType *sent_packets;

} LobbyStats;

/**
 * @brief Sets how often the stats of a lobby should be reset.
 *
 * @param[in,out] lobby the lobby
 * @param[in] threshold_time the seconds
 * @warning The value is only stored and printed, see LobbyStats::threshold_time.
 * @deprecated Only the stats of the cerver are reset, with cerver_stats_set_threshold_time ().
 */
DEPRECATED (CERVER_EXPORT void lobby_stats_set_threshold_time (struct _Lobby *lobby, time_t threshold_time));

/**
 * @brief Prints the stats of a lobby to stdout, with printf ().
 *
 * @param[in] lobby the lobby, a NULL one logs a warning
 */
CERVER_EXPORT void lobby_stats_print (struct _Lobby *lobby);

/**
 * @brief A lobby.
 *
 * @threadsafety The players, the owner, the counts and the state are read and written with the lock of its game
 * cerver (game_cerver_lock ()).
 */
struct _Lobby {

	/** @brief The cerver that the lobby belongs to. */
	struct _Cerver *cerver;

	/** @brief The unique id of the lobby, from the lobby id generator of the game cerver. */
	String *id;
	/** @brief When the lobby was created. */
	time_t creation_time_stamp;

	/**
	 * @brief The references: the game cerver keeps one, and each thread, packet and event of the lobby another; the
	 * last one that is released deletes the lobby.
	 */
	unsigned int references;

	/** @brief The lobby poll, that takes the connections of the players from the cerver poll while the game runs (CERVER_HANDLER_TYPE_POLL). */
	struct pollfd *players_fds;
	/** @brief The size of players_fds. */
	u32 max_players_fds;
	/** @brief The active fds in players_fds. */
	u32 current_players_fds;
	/** @brief The timeout in milliseconds of the lobby poll. */
	u32 poll_timeout;
	/** @brief The lobby poll has the connections of its players. */
	bool poll_connections;
	/** @brief A pipe that wakes up the lobby poll to stop it. */
	int wake_fds[2];

	/** @brief The lobby is listening for the packets of the players. */
	bool running;
	/** @brief The lobby is inside a game. */
	bool in_game;
	/** @brief The game was initialized with GAME_INIT. */
	bool initialized;
	/** @brief The lobby was torn down. */
	bool ended;

	/** @brief The players in the lobby. */
	DoubleList *players;
	/** @brief The player that created the lobby, it has higher privileges. */
	struct _Player *owner;
	/** @brief The most players of the lobby. */
	unsigned int max_players;
	/** @brief The players in the lobby now. */
	unsigned int n_current_players;

	/** @brief The thread of the handler. */
	pthread_t handler_thread_id;
	/** @brief The thread of the handler was started. */
	bool handler_started;
	/** @brief The lobby uses its default handler, the lobby poll. */
	bool default_handler;
	/** @brief The handler of the lobby (lobby_poll () by default), it gets a CerverLobby. */
	Action handler;
	/** @brief Takes the app packets of the players while the lobby runs. */
	Action packet_handler;

	/** @brief The game type of the lobby. */
	GameType *game_type;

	/** @brief The settings of the game. */
	void *game_settings;
	/** @brief Releases the settings, free () when it is NULL. */
	Action game_settings_delete;

	/** @brief The data of the game. */
	void *game_data;
	/** @brief Releases the data, free () when it is NULL. */
	Action game_data_delete;

	/** @brief The thread of the update. */
	pthread_t update_thread_id;
	/** @brief The thread of the update was started. */
	bool update_started;
	/** @brief Called every frame (the fps of the game type) with a CerverLobby while the lobby runs. */
	Action update;

	/** @brief The update thread waits on it between its frames, so the teardown stops it at once. */
	pthread_mutex_t stop_mutex;
	/** @brief Signaled by the teardown. */
	pthread_cond_t stop_cond;

	/** @brief The stats of the lobby. */
	LobbyStats *stats;

};

/** @brief A lobby, see struct _Lobby. */
typedef struct _Lobby Lobby;

/**
 * @brief Allocates a lobby with the reference of its creator.
 *
 * @return the new lobby, NULL if it could not be allocated
 */
CERVER_EXPORT Lobby *lobby_new (void);

/**
 * @brief Releases a lobby right away, with its players, its settings and its data.
 *
 * @param[in] lobby_ptr the lobby, it can be NULL
 * @note Use lobby_release () for a lobby that has references.
 */
CERVER_EXPORT void lobby_delete (void *lobby_ptr);

/**
 * @brief Takes a reference to a lobby.
 *
 * @param[in,out] lobby the lobby, it can be NULL
 * @return the lobby
 * @threadsafety Safe from any thread, with __atomic.
 */
CERVER_PUBLIC Lobby *lobby_reference (Lobby *lobby);

/**
 * @brief Releases a reference to a lobby; the last one deletes it.
 *
 * @param[in,out] lobby the lobby, it can be NULL
 * @threadsafety Safe from any thread, with __atomic.
 */
CERVER_PUBLIC void lobby_release (Lobby *lobby);

/**
 * @brief Compares two lobbies by their ids.
 *
 * @param[in] one the first lobby
 * @param[in] two the second lobby
 * @return as str_compare () of their ids; a NULL lobby goes before the other one
 */
CERVER_EXPORT int lobby_comparator (const void *one, const void *two);

/**
 * @brief Sets the timeout of the lobby poll: how often it checks for new packets.
 *
 * @param[in,out] lobby the lobby
 * @param[in] timeout the milliseconds, the default is #LOBBY_DEFAULT_POLL_TIMEOUT
 */
CERVER_EXPORT void lobby_set_poll_time_out (Lobby *lobby, unsigned int timeout);

/**
 * @brief Sets the handler of a lobby.
 *
 * @param[in,out] lobby the lobby
 * @param[in] handler the handler, it gets a CerverLobby
 */
CERVER_EXPORT void lobby_set_handler (Lobby *lobby, Action handler);

/**
 * @brief Sets the handler of the app packets of the players while the lobby runs.
 *
 * @param[in,out] lobby the lobby
 * @param[in] packet_handler gets the packet, that is released after it
 */
CERVER_EXPORT void lobby_set_packet_handler (Lobby *lobby, Action packet_handler);

/**
 * @brief Sets the settings of the game of a lobby and how to release them.
 *
 * @param[in,out] lobby the lobby
 * @param[in] game_settings the settings, the lobby owns them
 * @param[in] game_settings_delete releases them, NULL to release them with free ()
 * @warning Settings that were already set are replaced without being released.
 */
CERVER_EXPORT void lobby_set_game_settings (
	Lobby *lobby, void *game_settings, Action game_settings_delete
);

/**
 * @brief Sets the data of the game of a lobby and how to release it.
 *
 * @param[in,out] lobby the lobby
 * @param[in] game_data the data, the lobby owns it
 * @param[in] game_data_delete releases it, NULL to release it with free ()
 * @warning Data that was already set is replaced without being released.
 */
CERVER_EXPORT void lobby_set_game_data (Lobby *lobby, void *game_data, Action game_data_delete);

/**
 * @brief Sets the update of a lobby, called every frame (the fps of the game type) with a CerverLobby while the lobby
 * runs.
 *
 * @param[in,out] lobby the lobby
 * @param[in] update the update
 */
CERVER_EXPORT void lobby_set_update (Lobby *lobby, Action update);

/**
 * @brief Returns whether the lobby runs.
 *
 * @param[in] lobby the lobby
 * @return true while it runs, false for a NULL lobby
 * @threadsafety Safe from any thread, with __atomic.
 */
CERVER_EXPORT bool lobby_is_running (const Lobby *lobby);

/**
 * @brief Searches a registered lobby that matches a query by its id, with the lock of the game cerver.
 *
 * @param[in] game_cerver the game cerver
 * @param[in] query a lobby with the id
 * @return the lobby, NULL for a NULL argument or without a match
 * @warning It does not take a reference: the lobby can be released after the lock is released.
 * @deprecated Use lobby_get_reference (), which takes the reference with the lock.
 */
DEPRECATED (CERVER_EXPORT Lobby *lobby_get (struct _GameCerver *game_cerver, Lobby *query));

/**
 * @brief Searches a registered lobby that matches a query by its id and takes a reference to it, with the lock of
 * the game cerver, so it can not be released before its caller is done.
 *
 * @param[in] game_cerver the game cerver
 * @param[in] query a lobby with the id
 * @return the lobby with a reference that the caller releases with lobby_release (), NULL for a NULL argument or
 * without a match
 * @since 1.9b-2
 */
CERVER_EXPORT Lobby *lobby_get_reference (struct _GameCerver *game_cerver, Lobby *query);

/**
 * @brief Searches a registered lobby with an id, with the lock of the game cerver.
 *
 * @param[in] cerver a game cerver
 * @param[in] id the id
 * @return the lobby, NULL for a NULL argument, a cerver that is not a game cerver or without a match
 * @warning It does not take a reference: the lobby can be released after the lock is released.
 * @deprecated Use lobby_search_with_id_reference (), which takes the reference with the lock.
 */
DEPRECATED (CERVER_EXPORT Lobby *lobby_search_with_id (struct _Cerver *cerver, const char *id));

/**
 * @brief Searches a registered lobby with an id and takes a reference to it, with the lock of the game cerver, so
 * it can not be released before its caller is done.
 *
 * @param[in] cerver a game cerver
 * @param[in] id the id
 * @return the lobby with a reference that the caller releases with lobby_release (), NULL for a NULL argument, a
 * cerver that is not a game cerver or without a match
 * @since 1.9b-2
 */
CERVER_EXPORT Lobby *lobby_search_with_id_reference (struct _Cerver *cerver, const char *id);

/**
 * @brief Tears down a lobby: stops its threads, returns the connections of its players to the cerver, ends its game,
 * triggers CERVER_EVENT_LOBBY_DESTROY, releases its game data and the reference of the game cerver.
 *
 * The lobby is released when its last reference is; a lobby that already ended is not torn down again.
 *
 * @param[in] game_cerver the game cerver, NULL for the one of the cerver of the lobby
 * @param[in,out] lobby the lobby
 * @retval 0 the lobby was torn down, or it had already ended
 * @retval 1 a NULL lobby
 */
CERVER_EXPORT int lobby_teardown (struct _GameCerver *game_cerver, Lobby *lobby);

/**
 * @brief Initializes the structures of the lobby poll.
 *
 * @param[in,out] lobby the lobby
 * @param[in] max_players_fds the size of the poll
 * @retval 0 the poll was initialized
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 lobby_poll_init (Lobby *lobby, unsigned int max_players_fds);

/**
 * @brief Registers a connection of a player in the lobby poll and maps its socket to the player.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player
 * @param[in] connection the connection
 * @retval 0 the connection was registered
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 lobby_poll_register_connection (
	Lobby *lobby, struct _Player *player, struct _Connection *connection
);

/**
 * @brief Unregisters a connection of a player from the lobby poll and removes the map of its socket.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player
 * @param[in] connection the connection
 * @retval 0 the connection was unregistered
 * @retval 1 on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 lobby_poll_unregister_connection (
	Lobby *lobby, struct _Player *player, struct _Connection *connection
);

/**
 * @brief The default handler of a lobby: receives the packets of the players while the lobby runs.
 *
 * @param[in] ptr a CerverLobby
 */
CERVER_PUBLIC void lobby_poll (void *ptr);

/**
 * @brief Creates a lobby with a new player of the client as its owner.
 *
 * The lobby is not registered in the game cerver: game_cerver_register_lobby () does it.
 *
 * @param[in] cerver a game cerver
 * @param[in] client the client of the owner
 * @param[in] use_default_handler true for the lobby poll
 * @param[in] custom_handler the handler when it does not use the default one
 * @param[in] max_players the max players, 0 takes #LOBBY_DEFAULT_MAX_PLAYERS
 * @return the new lobby, NULL for a NULL argument or on error
 */
CERVER_EXPORT Lobby *lobby_create (
	struct _Cerver *cerver, struct _Client *client,
	bool use_default_handler, Action custom_handler, u32 max_players
);

/**
 * @brief Creates a player of a client and registers it in a lobby, with the lock of the game cerver.
 *
 * @param[in] cerver a game cerver
 * @param[in] client the client
 * @param[in,out] lobby the lobby
 * @retval 0 the client joined the lobby
 * @retval 1 a NULL argument, a full lobby, a lobby in game, a lobby that ended, a client that is already in it, or
 * the player could not be created
 */
CERVER_EXPORT u8 lobby_join (struct _Cerver *cerver, struct _Client *client, Lobby *lobby);

/**
 * @brief A player leaves a lobby and is released; the owner is given to the next player, and the last player tears
 * down the lobby.
 *
 * @param[in] cerver the cerver
 * @param[in,out] lobby the lobby
 * @param[in] player the player
 * @retval 0 the player left
 * @retval 1 a NULL argument or the player is not in the lobby
 */
CERVER_EXPORT u8 lobby_leave (struct _Cerver *cerver, Lobby *lobby, struct _Player *player);

/**
 * @brief Starts the handler of a lobby (its poll or its thread) and its update thread.
 *
 * @param[in] cerver the cerver
 * @param[in,out] lobby the lobby
 * @retval 0 the lobby was started
 * @retval 1 a NULL argument, a lobby that ended or that already runs
 */
CERVER_EXPORT u8 lobby_start (struct _Cerver *cerver, Lobby *lobby);

/**
 * @brief Returns the player of a client in a lobby.
 *
 * @param[in] lobby the lobby
 * @param[in] client the client
 * @return the player, NULL for a NULL argument or when the client is not in the lobby
 * @note Call it with the lock of the game cerver.
 */
CERVER_EXPORT struct _Player *lobby_player_of_client (
	const Lobby *lobby, const struct _Client *client
);

/**
 * @brief Returns whether the client is a player of a lobby.
 *
 * @param[in] client the client
 * @return true when it is a player
 * @note For the internal methods of cerver, with the lock of the game cerver.
 */
CERVER_PRIVATE bool lobby_client_is_player (const struct _Client *client);

/**
 * @brief Removes and releases a player of the lobby that left, ended its connection or stopped sending.
 *
 * @param[in,out] lobby the lobby
 * @param[in] player the player
 * @return true if the lobby is empty after it
 * @note For the internal methods of cerver, it takes the lock of the game cerver.
 */
CERVER_PRIVATE bool lobby_player_remove (Lobby *lobby, struct _Player *player);

/**
 * @brief The players that did not send a packet in the player timeout of the game type leave the lobby.
 *
 * @param[in,out] lobby the lobby
 * @param[in] now the milliseconds of player_activity_now ()
 * @return true if the lobby is empty after it
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE bool lobby_players_check_timeouts (Lobby *lobby, const u64 now);

/**
 * @brief A cerver and a lobby, the argument of the handler and the update of a lobby.
 */
typedef struct CerverLobby {

	/** @brief The cerver. */
	struct _Cerver *cerver;
	/** @brief The lobby. */
	Lobby *lobby;

} CerverLobby;

/**
 * @brief The X-macro table of the fields of the request of a client that creates a lobby of a game type.
 */
#define LOBBY_CREATE_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, game_type, LOBBY_GAME_TYPE_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (LobbyCreate, lobby_create_request, LOBBY_CREATE_FIELDS)

/**
 * @brief The X-macro table of the fields of the request of a client that joins a lobby.
 *
 * Without an id the cerver searches one of the game type, and creates it when none has a place.
 */
#define LOBBY_JOIN_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	STRING(T, 1, TEXT, lobby_id, LOBBY_ID_SIZE, SERIALIZER_OPTIONAL)				\
	STRING(T, 2, TEXT, game_type, LOBBY_GAME_TYPE_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (LobbyJoin, lobby_join_request, LOBBY_JOIN_FIELDS)

/**
 * @brief The X-macro table of the fields of the requests of a client about a lobby: init, start, leave, update and
 * destroy.
 */
#define LOBBY_REQUEST_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, lobby_id, LOBBY_ID_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (LobbyRequest, lobby_request, LOBBY_REQUEST_FIELDS)

/**
 * @brief The X-macro table of the fields of a player of a lobby: the client id and the player id if it has one.
 */
#define LOBBY_PLAYER_SUMMARY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)	\
	SCALAR(T, 1, U64, client_id, SERIALIZER_REQUIRED, 0)							\
	STRING(T, 2, TEXT, player_id, LOBBY_PLAYER_ID_SIZE, SERIALIZER_OPTIONAL)		\
	SCALAR(T, 3, BOOL, owner, SERIALIZER_OPTIONAL, false)

SERIALIZER_DECLARE (LobbyPlayerSummary, lobby_player_summary, LOBBY_PLAYER_SUMMARY_FIELDS)

/**
 * @brief The X-macro table of the fields of the settings of the game type of a lobby.
 */
#define LOBBY_SETTINGS_SUMMARY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)	\
	SCALAR(T, 1, U32, max_players, SERIALIZER_OPTIONAL, 0)								\
	SCALAR(T, 2, U32, player_timeout, SERIALIZER_OPTIONAL, 0)							\
	SCALAR(T, 3, U32, fps, SERIALIZER_OPTIONAL, 0)

SERIALIZER_DECLARE (LobbySettingsSummary, lobby_settings_summary, LOBBY_SETTINGS_SUMMARY_FIELDS)

/**
 * @brief The X-macro table of the fields of the state of a lobby that the cerver sends to its players.
 */
#define LOBBY_SUMMARY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)									\
	STRING(T, 1, TEXT, id, LOBBY_ID_SIZE, SERIALIZER_REQUIRED)													\
	SCALAR(T, 2, I64, creation, SERIALIZER_OPTIONAL, 0)															\
	STRING(T, 3, TEXT, game_type, LOBBY_GAME_TYPE_SIZE, SERIALIZER_OPTIONAL)									\
	SCALAR(T, 4, BOOL, created, SERIALIZER_OPTIONAL, false)														\
	SCALAR(T, 5, BOOL, running, SERIALIZER_OPTIONAL, false)														\
	SCALAR(T, 6, BOOL, in_game, SERIALIZER_OPTIONAL, false)														\
	SCALAR(T, 7, U32, max_players, SERIALIZER_OPTIONAL, 0)														\
	SCALAR(T, 8, U32, n_players, SERIALIZER_OPTIONAL, 0)														\
	SCALAR(T, 9, U64, owner_client_id, SERIALIZER_OPTIONAL, 0)													\
	OBJECTS(T, 10, LobbyPlayerSummary, lobby_player_summary_schema, players, LOBBY_SUMMARY_PLAYERS_MAX, SERIALIZER_OPTIONAL)	\
	OBJECT(T, 11, LobbySettingsSummary, lobby_settings_summary_schema, settings, SERIALIZER_OPTIONAL)

SERIALIZER_DECLARE (LobbySummary, lobby_summary, LOBBY_SUMMARY_FIELDS)

/**
 * @brief Fills a summary with the current state of a lobby.
 *
 * @param[out] summary the summary
 * @param[in] lobby the lobby
 * @param[in] created true when the lobby was just created
 * @note Call it with the lock of the game cerver.
 */
CERVER_EXPORT void lobby_summary_set (LobbySummary *summary, const Lobby *lobby, const bool created);

/**
 * @brief Creates a GAME packet of a request type with the summary of a lobby, taken with the lock of the game cerver.
 *
 * @param[in] lobby the lobby
 * @param[in] request_type the request type of the packet
 * @param[in] created true when the lobby was just created
 * @return the new packet, NULL for a NULL lobby or on error
 */
CERVER_PUBLIC struct _Packet *lobby_summary_packet_create (
	const Lobby *lobby, const u32 request_type, const bool created
);

/**
 * @brief Sends the summary of a lobby to a client, as the response of LOBBY_CREATE when it was just created, or of
 * LOBBY_JOIN.
 *
 * @param[in] lobby the lobby, it can be NULL
 * @param[in] created true when the lobby was just created
 * @param[in] cerver the cerver
 * @param[in] client the client
 * @param[in] connection the connection
 */
CERVER_EXPORT void lobby_send (
	Lobby *lobby, bool created,
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection
);

/**
 * @brief Sends the summary of a lobby to every player of the lobby.
 *
 * @param[in] lobby the lobby, it can be NULL
 * @param[in] request_type the request type of the packet
 */
CERVER_EXPORT void lobby_send_to_players (
	Lobby *lobby, const u32 request_type
);

/**
 * @brief Takes a reference to the lobby of the connection of a received packet, that the packet releases when it is
 * deleted; without a lobby it does nothing.
 *
 * @param[in,out] packet the packet
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void lobby_packet_attach (struct _Packet *packet);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
