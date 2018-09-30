#ifndef UI_H_
#define UI_H_

#include <SDL2/SDL.h>

#include "resources.h"

#include "ui/console.h"

#include "utils/list.h"

typedef SDL_Rect UIRect;

struct UIScreen;

typedef void (*UIRenderFunction)(Console *);
typedef void (*UIEventHandler)(struct UIScreen *, SDL_Event);

typedef struct UIView {

    Console *console;
    UIRect *pixelRect;
    UIRenderFunction render;

} UIView;

typedef struct UIScreen {

    List *views;
    UIView *activeView;
    UIEventHandler handleEvent;

} UIScreen;

/*** SCREENS ***/

extern UIScreen *activeScene;
extern void setActiveScene (UIScreen *);
extern void destroyUIScreen (UIScreen *);

typedef void (*CleanUI)(void);
extern CleanUI destroyCurrentScreen;

/*** VIEWS **/

extern UIView *newView (UIRect pixelRect, u32 colCount, u32 rowCount, 
    char *fontFile, asciiChar firstCharInAtlas, u32 bgColor, bool colorize,
     UIRenderFunction renderFunc);

extern void destroyView (UIView *view);

extern void drawRect (Console *con, UIRect *rect, u32 color, i32 borderWidth, u32 borderColor);

extern void drawImageAt (Console *console, BitmapImage *image, i32 cellX, i32 cellY);


extern char *tileset;


#endif