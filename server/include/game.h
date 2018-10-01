#ifndef GAME_H
#define GAME_H

#include "network.h"

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
	SBool alive;
	SVectorFloat position;
	float heading;
	uint32_t score;
	SColor color;
} SPlayer;

typedef struct SExplosion {
	SVectorFloat position;
	uint16_t n_ticks_since_creation;
} SExplosion;

typedef struct SProjectile {
	SVectorFloat position;
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
	SVectorInt level_size;
	uint16_t fps;
	uint16_t projectile_lifetime;
} SGameSettings;

typedef struct SSimulationTickPacket {
	SSequenceNum sequence_num;
	SSequenceNum ack_input_sequence_num;
	SGameSettings game_settings;
	SPlayerId your_player_id;
	SArray players; // Array of SPlayer.
	SArray explosions; // Array of SExplosion.
	SArray projectiles; // Array of SProjectile.
} SSimulationTickPacket;


// FIXME: MINE!!1
/* typedef struct Player {

	u16 id;
	struct sockaddr_storage address;

	PlayerInput input;
	u32 input_sequence_num;
	Cptime last_input_time;

	// bool alive;

	// int score;
	// Color color;

} Player; */

// TODO: maybe we can move this form here?
/*** PACKETS ***/

typedef u32 ProtocolId;

typedef struct Version {

	u16 major;
	u16 minor;
	
} Version;

// FIXME: choose wisely...
const ProtocolId PROTOCOL_ID = 0xEC3B5FA9; // Randomly chosen.
const Version PROTOCOL_VERSION = {7, 0};

// FIXME: NAMES!!
typedef Packet_Type {

	S_PT_SIMULATION_TICK,
	S_PT_PLAYER_INPUT,

}

typedef struct PacketHeader {

	ProtocolId protocolID;
	Version protocolVersion;
	PacketType packetType;

} PacketHeader;

#endif