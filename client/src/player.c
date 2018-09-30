#include "game.h"
#include "player.h"

// #include "config.h"

extern void die (char *);

Player *player = NULL;

// Config *playerConfig = NULL;
//  Config *classesConfig = NULL;

// TODO:
void getPlayerData (void) {

    /* playerConfig = parseConfigFile ("./data/player.cfg");
    if (playerConfig == NULL) 
        die ("Critical Error! No player config!\n");


    classesConfig = parseConfigFile ("./data/classes.cfg");
    if (classesConfig == NULL) 
        die ("Critical Error! No classes config!\n"); */

}

Player *createPlayer (void) {

    Player *p = (Player *) malloc (sizeof (Player));
    p->pos = (Position *) malloc (sizeof (Position));
    p->physics = (Physics *) malloc (sizeof (Physics));
    p->graphics = (Graphics *) malloc (sizeof (Graphics));

    return p;

}

void initPlayer (Player *p) {

    // getPlayerData ();

    p->pos->objectId = 0;
    p->pos->layer = TOP_LAYER;

    p->physics->objectId = 0;
    p->physics->blocksMovement = true;
    p->physics->blocksSight = true;

    // ConfigEntity *playerEntity = getEntityWithId (playerConfig, 1);

    // if (p->name != NULL) free (p->name);
    // p->name = getEntityValue (playerEntity, "name");
    // p->genre = atoi (getEntityValue (playerEntity, "genre"));
    // p->level = atoi (getEntityValue (playerEntity, "level"));

    // money
    // p->money[0] = atoi (getEntityValue (playerEntity, "gold"));
    // p->money[1] = atoi (getEntityValue (playerEntity, "silver"));
    // p->money[2] = atoi (getEntityValue (playerEntity, "copper"));

    // the color of the glyph is based on the class
    p->graphics->objectId = 0;
    p->graphics->bgColor = 0x000000FF;
    p->graphics->fgColor = 0x901515FF;
    p->graphics->hasBeenSeen = false;
    p->graphics->visibleOutsideFov = false;
    p->graphics->glyph = 64;
    p->graphics->name = NULL;

    // we don't need to have this two in memory
    // clearConfig (playerConfig);
    // clearConfig (classesConfig);

    fprintf (stdout, "Init player done!\n");

}

void resetPlayer (Player *p) {

    

}

void destroyPlayer (void) {

    if (player) {
        if (player->name) free (player->name);

        if (player->pos) free (player->pos);
        if (player->graphics) free (player->graphics);
        if (player->physics) free (player->physics);

        free (player);
    }

}