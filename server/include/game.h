#ifndef GAME_H
#define GAME_H

#include "utils/myTime.h"

#define FPS		20

// FIXME: CHANGE ALL THE FOLLOWING UNTIL MINE!!!

typedef uint16_t SPlayerId;

typedef int8_t SPlayerRotation;
enum SPlayerRotation {
	S_PR_NONE,
	S_PR_LEFT,
	S_PR_RIGHT,
};

typedef int8_t SPlayerAcceleration;
enum SPlayerAcceleration {
	S_PA_NONE,
	S_PA_FORWARD,
	S_PA_REVERSE,
};

typedef struct SPlayerInput {
	SPlayerAcceleration accelerate;
	SPlayerRotation rotate;
	bool shoot;
} SPlayerInput;

typedef struct SPlayer {
	SPlayerId id;
	// SBool alive;
	// SVectorFloat position;
	float heading;
	uint32_t score;
	// SColor color;
} SPlayer;

typedef struct SExplosion {
	// SVectorFloat position;
	uint16_t n_ticks_since_creation;
} SExplosion;

typedef struct SProjectile {
	// SVectorFloat position;
	float heading;
	uint16_t n_ticks_since_creation;
} SProjectile;

typedef uint64_t SSequenceNum;

typedef struct PlayerInputPacket {
	SSequenceNum sequence_num;
	SPlayerInput input;
} PlayerInputPacket;

typedef struct SGameSettings {
	float player_timeout; // Seconds.
	// SVectorInt level_size;
	uint16_t fps;
	uint16_t projectile_lifetime;
} SGameSettings;

typedef struct SSimulationTickPacket {
	SSequenceNum sequence_num;
	SSequenceNum ack_input_sequence_num;
	SGameSettings game_settings;
	SPlayerId your_player_id;
	// SArray players; // Array of SPlayer.
	// SArray explosions; // Array of SExplosion.
	// SArray projectiles; // Array of SProjectile.
} SSimulationTickPacket; 


// FIXME: MINE!!
typedef struct Player {

	u16 id;
	struct sockaddr_storage address;

	// PlayerInput input;
	u32 inputSequenceNum;
	TimeSpec lastInputTime;

	// bool alive;

	// int score;
	// Color color;

} Player;

/*** IN GAME DATA STRUCTURES ***/

#include "utils/vector.h"

extern Vector players;

/*** GAME FUNCTIONS ***/

extern void spawnPlayer (Player *);

#endif