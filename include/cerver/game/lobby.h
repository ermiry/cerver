#ifndef _CERVER_GAME_LOBBY_H_
#define _CERVER_GAME_LOBBY_H_

#include <stdbool.h>
#include <time.h>

#include <poll.h>
#include <pthread.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/config.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/serializer.h"

#include "cerver/threads/thread.h"

#include "cerver/game/game.h"
#include "cerver/game/gametype.h"
#include "cerver/game/player.h"

#define LOBBY_DEFAULT_POLL_TIMEOUT			2000
#define LOBBY_DEFAULT_MAX_PLAYERS			4

// the sizes of the texts of the lobby objects, with their NUL
#define LOBBY_ID_SIZE						80
#define LOBBY_GAME_TYPE_SIZE				64
#define LOBBY_PLAYER_ID_SIZE				64

// the players that a LobbySummary sends, a lobby can have more
#define LOBBY_SUMMARY_PLAYERS_MAX			32

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

struct _GameSettings {

	// config
	GameType *game_type;
	u8 player_timeout;		// seconds until we drop the player
	u8 fps;

	// rules
	u8 min_players;
	u8 max_players;
	int duration;			// in secconds, -1 for inifinite duration

};

typedef struct _GameSettings GameSettings;

typedef struct LobbyStats {

	time_t threshold_time;                          // every time we want to reset lobby stats (like packets), defaults 24hrs

	u64 n_packets_received;                   		// total number of cerver packets received (packet header + data)
	u64 n_receives_done;                      		// total amount of actual calls to recv ()
	u64 bytes_received;                       		// total amount of bytes received in the cerver

	u64 n_packets_sent;                             // total number of packets that were sent
	u64 bytes_sent;                           		// total amount of bytes sent by the cerver

	struct _PacketsPerType *received_packets;
	struct _PacketsPerType *sent_packets;

} LobbyStats;

// sets the lobby stats threshold time (how often the stats get reset)
CERVER_EXPORT void lobby_stats_set_threshold_time (struct _Lobby *lobby, time_t threshold_time);

CERVER_EXPORT void lobby_stats_print (struct _Lobby *lobby);

// the players, the owner, the counts and the state of a lobby
// are read and written with the lock of its game cerver (game_cerver_lock ())
struct _Lobby {

	struct _Cerver *cerver;				// the cerver the lobby belongs to

	String *id;							// lobby unique id - generated using the creation timestamp
	time_t creation_time_stamp;

	// the game cerver keeps one, and each thread of the lobby, packet and event another,
	// the last one that is released deletes the lobby
	unsigned int references;

	// the lobby poll, that takes the connections of the players
	// from the cerver poll while the game runs (CERVER_HANDLER_TYPE_POLL)
	struct pollfd *players_fds;
	u32 max_players_fds;
	u32 current_players_fds;            // n of active fds in the pollfd array
	u32 poll_timeout;
	bool poll_connections;				// the lobby poll has the connections of its players
	int wake_fds[2];					// a pipe that wakes up the lobby poll to stop it

	bool running;						// lobby is listening for player packets
	bool in_game;						// lobby is inside a game
	bool initialized;					// the game was initialized with GAME_INIT
	bool ended;							// the lobby was torn down

	DoubleList *players;				// players insside the lobby
	struct _Player *owner;				// the client that created the lobby -> he has higher privileges
	unsigned int max_players;
	unsigned int n_current_players;

	pthread_t handler_thread_id;
	bool handler_started;
	bool default_handler;
	Action handler;						// lobby handler (lobby poll), it gets a CerverLobby
	Action packet_handler;				// takes the app packets of the players while the lobby runs

	GameType *game_type;

	void *game_settings;
	Action game_settings_delete;

	// the server admin can add its server specific data types
	void *game_data;
	Action game_data_delete;

	pthread_t update_thread_id;
	bool update_started;
	Action update;						// lobby update function to be executed every fps, it gets a CerverLobby

	// the update thread waits on it between its frames, so the teardown stops it at once
	pthread_mutex_t stop_mutex;
	pthread_cond_t stop_cond;

	LobbyStats *stats;

};

typedef struct _Lobby Lobby;

