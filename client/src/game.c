#include "game.h"
#include "player.h"

void initWorld (void) {

    player = createPlayer ();
    initPlayer (player);

    // spawn the player 
    player->pos->x = MAP_WIDTH / 2;
    player->pos->y = MAP_HEIGHT / 2;

}