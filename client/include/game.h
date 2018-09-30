#ifndef GAME_H_
#define GAME_H_

#include <stdint.h>
#include <stdbool.h>

#include <SDL2/SDL.h>

#define SCREEN_WIDTH    1280
#define SCREEN_HEIGHT   720

typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int32_t i32;
typedef int64_t i64;

#define internal    static
#define localPersist    static
#define global  static

// FIXME: later we will want to move this from here
typedef struct Player {

    u8 xPos;
    u8 yPos;

} Player;

#endif