#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "utils/myTime.h"

#include "server.h"
#include "network.h"    // TODO: refactor our code --> we dont want the game to 
// know about the network functions, only the server --> we are using set the socket to
// no blocking the create lobby function!!
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

// TODO: maybe later we can pass the socket of the server?
// TODO: do we need to pass the lobby struct and settings??
// this is called from a request from the owner of the lobby
void startGame (void) {

    // TODO: init here other game structures like the map and enemies

    // TODO: we need to send the players a request to init their game, and we need to send
    // them our game settings

    // TODO: make sure that all the players are sync and have inited their own game

    // if all the players are fine, start the game
    inGame = true;
    gameLoop ();

}

// TODO: add support for multiple lobbys at the smae time
    // --> maybe create a different socket for each lobby?

// we create the lobby, and we wait until the owner of the lobby tell us to start the game
Lobby *newLobby (void) {

    fprintf (stdout, "Creatting a new lobby...\n");

    // TODO: what about a new connection in a new socket??

    // TODO: set the server socket to no blocking and make sure we have a udp connection
    // make sure that we have the correct config for the server in other func
    // FIXME: where do we want to call this?? sock_setNonBlocking (server);

    // TODO: create the lobby and player owner data structures

    // TODO: better manage who created the game lobby --> better players/clients management

    // TODO: init the clients/players structures inside the lobby
    // vector_init (&players, sizeof (Player));

    // TODO: maybe we want to get some game settings first??

    // TODO: make sure that all the players have the same game settings, 
    // so send to them that info first!!

    // FIXME: return the new lobby on success to send its info to the client

    // TODO: error handling when creating the server, and send the error to the client(s)
    // on error just return a NULL;

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