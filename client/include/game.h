#ifndef GAME_H_
#define GAME_H_

#include <stdint.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

#define THREAD_OK   0

#define FPS_LIMIT   20

#define SCREEN_WIDTH    1280
#define SCREEN_HEIGHT   720

#define MAP_WIDTH   80
#define MAP_HEIGHT  40

#define NUM_COLS    80
#define NUM_ROWS    45

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

typedef unsigned char asciiChar;

extern bool inGame;
extern bool wasInGame;

/*** IN-GAME DATA ***/

#define UNSET_LAYER     0
#define GROUND_LAYER    1
#define LOWER_LAYER     2
#define MID_LAYER       3
#define TOP_LAYER       4

/*** COMPONENTS ***/

typedef struct Position {

    u32 objectId;
    u8 x, y;
    u8 layer;   

} Position;

typedef struct Graphics {

    u32 objectId;
    asciiChar glyph;
    u32 fgColor;
    u32 bgColor;
    bool hasBeenSeen;
    bool visibleOutsideFov;
    char *name;     // 19/08/2018 -- 16:47

} Graphics;

typedef struct Physics {

    u32 objectId;
    bool blocksMovement;
    bool blocksSight;

} Physics;

/*** GAME FUNCTIONS ***/

extern void startGame (void);
extern void initWorld (void);
extern void cleanUpGame (void);

/*** MULTIPLAYER ***/

extern void createGame (void);
extern void joinGame (void);

#endif