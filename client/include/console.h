#ifndef CONSOLE_H_
#define CONSOLE_H_

#include "game.h"

#define NUM_COLS    80
#define NUM_ROWS    45

typedef unsigned char asciiChar;

typedef struct {

    i32 x; i32 y; i32 w; i32 h;

} Rect;

// this are used to emulate a console inside the SDL window
typedef struct {

    asciiChar glyph;
    u32 fgColor;
    u32 bgColor;

} Cell;

typedef struct {

    u32 *atlas;
    u32 atlasWidth;
    u32 atlasHeight;
    u32 charWidth;
    u32 charHeight;
    asciiChar firstCharInAtlas;

} Font;

typedef struct {

    u32 *pixels;
    u32 width;
    u32 height;
    u32 rows;
    u32 cols;
    u32 cellWidth;
    u32 cellHeight;

    Cell *cells;
    Font *font;

} Console;


extern void clearConsole (Console *console);
extern Console *initConsole (i32, i32, i32, i32);
extern void setConsoleBitmapFont (Console *, char *, asciiChar, int, int);
extern void putCharAt (Console *, asciiChar, i32 cellX, i32 cellY, u32 fgColor, u32 bgColor);

#endif 