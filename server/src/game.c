#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "utils/myTime.h"

#include "server.h"
#include "game.h"

#include "utils/vector.h"

/*** DATA STRUCTURES ***/

Vector players;

/*** MULTIPLAYER ***/

bool inGame = false;

void updateGame (void);

void gameLoop (void) {

    double tickInterval = 1.0 / FPS;
    double sleepTime = 0;
	TimeSpec lastIterTime = getTimeSpec ();

	while (inGame) {
        // get packets from players in the game lobby
        recievePackets ();

        // check for disconnected players
		checkTimeouts ();

        // update the game
		updateGame ();

        // send packets to connected players
		for (size_t p = 0; p < players.elements; p++) sendGamePackets (p);

		// self-adjusting sleep that makes the loop contents execute every TICK_INTERVAL seconds
		TimeSpec thisIterTime = getTimeSpec ();
		double timeSinceLastIter = timeElapsed (&lastIterTime, &thisIterTime);
		lastIterTime = thisIterTime;
		sleepTime += tickInterval - timeSinceLastIter;
		if (sleepTime > 0) sleepFor (sleepTime);

	}

}

// TODO: add support for multiple lobbys at the smae time
    // --> maybe create a different socket for each lobby?
u8 createLobby (void) {

    fprintf (stdout, "Creatting a new lobby...\n");

    // TODO: what about a new connection in a new socket??

    // TODO: set the server socket to no blocking and make sure we have a udp
    // connection!!

    // TODO: better manage who created the game lobby --> better players/clients management

    // TODO: don't forget that for creating many lobbys, we need individual game structs 
    // for each one!!!
    // init the necessary game structures
    // vector_init (&players, sizeof (Player));

    // TODO: init the map and other game structures

    inGame = true;

    // TODO: maybe we want to get some game settings first??

    // TODO: make sure that all the players have the same game settings, 
    // so send to them that info first!!

    // start the game
    gameLoop ();

}

/*** THE ACTUAL GAME ***/

// TODO:
void playerDead (Player *player) {

}

void spawnPlayer (Player *player) {
    
}

// TODO: how do we handle collisions with other players?
void updateGame (void) {

    // update the players
    Player *player = NULL;
    for (size_t p = 0; p < players.elements; p++) {
        player = vector_get (&players, p);

        // FIXME: handle resspawn
        if (!player->alive) {
			// player->ticks_until_respawn--;
			// if (player->ticks_until_respawn <= 0)
			// 	player_spawn(player);
			continue;
		} 

        // FIXME:
        // handle player movement
        else {
            player->pos.x = player->input.pos.x;
            player->pos.y = player->input.pos.y;
        }

    }

}