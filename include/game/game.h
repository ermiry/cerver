#ifndef GAME_SERVER_H
#define GAME_SERVER_H

#include <poll.h>

#include "types/myTypes.h"

#include "cerver/cerver.h"
#include "player.h"
#include "lobby.h"

#include "collections/dllist.h"
#include "collections/avl.h"
#include "utils/config.h"
#include "utils/objectPool.h"

struct _Server;
struct _GameServerData;
struct _Client;
struct _PacketInfo;

#define DEFAULT_PLAYER_TIMEOUT      30
#define DEFAULT_FPS                 20
#define DEFAULT_MIN_PLAYERS         2
#define DEFAULT_MAX_PLAYERS         4    

#define FPS		20

#define GS_LOBBY_POOL_INIT      1   // n lobbys to init the lobby pool with
#define GS_PLAYER_POOL_INT      2   // n players to init the player pool with

#ifdef RUN_FROM_MAKE
    #define GS_GAME_SETTINGS_CFG         "./config/gameSettings.cfg"

#elif RUN_FROM_BIN
    #define GS_GAME_SETTINGS_CFG         "../config/gameSettings.cfg"

#else
    #define GS_GAME_SETTINGS_CFG          ""
#endif  

struct _GameServerData {

    DoubleList *currentLobbys;      // a list of the current lobbys
    void (*lobby_id_generator)(char *);
    // Pool *lobbyPool;             // 21/10/2018 -- 22:04 -- each game server has its own pool

    // Pool *playersPool;          // 22/10/2018 -- each server has its own player's pool
    AVLTree *players;
    Comparator player_comparator;      // used in the avl tree

    // we can define a function to load game data at start, 
    // for example to connect to a db or something like that
    Action load_game_data;
    Action delete_game_data;
    void *game_data;

    // an admin can set a custom function to find a lobby bassed on 
	// some game parameters
    // TODO: do we want this here?  21/04/2019
	Action findLobby;

    // action to be performed right before the game server teardown
    // eg. send a message to all players
    Action final_game_action;

};

typedef struct _GameServerData GameServerData;

/*** Game Server Configuration ***/

// option to set a custom lobby id generator
extern void game_set_lobby_id_generator (GameServerData *game_data, void (*lobby_id_generator)(char *));
// option to set the main game server player comprator
extern void game_set_player_comparator (GameServerData *game_data, Comparator player_comparator);
// option to set the a func to be executed only once at the start of the game server
extern void game_set_load_game_data (GameServerData *game_data, Action load_game_data);
// option to set the func to be executed only once when the game server gets destroyed
extern void game_set_delete_game_data (GameServerData *game_data, Action delete_game_data);

// option to set an action to be performed right before the game server teardown
// the server reference will be passed to the action
// eg. send a message to all players
extern void game_set_final_action (GameServerData *game_data, Action final_action);

// constructor for a new game server data
// initializes game server data structures and sets actions to defaults
extern GameServerData *game_server_data_new (void);
extern void game_server_data_delete (GameServerData *game_server_data);

// cleans up all the game structs like lobbys and in game data set by the admin
u8 game_server_teardown (struct _Server *server);


/*** THE FOLLOWING AND KIND OF BLACKROCK SPECIFIC ***/
/*** WE NEED TO DECIDE WITH NEED TO BE ON THE FRAMEWORK AND WHICH DOES NOT!! ***/


enum _GameType {

	ARCADE = 1

};

typedef enum _GameType GameType;

// 17/11/2018 -- aux structure for traversing a players tree
typedef struct PlayerAndData {

    void *playerData;
    void *data;

} PlayerAndData;

/*** GAME PACKETS ***/

// 04/11/2018 -- 21:29 - to handle requests from players inside the lobby
// primarilly game updates and messages
typedef struct GamePacketInfo {

    struct _Server *server;
    Lobby *lobby;
    Player *player;
    char packetData[65515];		// max udp packet size
    size_t packetSize;

} GamePacketInfo;

/*** GAME SERIALIZATION ***/

#pragma region GAME SERIALIZATION

typedef int32_t SRelativePtr;
struct _SArray;

// TODO: 19/11/2018 -- 19:05 - we need to add support for scores!

// info that we need to send to the client about the players
typedef struct Splayer {

	// TODO:
	// char name[64];

	// TODO: 
	// we need a way to add info about the players info for specific game
	// such as their race or level in blackrock

	bool owner;

} SPlayer;

// info that we need to send to the client about the lobby he is in
typedef struct SLobby {

	GameSettings settings;
    bool inGame;

	// array of players inside the lobby
	// struct _SArray players;

} SLobby;

// FIXME: do we need this?
typedef struct UpdatedGamePacket {

	GameSettings gameSettings;

	PlayerId playerId;

	// SequenceNum sequenceNum;
	// SequenceNum ack_input_sequence_num;

	// SArray players; // Array of SPlayer.
	// SArray explosions; // Array of SExplosion.
	// SArray projectiles; // Array of SProjectile.

} UpdatedGamePacket; 

// FIXME: do we need this?
typedef struct PlayerInput {

	// Position pos;

} PlayerInput;

typedef uint64_t SequenceNum;

// FIXME:
typedef struct PlayerInputPacket {

	// SequenceNum sequenceNum;
	// PlayerInput input;
	
} PlayerInputPacket;

// FIXME: this should be a serialized version of the game component
// in the game we move square by square
/* typedef struct Position {

    u8 x, y;
    // u8 layer;   

} Position; */

#pragma endregion

#endif