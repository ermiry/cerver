#ifndef _CERVER_GAME_LOBBY_H_
#define _CERVER_GAME_LOBBY_H_

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/game/game.h"
#include "cerver/game/gameType.h"
#include "cerver/game/player.h"

#include "cerver/collections/dllist.h"
#include "cerver/collections/htab.h"

#include "cerver/utils/objectPool.h"

#define LOBBY_DEFAULT_POLL_TIMEOUT			2000
#define DEFAULT_MAX_LOBBY_PLAYERS			4

struct _Cerver;
struct _Connection;
struct _GameCerver;
struct _Player;

struct _GameSettings {

	// config
	GameType *game_type;
	u8 player_timeout;		// secons until we drop the player 
	u8 fps;

	// rules
	u8 min_players;
	u8 max_players;
	int duration;			// in secconds, -1 for inifinite duration

};

typedef struct _GameSettings GameSettings;

struct _Lobby {

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

	Action handler;						// lobby handler (lobby poll)
	Action packet_handler;				// lobby packet handler

	void *game_settings;
	Action game_settings_delete;

	// the server admin can add its server specific data types
	void *game_data;
	Action game_data_delete;

	Action update;						// lobby update function to be executed every fps

};

typedef struct _Lobby Lobby;

// lobby constructor
extern Lobby *lobby_new (void);

extern void lobby_delete (void *lobby_ptr);

//compares two lobbys based on their ids
extern int lobby_comparator (const void *one, const void *two);

// inits the lobby poll structures
// returns 0 on success, 1 on error
extern u8 lobby_poll_init (Lobby *lobby, unsigned int max_players_fds);

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

// registers a player's client connection to the lobby poll
// and maps the sock fd to the player
extern u8 lobby_poll_register_connection (Lobby *lobby, 
	struct _Player *player, struct _Connection *connection);

// unregisters a player's client connection from the lobby poll structure
// and removes the map from the sock fd to the player
// returns 0 on success, 1 on error
extern u8 lobby_poll_unregister_connection (Lobby *lobby, 
	struct _Player *player, struct _Connection *connection);

// searches a lobby in the game cerver and returns a reference to it
extern Lobby *lobby_get (struct _GameCerver *game_cerver, Lobby *query);

/*** Public lobby functions ***/

// starts the lobby's handler and/or update method in the cervers thpool
extern u8 lobby_start (struct _Cerver *cerver, Lobby *lobby);

// creates and inits a new lobby
// creates a new user associated with the client and makes him the owner
extern Lobby *lobby_create (struct _Cerver *cerver, struct _Client *client);

// a client wants to join a game, so we create a new player and register him to the lobby
// returns 0 on success, 1 on error
extern u8 lobby_join (struct _Cerver *cerver, struct _Client *client, Lobby *lobby);

// called when a player requests to leave the lobby
// returns 0 on success, 1 on error
extern u8 lobby_leave (struct _Cerver *cerver, Lobby *lobby, struct _Player *player);

typedef struct CerverLobby {

    struct _Cerver *cerver;
    Lobby *lobby;

} CerverLobby;

#endif