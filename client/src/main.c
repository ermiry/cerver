#include "game.h"

#include "console.h"

// FIXME:
global Player player;

/*** SCREEN ***/

// TODO: are we cleanning up the console and the screen??
// do we want that to happen?
void renderScreen (SDL_Renderer *renderer, SDL_Texture *screen, Console *console) {

    clearConsole (console);

    // test
    putCharAt (console, '@', player.xPos, player.yPos, 0xFFFFFFFF, 0x000000FF);

    // u32 *pixels = (u32 *) calloc (SCREEN_WIDTH * SCREEN_HEIGHT, sizeof (u32));

    SDL_UpdateTexture (screen, NULL, console->pixels, SCREEN_WIDTH * sizeof (u32));
    SDL_RenderClear (renderer);
    SDL_RenderCopy (renderer, screen, NULL, NULL);
    SDL_RenderPresent (renderer);

}


/*** THREAD ***/

int main (void) {

    // SDL SETUP
    SDL_Init (SDL_INIT_VIDEO);
    SDL_Window *window = SDL_CreateWindow ("Blackrock Dungeons",
         SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);

    SDL_Renderer *renderer = SDL_CreateRenderer (window, 0, SDL_RENDERER_SOFTWARE);

    SDL_SetHint (SDL_HINT_RENDER_SCALE_QUALITY, "linear");
    SDL_RenderSetLogicalSize (renderer, SCREEN_WIDTH, SCREEN_HEIGHT);

    SDL_Texture *screen = SDL_CreateTexture (renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING, SCREEN_WIDTH, SCREEN_HEIGHT);

    // Create our console emulator graphics
    Console *console = initConsole (SCREEN_WIDTH, SCREEN_HEIGHT, NUM_ROWS, NUM_COLS);

    // set up the console font
    setConsoleBitmapFont (console, "./resources/terminal-art.png", 0, 16, 16);

    // FIXME: better player init
    player.xPos = 10;
    player.yPos = 10;

    // Main loop
    // TODO: maybe we want to refactor this
    bool done = false;
    while (!done) {
        SDL_Event event;
        while (SDL_PollEvent (&event) != 0) {
            if (event.type == SDL_QUIT) {
                done = true;
                break;
            }

            // Basic Input
            // Movement with wsad   03/08/2018
            if (event.type == SDL_KEYDOWN) {
                SDL_Keycode key = event.key.keysym.sym;
                switch (key) {
                    case SDLK_w:
                        if (player.yPos > 0) player.yPos -= 1; break;
                    case SDLK_s:
                        if (player.yPos < NUM_ROWS - 1) player.yPos += 1; break;
                    case SDLK_a:
                        if (player.xPos > 0) player.xPos -= 1; break;
                    case SDLK_d:
                        if (player.xPos < NUM_COLS - 1) player.xPos += 1; break;
                    default: break;
                }
            }

        }

        renderScreen (renderer, screen, console);
    }


    // SDL CLEANUP
    SDL_DestroyRenderer (renderer);
    SDL_DestroyWindow (window);

    SDL_Quit ();

    return 0;

}
