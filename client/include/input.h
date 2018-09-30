#ifndef INPUT_H
#define INPUT_H

#include <SDL2/SDL.h>

#include "ui/ui.h"

extern void hanldeMenuEvent (UIScreen *activeScreen, SDL_Event event);
extern void hanldeGameEvent (UIScreen *activeScreen, SDL_Event event);

#endif