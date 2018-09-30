#include <stdio.h>

#include "game.h"
#include "player.h"

#include "ui/ui.h"
#include "ui/menu.h"
#include "ui/gameUI.h"

void initWorld (void) {

    player = createPlayer ();
    initPlayer (player);

    // spawn the player 
    player->pos->x = MAP_WIDTH / 2;
    player->pos->y = MAP_HEIGHT / 2;

}

void startGame (void) {

    cleanUpMenuScene ();
    activeScene = NULL;

    initWorld ();

    setActiveScene (gameScene ());

    inGame = true;
    wasInGame = true;

}

void cleanUpGame (void) {

    destroyPlayer ();

    fprintf (stdout, "Done cleaning up game!\n");

}

/*** MULTIPLAYER ***/

// create a new game lobby
void createGame (void) {

    fprintf (stdout, "\nCreating a new game lobby..\n");

}

// join an existing game lobby
void joinGame (void) {

    fprintf (stdout, "\nSearching for a game..\n");

}