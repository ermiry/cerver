#ifndef GAME_SERVER_H
#define GAME_SERVER_H

#include <poll.h>

#include "myTypes.h"

#include "cerver.h"

#include "utils/list.h"
#include "utils/avl.h"
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

    Config *gameSettingsConfig;     // stores game modes info

    Pool *lobbyPool;        // 21/10/2018 -- 22:04 -- each game server has its own pool
    List *currentLobbys;    // a list of the current lobbys

    Pool *playersPool;          // 22/10/2018 -- each server has its own player's pool
    AVLTree *players;

	// an admin can set a custom function to find a lobby bassed on 
	// some game parameters
	Action findLobby;

    // we can define a function to load game data at start, 
    // for example to connect to a db or something like that
    Func loadGameData;
    Func deleteGameData;

    Action deleteLobbyGameData;

    // 13//11/2018 -- depending on the game type, we can have different init game functions
    u8 n_gameInits;
    delegate *gameInitFuncs;

    // TODO: 13/11/2018 - do we also need separte functions to handle the game stop?

};

typedef struct _GameServerData GameServerData;

typedef enum GameType {

	ARCADE = 1

} GameType;

struct _GameSettings {

	GameType gameType;

	u8 playerTimeout; 	// in seconds.
	u8 fps;

	u8 minPlayers;
	u8 maxPlayers;

	// duration?

};

typedef struct _GameSettings GameSettings;

typedef uint16_t PlayerId;

typedef struct Player {

	struct _Client *client;		// client network data associated to this player

	PlayerId id;
	bool inLobby;

	// PlayerInput input;
	// u32 inputSequenceNum;
	// TimeSpec lastInputTime;

	bool alive;

	// 15/11/2018 -- we spec the player to be ecs based
	// the server admin can add its own components
	void *components;

} Player;

struct _Lobby {

	GameSettings *settings;
	bool inGame;

	Player *owner;				// the client that created the lobby -> he has higher privileges

	bool isRunning;				// lobby is listening for player packets

	// 04/11/2018 -- lets try this and see how it goes - intended to also work for a bigger 
	// lobby with more players in it
	AVLTree *players;							// players inside the lobby
    struct pollfd players_fds[4];     			// 04/11/2018 - 4 max players in lobby
    u16 players_nfds;                           // n of active fds in the pollfd array
    bool compress_players;              		// compress the fds array?
    u32 pollTimeout;    

	// the server admin can add its server specific data types
	void *gameData;
	Action deleteLobbyGameData;

	// 21/11/2018 - we put this here to avoid race conditions if we put it on
	// the server game data
	Pool *gamePacketsPool;

};

typedef struct _Lobby Lobby;

// 10/11/2018 - aux reference to a server and lobby for thread functions
typedef struct ServerLobby {

    struct _Server *server;
    Lobby *lobby;

} ServerLobby;

// 17/11/2018 -- aux structure for traversing a players tree
typedef struct PlayerAndData {

    void *playerData;
    void *data;

} PlayerAndData;

/*** GAME SERVER FUNCTIONS ***/

extern u8 destroyGameServer (struct _Server *server);

extern u8 game_initPlayers (GameServerData *gameData, u8 n_players);
extern u8 game_initLobbys (GameServerData *gameData, u8 n_lobbys);

extern void broadcastToAllPlayers (AVLNode *playerNode, struct _Server *server,
	void *packet, size_t packetSize);
extern void traversePlayers (AVLNode *node, Action action, void *data);

extern void gs_set_loadGameData (struct _Server *server, Func loadData);
extern void gs_set_deleteGameData (struct _Server *server, Func deleteData);
extern void gs_add_gameInit (struct _Server *server, GameType gameType, delegate gameInit);
extern void gs_set_lobbyDeleteGameData (struct _Server *server, Action deleteData);

extern void gs_handlePacket (struct _PacketInfo *packet);

/*** SCORES ***/

#pragma region SCORES 

#include "utils/htab.h"

#define DEFAULT_SCORE_SIZE      5   // default players inside the scoreboard

typedef struct ScoreBoard {

    Htab *scores;
    u8 registeredPlayers;
    u8 scoresNum;
    char **scoreTypes;

} ScoreBoard;

extern ScoreBoard *game_score_new (u8 playersNum, u8 scoresNum, ...);
extern void game_score_destroy (ScoreBoard *sb);

extern void game_score_add_scoreType (ScoreBoard *sb, char *newScore);
extern void game_score_add_lobby_players (ScoreBoard *sb, AVLNode *playerNode);
extern u8 game_score_remove_scoreType (ScoreBoard *sb, char *oldScore);

extern u8 game_score_add_player (ScoreBoard *sb, char *playerName);
extern u8 game_score_remove_player (ScoreBoard *sb, char *playerName);

extern void game_score_set (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);
extern i32 game_score_get (ScoreBoard *sb, char *playerName, char *scoreType);
extern void game_score_update (ScoreBoard *sb, char *playerName, char *scoreType, i32 value);
extern void game_score_reset (ScoreBoard *sb, char *playerName);

#pragma endregion

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