// lobby constructor, with the reference of its creator
CERVER_EXPORT Lobby *lobby_new (void);

// releases the lobby right away, use lobby_release () for a lobby with references
CERVER_EXPORT void lobby_delete (void *lobby_ptr);

// takes a reference to the lobby, returns the lobby (NULL is safe)
CERVER_PUBLIC Lobby *lobby_reference (Lobby *lobby);

// releases a reference to the lobby, the last one deletes it (NULL is safe)
CERVER_PUBLIC void lobby_release (Lobby *lobby);

// compares two lobbys based on their ids
CERVER_EXPORT int lobby_comparator (const void *one, const void *two);

// set lobby poll function timeout in mili secs
// how often we are checking for new packages
CERVER_EXPORT void lobby_set_poll_time_out (Lobby *lobby, unsigned int timeout);

// set the lobby hanlder
CERVER_EXPORT void lobby_set_handler (Lobby *lobby, Action handler);

// sets the handler of the app packets of the players while the lobby runs,
// it gets the packet and the packet is released after it
CERVER_EXPORT void lobby_set_packet_handler (Lobby *lobby, Action packet_handler);

// sets the lobby settings and a function to delete it
CERVER_EXPORT void lobby_set_game_settings (
	Lobby *lobby, void *game_settings, Action game_settings_delete
);

// sets the lobby game data and a function to delete it
CERVER_EXPORT void lobby_set_game_data (Lobby *lobby, void *game_data, Action game_data_delete);

// sets the lobby update action, called every frame (the fps of the game type)
// with a CerverLobby while the lobby runs
CERVER_EXPORT void lobby_set_update (Lobby *lobby, Action update);

// returns true while the lobby runs
CERVER_EXPORT bool lobby_is_running (const Lobby *lobby);

// searches a lobby in the game cerver and returns a reference to it
CERVER_EXPORT Lobby *lobby_get (struct _GameCerver *game_cerver, Lobby *query);

// seraches for a lobby with matching id, with the lock of the game cerver
CERVER_EXPORT Lobby *lobby_search_by_id (struct _Cerver *cerver, const char *id);

// stops the lobby threads, returns the connections of its players to the cerver,
// destroys the lobby game data and releases the reference of the game cerver
// the lobby is released when its last reference is
// returns 0 on success, 1 on error
CERVER_EXPORT int lobby_teardown (struct _GameCerver *game_cerver, Lobby *lobby);

/*** lobby poll ***/

// inits the lobby poll structures
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 lobby_poll_init (Lobby *lobby, unsigned int max_players_fds);

// registers a player's client connection to the lobby poll
// and maps the sock fd to the player
CERVER_PRIVATE u8 lobby_poll_register_connection (
	Lobby *lobby, struct _Player *player, struct _Connection *connection
);

// unregisters a player's client connection from the lobby poll structure
// and removes the map from the sock fd to the player
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 lobby_poll_unregister_connection (
	Lobby *lobby, struct _Player *player, struct _Connection *connection
);

// lobby default handler, receives the packets of the players while the lobby runs
CERVER_PUBLIC void lobby_poll (void *ptr);

/*** Public lobby functions ***/

// creates and inits a new lobby
// creates a new user associated with the client and makes him the owner
// max_players 0 takes LOBBY_DEFAULT_MAX_PLAYERS
CERVER_EXPORT Lobby *lobby_create (
	struct _Cerver *cerver, struct _Client *client,
	bool use_default_handler, Action custom_handler, u32 max_players
);

// a client wants to join a game, so we create a new player and register him to the lobby
// a full lobby, a lobby in game, a lobby that ended, or a client that is already in it fail
// returns 0 on success, 1 on error
CERVER_EXPORT u8 lobby_join (struct _Cerver *cerver, struct _Client *client, Lobby *lobby);

// called when a player requests to leave the lobby, the player is released
// the owner is given to the next player, and the last player destroys the lobby
// returns 0 on success, 1 on error
CERVER_EXPORT u8 lobby_leave (struct _Cerver *cerver, Lobby *lobby, struct _Player *player);

// starts the lobby's handler and update threads
// returns 0 on success, 1 on error or if it was already started
CERVER_EXPORT u8 lobby_start (struct _Cerver *cerver, Lobby *lobby);

