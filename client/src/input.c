#include <stdbool.h>

#include "game.h"
#include "player.h"

#include "ui/ui.h"
#include "ui/gameUI.h"

#include "utils/list.h"

extern bool inGame;

/*** MAIN MENU EVENT ***/

void hanldeMenuEvent (UIScreen *activeScreen, SDL_Event event) {

    if (event.type == SDL_KEYDOWN) {
        SDL_Keycode key = event.key.keysym.sym;

        switch (key) {
            case SDLK_s: startGame (); break;
        }

    }

}

/*** GAME ***/

Position *playerPos = NULL;

void move (u8 newX, u8 newY) {

    playerPos->x = newX;
    playerPos->y = newY;

    // if (canMove (newPos, true)) {
    //     // recalculateFov = true;
    //     playerPos->x = newX;
    //     playerPos->y = newY;
    // } 
    // else resolveCombat (newPos);
    // playerTookTurn = true; 

}

/*** GAME EVENTS ***/

void hanldeGameEvent (UIScreen *activeScreen, SDL_Event event) {

    playerPos = player->pos;

    if (event.type == SDL_KEYDOWN) {
        SDL_Keycode key = event.key.keysym.sym;

        switch (key) {
            case SDLK_w:
                if (activeScene->activeView == MAP_VIEW) move (playerPos->x, playerPos->y - 1);
                break;
            case SDLK_s: 
                if (activeScene->activeView == MAP_VIEW) move (playerPos->x, playerPos->y + 1);
                break;
            case SDLK_a: 
                if (activeScene->activeView == MAP_VIEW) move (playerPos->x - 1, playerPos->y);
                break;
            case SDLK_d:
                if (activeScene->activeView == MAP_VIEW) move (playerPos->x + 1, playerPos->y);
                break;

            default: break;
        }
    }

}