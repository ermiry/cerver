#ifndef _CERVER_LOBBY_H_
#define _CERVER_LOBBY_H_

#include <time.h>

#include "types/types.h"

#include "game.h"
#include "player.h"

#include "collections/avl.h"

#include "utils/objectPool.h"

struct _Server;
struct _Player;
struct _GameServerData;
// enum _GameType;

struct _GameSettings {

    // FIXME: set init function for diffrent game types
	// enum _GameType gameType;

	u8 playerTimeout; 	// in seconds.
	u8 fps;

	u8 minPlayers;
	u8 maxPlayers;

	// duration?

};

typedef struct _GameSettings GameSettings;

struct _Lobby {

	const char *id;						// lobby unique id - generated using the creation timestamp
	time_t creation_time_stamp;

	bool isRunning;						// lobby is listening for player packets
	bool inGame;						// lobby is inside a game

	void *game_settings;
	Action delete_lobby_game_settings;

	struct _Player *owner;				// the client that created the lobby -> he has higher privileges
	AVLTree *players;					// players inside the lobby -> reference to the main player avl
	unsigned int max_players;
	unsigned int current_players;

    struct pollfd *players_fds;     			
    u16 players_nfds;                           // n of active fds in the pollfd array
    bool compress_players;              		// compress the fds array?
    u32 poll_timeout;    

	// the server admin can add its server specific data types
	void *game_data;
	Action delete_lobby_game_data;

	Action handler;						// lobby player handler

	// 21/11/2018 - we put this here to avoid race conditions if we put it on
	// the server game data
	// Pool *gamePacketsPool;

};

typedef struct _Lobby Lobby;

typedef struct ServerLobby {

    struct _Server *server;
    Lobby *lobby;

} ServerLobby;

// sets the lobby settings and a function to delete it
extern void lobby_set_game_settings (Lobby *lobby, void *game_settings, Action delete_game_settings);
// sets the lobby game data and a function to delete it
extern void lobby_set_game_data (Lobby *lobby, void *game_data, Action delete_lobby_game_data);
// set the lobby player handler
extern void lobby_set_handler (Lobby *lobby, Action handler);

// the default lobby id generator
extern void lobby_default_generate_id (char *lobby_id);

// lobby constructor, it also initializes basic lobby data
extern Lobby *lobby_new (struct _GameServerData *game_data, unsigned int max_players);
// deletes a lobby for ever -- called when we teardown the server
// we do not need to give any feedback to the players if there is any inside
extern void lobby_delete (void *ptr);
extern int lobby_comparator (void *one, void *two);

// searchs a lobby in the game data and returns a reference to it
extern Lobby *lobby_get (struct _GameServerData *game_data, Lobby *query);

// create a list to manage the server lobbys
// called when we init the game server
extern u8 game_init_lobbys (struct _GameServerData *game_data, u8 n_lobbys);

/*** Player interaction ***/

// starts the lobby in a separte thread using its hanlder
extern u8 lobby_start (struct _Server *server, Lobby *lobby);

// creates a new lobby and inits his values with an owner
extern Lobby *lobby_create (struct _Server *server, struct _Player *owner, unsigned int max_players);
// called by a registered player that wants to join a lobby on progress
// the lobby model gets updated with new values
extern u8 lobby_join (struct _GameServerData *game_data, Lobby *lobby, struct _Player *player);
// called when a player requests to leave the lobby
extern u8 lobby_leave (struct _GameServerData *game_data, Lobby *lobby, struct _Player *player);

#endif