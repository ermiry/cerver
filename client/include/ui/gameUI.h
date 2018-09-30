#ifndef GAMEUI_H
#define GAMEUI_H

#include "ui/ui.h"

#include "utils/list.h"

#define NO_COLOR        0x00000000
#define WHITE           0xFFFFFFFF
#define BLACK           0x000000FF

#define FULL_GREEN      0x00FF00FF
#define FULL_RED        0xFF0000FF

#define YELLOW          0xFFD32AFF
#define SAPPHIRE        0x1E3799FF

/*** FULL SCREEN ***/

#define FULL_SCREEN_LEFT		0
#define FULL_SCREEN_TOP		    0
#define FULL_SCREEN_WIDTH		80
#define FULL_SCREEN_HEIGHT	    45

/*** GAME UI ***/

extern UIScreen *gameScene (void);


/*** CLEAN UP GAME UI ***/

extern void resetGameUI (void);
extern void destroyGameUI (void);

#endif