// the player of the client in the lobby, with the lock of the game cerver
CERVER_EXPORT struct _Player *lobby_player_of_client (
	const Lobby *lobby, const struct _Client *client
);

// returns true if the client is a player of a lobby, with the lock of the game cerver
CERVER_PRIVATE bool lobby_client_is_player (const struct _Client *client);

// a player of the lobby that left, ended its connection or stopped sending,
// without the lock of the game cerver; returns true if the lobby is empty after it
CERVER_PRIVATE bool lobby_player_remove (Lobby *lobby, struct _Player *player);

// the players that did not send a packet in the player timeout of the game type leave the lobby,
// now is in the milliseconds of player_activity_now ()
// returns true if the lobby is empty after it
CERVER_PRIVATE bool lobby_players_check_timeouts (Lobby *lobby, const u64 now);

typedef struct CerverLobby {

	struct _Cerver *cerver;
	Lobby *lobby;

} CerverLobby;

/*** serialization ***/

// the request of a client that creates a lobby of a game type
#define LOBBY_CREATE_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, game_type, LOBBY_GAME_TYPE_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (LobbyCreate, lobby_create_request, LOBBY_CREATE_FIELDS)

// the request of a client that joins a lobby, without an id the cerver searches one of the game type,
// and creates it when none has a place
#define LOBBY_JOIN_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)			\
	STRING(T, 1, TEXT, lobby_id, LOBBY_ID_SIZE, SERIALIZER_OPTIONAL)				\
	STRING(T, 2, TEXT, game_type, LOBBY_GAME_TYPE_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (LobbyJoin, lobby_join_request, LOBBY_JOIN_FIELDS)

// the requests of a client about a lobby: init, start, leave, update and destroy
#define LOBBY_REQUEST_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, lobby_id, LOBBY_ID_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (LobbyRequest, lobby_request, LOBBY_REQUEST_FIELDS)

// a player of a lobby, the client id and the player id if it has one
#define LOBBY_PLAYER_SUMMARY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)	\
	SCALAR(T, 1, U64, client_id, SERIALIZER_REQUIRED, 0)							\
	STRING(T, 2, TEXT, player_id, LOBBY_PLAYER_ID_SIZE, SERIALIZER_OPTIONAL)		\
	SCALAR(T, 3, BOOL, owner, SERIALIZER_OPTIONAL, false)

SERIALIZER_DECLARE (LobbyPlayerSummary, lobby_player_summary, LOBBY_PLAYER_SUMMARY_FIELDS)

// the settings of the game type of a lobby
#define LOBBY_SETTINGS_SUMMARY_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)	\
	SCALAR(T, 1, U32, max_players, SERIALIZER_OPTIONAL, 0)								\
	SCALAR(T, 2, U32, player_timeout, SERIALIZER_OPTIONAL, 0)							\
	SCALAR(T, 3, U32, fps, SERIALIZER_OPTIONAL, 0)

SERIALIZER_DECLARE (LobbySettingsSummary, lobby_settings_summary, LOBBY_SETTINGS_SUMMARY_FIELDS)

// the state of a lobby that the cerver sends to its players
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

// fills the summary with the current state of the lobby, with the lock of the game cerver
CERVER_EXPORT void lobby_summary_set (LobbySummary *summary, const Lobby *lobby, const bool created);

// creates a GAME packet of the request type with the summary of the lobby
CERVER_PUBLIC struct _Packet *lobby_summary_packet_create (
	const Lobby *lobby, const u32 request_type, const bool created
);

// sends the lobby and all of its data to the client,
// as the response of LOBBY_CREATE when it was just created, or of LOBBY_JOIN
CERVER_EXPORT void lobby_send (
	Lobby *lobby, bool created,
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection
);

// sends the summary of the lobby to every player of the lobby
CERVER_EXPORT void lobby_send_to_players (
	Lobby *lobby, const u32 request_type
);

// the lobby of a received packet: takes a reference to the lobby of the connection
// that the packet releases when it is deleted, without a lobby it does nothing
CERVER_PRIVATE void lobby_packet_attach (struct _Packet *packet);

#ifdef __cplusplus
}
#endif

#endif
