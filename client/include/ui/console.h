#ifndef CONSOLE_H_
#define CONSOLE_H_

#include "game.h"

// Helper macros for working with pixel colors
#define RED(c) ((c & 0xff000000) >> 24)
#define GREEN(c) ((c & 0x00ff0000) >> 16)
#define BLUE(c) ((c & 0x0000ff00) >> 8)
#define ALPHA(c) (c & 0xff)

#define COLOR_FROM_RGBA(r, g, b, a) ((r << 24) | (g << 16) | (b << 8) | a)

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

    u32 bgColor;
    bool colorize;

    Cell *cells;
    Font *font;

} Console;


extern void clearConsole (Console *console);
extern Console *initConsole (i32 width, i32 height, i32 rowCount, i32 colCount, u32 bgColor, bool colorize);
extern void setConsoleBitmapFont (Console *, char *, asciiChar, int, int);

extern void putCharAt (Console *, asciiChar, i32 cellX, i32 cellY, u32 fgColor, u32 bgColor);
extern void putStringAt (Console *con, char *string, i32 x, i32 y, u32 fgColor, u32 bgColor);
extern void putReverseString (Console *con, char *string, i32 x, i32 y, u32 fgColor, u32 bgColor);
void putStringAtCenter (Console *con, char *string, i32 y, u32 fgColor, u32 bgColor);
extern void putStringAtRect (Console *con, char *string, Rect rect, bool wrap, u32 fgColor, u32 bgColor);

extern void destroyConsole (Console *);

#endif 