#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "server.h"
#include "game.h"

#include "vector.h"

Vector players;

u8 createLobby (void) {

    fprintf (stdout, "Creatting a new lobby...\n");

    vector_init (&players, sizeof (Player));

    // TODO: init the map and other game structures

}

