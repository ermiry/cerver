#ifndef GAME_H
#define GAME_H

#include "network.h"
#include "utils/myTime.h"

#define FPS		20

// TODO: what other settings do we need?? map? enemies? loot?
typedef struct GameSettings {

	u8 playerTimeout; 	// in seconds.
	u8 fps;

	u8 minPlayers;
	u8 maxPlayers;

	// duration?

} GameSettings;

// in the game we move square by square
typedef struct Position {

    u8 x, y;
    // u8 layer;   

} Position;

typedef struct PlayerInput {

	Position pos;

} PlayerInput;

/* typedef struct SPlayer {
	SPlayerId id;
	// SBool alive;
	// SVectorFloat position;
	float heading;
	uint32_t score;
	// SColor color;
} SPlayer; */

typedef uint64_t SequenceNum;

typedef struct PlayerInputPacket {

	SequenceNum sequenceNum;
	PlayerInput input;
	
} PlayerInputPacket;

typedef uint16_t PlayerId;

typedef struct UpdatedGamePacket {

	GameSettings gameSettings;

	PlayerId playerId;

	SequenceNum sequenceNum;
	SequenceNum ack_input_sequence_num;

	// SArray players; // Array of SPlayer.
	// SArray explosions; // Array of SExplosion.
	// SArray projectiles; // Array of SProjectile.

} UpdatedGamePacket; 

// TODO: maybe add the game components here? as in the client?
typedef struct Player {

	PlayerId id;
	struct sockaddr_storage address;

	PlayerInput input;
	u32 inputSequenceNum;
	TimeSpec lastInputTime;

	bool alive;

	// Components
	Position pos;

} Player;

// 02/10/2018 -- 9:38 -- lets try this...
typedef struct Lobby {

	GameSettings *settings;

	Player *owner;		// the client that created the lobby -> he has higher privileges
	Vector players;		// the client connected to the lobby

} Lobby;

/*** IN GAME DATA STRUCTURES ***/

#include "utils/vector.h"

extern Vector players;

/*** GAME FUNCTIONS ***/

extern Lobby *newLobby (void);

extern void spawnPlayer (Player *);

#endif