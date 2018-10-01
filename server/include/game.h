#ifndef GAME_H
#define GAME_H

#include "network.h"

#define FPS		20

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

typedef struct PlayerInput {

	SPlayerAcceleration accelerate;
	SPlayerRotation rotate;
	bool shoot;

} PlayerInput;

typedef uint64_t SSequenceNum;

typedef struct Player {

	u16 id;
	struct sockaddr_storage address;

	PlayerInput input;
	u32 input_sequence_num;
	Cptime last_input_time;

	// bool alive;

	// int score;
	// Color color;

} Player;

#endif