#include <stdbool.h>

#include "game.h"
#include "player.h"

#include "ui/ui.h"
#include "ui/gameUI.h"

#include "utils/list.h"

extern bool inGame;

/*** MAIN MENU ***/

/* extern bool running;
extern bool wasInGame;

extern UIScreen *menuScreen;

extern UIView *characterMenu;
extern void toggleCharacterMenu (void);

extern void cleanUpMenuScene (void); */

// TODO: do we want this here?
void startGame (void) {

    // cleanUpMenuScene ();
    // activeScene = NULL;

    // initGame ();

    // setActiveScene (gameScene ());

    // inGame = true;
    // wasInGame = true;

}

void hanldeMenuEvent (UIScreen *activeScreen, SDL_Event event) {

    if (event.type == SDL_KEYDOWN) {
        SDL_Keycode key = event.key.keysym.sym;

        switch (key) {
            // case SDLK_p: toggleCharacterMenu (); break; 
            // case SDLK_s: if (characterMenu != NULL) startGame (); break;
            // case SDLK_c: break;     // TODO: toggle credits window
            // case SDLK_e: running = false; break;
            // default: break;
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
                if (activeScene->activeView == mapView) move (playerPos->x, playerPos->y - 1);
                break;
            case SDLK_s: 
                if (activeScene->activeView == mapView) move (playerPos->x, playerPos->y + 1);
                break;
            case SDLK_a: 
                if (activeScene->activeView == mapView) move (playerPos->x - 1, playerPos->y);
                break;
            case SDLK_d:
                if (activeScene->activeView == mapView) move (playerPos->x + 1, playerPos->y);
                break;

            default: break;
        }
    }

}