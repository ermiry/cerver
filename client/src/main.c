#include <time.h>
#include <stdio.h>
#include <pthread.h>

#include "game.h"

#include "ui/ui.h"
#include "ui/menu.h"
#include "ui/console.h"

bool running = false;
bool inGame = false;
bool wasInGame = false;

void die (char *error) {

    perror (error);
    running = false;

};


/*** SCREEN ***/

// TODO: are we cleanning up the console and the screen??
// do we want that to happen?
void renderScreen (SDL_Renderer *renderer, SDL_Texture *screen, UIScreen *scene) {

    // render the views from back to front for the current screen
    UIView *v = NULL;
    for (ListElement *e = LIST_START (scene->views); e != NULL; e = e->next) {
        v = (UIView *) LIST_DATA (e);
        clearConsole (v->console);
        v->render (v->console);
        SDL_UpdateTexture (screen, v->pixelRect, v->console->pixels, v->pixelRect->w * sizeof (u32));
    }

    SDL_RenderClear (renderer);
    SDL_RenderCopy (renderer, screen, NULL, NULL);
    SDL_RenderPresent (renderer);

}


/*** CLEAN UP ***/

extern void cleanUpMenuScene (void);

void cleanUp (SDL_Window *window, SDL_Renderer *renderer) {

    if (wasInGame) cleanUpGame ();

    // clean the UI
    destroyCurrentScreen ();
    
    // SDL CLEANUP
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);

    SDL_Quit ();

}

/*** SET UP ***/

void setUpSDL (SDL_Window **window, SDL_Renderer **renderer, SDL_Texture **screen) {

    SDL_Init (SDL_INIT_VIDEO);
    *window = SDL_CreateWindow ("Blackrock Dungeons",
         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);

    *renderer = SDL_CreateRenderer (*window, 0, SDL_RENDERER_SOFTWARE);

    SDL_SetHint (SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize (*renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    *screen = SDL_CreateTexture (*renderer, SDL_PIXELFORMAT_RGBA8888, 
        SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

}


/*** MAIN THREAD ***/

int main (void) {

    srand ((unsigned) time (NULL));

    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *screen = NULL;
    setUpSDL (&window, &renderer, &screen);

    running = true;
    SDL_Event event;
    // TODO: display an fps counter if we give a debug option
    u32 timePerFrame;
    u32 frameStart;
    i32 sleepTime;
    UIScreen *screenForInput;
    
    setActiveScene (menuScene ());

    pthread_t gameThread;

    while (running) {
        timePerFrame = 1000 / FPS_LIMIT;
        frameStart = 0;
        
        while (SDL_PollEvent (&event) != 0) {

            frameStart = SDL_GetTicks ();

            if (event.type == SDL_QUIT) running = false;

            // handle the event in the correct screen
            screenForInput = activeScene;
            screenForInput->handleEvent (screenForInput, event);
        }

        // if (inGame) {
        //     if (pthread_create (&gameThread, NULL, updateGame, NULL) != THREAD_OK)
        //         fprintf (stderr, "Error creating game thread!\n");
        // } 

        // render the correct screen
        renderScreen (renderer, screen, activeScene);

        // if (inGame)
        //     if (pthread_join (gameThread, NULL) != THREAD_OK) fprintf (stderr, "Error joinning game thread!\n");

        // Limit the FPS
        sleepTime = timePerFrame - (SDL_GetTicks () - frameStart);
        if (sleepTime > 0) SDL_Delay (sleepTime);

    }

    cleanUp (window, renderer);

    return 0;

}