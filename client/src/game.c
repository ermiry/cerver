#include <stdio.h>

#include "game.h"
#include "player.h"

#include "network/network.h"

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

// TODO: add feedback in the message log!

// create a new game lobby
void createGame (void) {

    fprintf (stdout, "\nCreating a new game lobby..\n");

    // FIXME: handle connection success or error
    if (!connected) connectToServer ();
    else fprintf (stdout, "We are already connected to the server.\n");

    // request server for a game lobby
    // TODO: how can we send the server the type of game to create??
    makeRequest (REQ_TEST);

}

// join an existing game lobby
void joinGame (void) {

    fprintf (stdout, "\nSearching for a game..\n");

}