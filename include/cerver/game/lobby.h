#ifndef _CERVER_GAME_LOBBY_H_
#define _CERVER_GAME_LOBBY_H_

#include <stdbool.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/cerver.h"
#include "cerver/client.h"

#include "cerver/threads/thread.h"

#include "cerver/game/game.h"
#include "cerver/game/gametype.h"
#include "cerver/game/player.h"

#define LOBBY_DEFAULT_POLL_TIMEOUT			2000
#define LOBBY_DEFAULT_MAX_PLAYERS			4

struct _Cerver;
struct _Client;
struct _Connection;
struct _GameCerver;
struct _Player;
struct _Lobby;
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
extern void lobby_stats_set_threshold_time (struct _Lobby *lobby, time_t threshold_time);

extern void lobby_stats_print (struct _Lobby *lobby);

struct _Lobby {

	struct _Cerver *cerver;				// the cerver the lobby belongs to

	String *id;							// lobby unique id - generated using the creation timestamp
	time_t creation_time_stamp;

	Htab *sock_fd_player_map;           // maps a socket fd to a player
	struct pollfd *players_fds;     			
	u16 max_players_fds;
	u16 current_players_fds;            // n of active fds in the pollfd array
	u32 poll_timeout;    

	bool running;						// lobby is listening for player packets
	bool in_game;						// lobby is inside a game

	DoubleList *players;				// players insside the lobby
	struct _Player *owner;				// the client that created the lobby -> he has higher privileges
	unsigned int max_players;
	unsigned int n_current_players;

	pthread_t handler_thread_id;
	bool default_handler;
	Action handler;						// lobby handler (lobby poll)
	Action packet_handler;				// lobby packet handler

	GameType *game_type;

	void *game_settings;
	Action game_settings_delete;

	// the server admin can add its server specific data types
	void *game_data;
	Action game_data_delete;

	pthread_t update_thread_id;
	Action update;						// lobby update function to be executed every fps

	LobbyStats *stats;

};

typedef struct _Lobby Lobby;

// lobby constructor
extern Lobby *lobby_new (void);

extern void lobby_delete (void *lobby_ptr);

//compares two lobbys based on their ids
extern int lobby_comparator (const void *one, const void *two);

// set lobby poll function timeout in mili secs
// how often we are checking for new packages
extern void lobby_set_poll_time_out (Lobby *lobby, unsigned int timeout);

// set the lobby hanlder
extern void lobby_set_handler (Lobby *lobby, Action handler);

// set the lobby packet handler
extern void lobby_set_packet_handler (Lobby *lobby, Action packet_handler);

// sets the lobby settings and a function to delete it
extern void lobby_set_game_settings (Lobby *lobby, 
	void *game_settings, Action game_settings_delete);

// sets the lobby game data and a function to delete it
extern void lobby_set_game_data (Lobby *lobby, void *game_data, Action game_data_delete);

// sets the lobby update action, the lobby will we passed as the args
extern void lobby_set_update (Lobby *lobby, Action update);

// searches a lobby in the game cerver and returns a reference to it
extern Lobby *lobby_get (struct _GameCerver *game_cerver, Lobby *query);

// seraches for a lobby with matching id
extern Lobby *lobby_search_by_id (struct _Cerver *cerver, const char *id);

// stops the lobby, disconnects clinets (players) if any, destroys lobby game data, and deletes the lobby at the end
extern int lobby_teardown (struct _GameCerver *game_cerver, Lobby *lobby);

/*** lobby poll ***/

// inits the lobby poll structures
// returns 0 on success, 1 on error
extern u8 lobby_poll_init (Lobby *lobby, unsigned int max_players_fds);

// registers a player's client connection to the lobby poll
// and maps the sock fd to the player
extern u8 lobby_poll_register_connection (Lobby *lobby, 
	struct _Player *player, struct _Connection *connection);

// unregisters a player's client connection from the lobby poll structure
// and removes the map from the sock fd to the player
// returns 0 on success, 1 on error
extern u8 lobby_poll_unregister_connection (Lobby *lobby, 
	struct _Player *player, struct _Connection *connection);

// lobby default handler
extern void lobby_poll (void *ptr);

/*** Public lobby functions ***/

// creates and inits a new lobby
// creates a new user associated with the client and makes him the owner
extern Lobby *lobby_create (struct _Cerver *cerver, struct _Client *client,
	bool use_default_handler, Action custom_handler, u32 max_players);

// a client wants to join a game, so we create a new player and register him to the lobby
// returns 0 on success, 1 on error
extern u8 lobby_join (struct _Cerver *cerver, struct _Client *client, Lobby *lobby);

// called when a player requests to leave the lobby
// returns 0 on success, 1 on error
extern u8 lobby_leave (struct _Cerver *cerver, Lobby *lobby, struct _Player *player);

// starts the lobby's handler and/or update method in the cervers thpool
extern u8 lobby_start (struct _Cerver *cerver, Lobby *lobby);

typedef struct CerverLobby {

	struct _Cerver *cerver;
	Lobby *lobby;

} CerverLobby;

/*** serialization ***/

typedef struct SGameSettings {

	// config
	SStringS game_type;
	u8 player_timeout;
	u8 fps;

	// rules
	u8 min_players;
	u8 max_players;
	int duration;

} SGameSettings;

typedef struct SLobby {

	// lobby info
	SStringS id;
	time_t creation_timestamp;

	// lobby status
	bool running;
	bool in_game;

	// game / players
	SGameSettings settings;
	unsigned int n_players;
	// TODO: owner
	
} SLobby;

extern SLobby *lobby_serialize (Lobby *lobby);

// sends a lobby and all of its data to the client
// created: if the lobby was just created
extern void lobby_send (Lobby *lobby, bool created,
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection);

typedef struct LobbyJoin {

	SStringS lobby_id;
	SStringS game_type;

} LobbyJoin;

#endif