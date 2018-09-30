#ifndef GAMEUI_H
#define GAMEUI_H

#include "ui/ui.h"

#include "utils/list.h"

/*** GAME UI ***/

extern UIScreen *gameScene (void);


/*** CLEAN UP GAME UI ***/

extern void resetGameUI (void);
extern void destroyGameUI (void);

#endif