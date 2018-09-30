/*** This file handles the in game UI and the input for playing the game ***/

#include <string.h>

#include "game.h"
#include "player.h"

#include "ui/ui.h"
#include "ui/console.h"
#include "ui/gameUI.h"

#include "input.h"

extern UIView *activeView;

u8 layerRendered[MAP_WIDTH][MAP_HEIGHT];

u32 wallsFgColor = WHITE;
u32 wallsBgColor = BLACK;
u32 wallsFadedColor;
asciiChar wallGlyph = '#';

void renderItems (Console *console) {

    // FIXME: i dont like this!!
    /* Item *item = NULL;
    Position *itemPos = NULL;
    Graphics *itemGra = NULL;
    for (ListElement *e = LIST_START (items); e != NULL; e = e->next) {
        item = (Item *) e->data;
        itemPos = getGameComponent (item, POSITION);
        if (itemPos != NULL) {
            itemGra = getGameComponent (item, GRAPHICS);

            if (fovMap[itemPos->x][itemPos->y] > 0) {
                itemGra->hasBeenSeen = true;
                putCharAt (console, itemGra->glyph, itemPos->x, itemPos->y, itemGra->fgColor, itemGra->bgColor);
                layerRendered[itemPos->x][itemPos->y] = itemPos->layer;
            }

            else if (itemGra->visibleOutsideFov && itemGra->hasBeenSeen) {
                fullColor = itemGra->fgColor;
                fadedColor = COLOR_FROM_RGBA (RED (fullColor), GREEN (fullColor), BLUE (fullColor), 0x77);
                putCharAt (console, g->glyph, itemPos->x, itemPos->y, fadedColor, 0x000000FF);
                layerRendered[itemPos->x][itemPos->y] = itemPos->layer;
            }
        }
    } */

}

// render things like walls
void renderMapElements (Console *console) {

    // 19/08/2018 -- 17:53 -- we are assuming all walls are visible outside fov
    /* for (u32 i = 0; i < wallCount; i++) {
        if (fovMap[walls[i].x][walls[i].y] > 0) {
            walls[i].hasBeenSeen = true;
            putCharAt (console, wallGlyph, walls[i].x, walls[i].y, wallsFgColor, wallsBgColor);
        }

        else if (walls[i].hasBeenSeen) 
            putCharAt (console, wallGlyph, walls[i].x, walls[i].y, wallsFadedColor, wallsBgColor);
        
    } */

}

// render the gos with a graphics component
void renderGameObjects (Console *console) {

    /* GameObject *go = NULL;
    Position *p = NULL;
    Graphics *g = NULL;
    u32 fullColor;
    u32 fadedColor;
    for (u8 layer = GROUND_LAYER; layer <= TOP_LAYER; layer++) {
        for (ListElement *ptr = LIST_START (gameObjects); ptr != NULL; ptr = ptr->next) {
            go = (GameObject *) ptr->data;
            p = (Position *) getComponent (go, POSITION);
            if (p != NULL && p->layer == layer) {
                g = getComponent (go, GRAPHICS);
                if (fovMap[p->x][p->y] > 0) {
                    g->hasBeenSeen = true;
                    putCharAt (console, g->glyph, p->x, p->y, g->fgColor, g->bgColor);
                    layerRendered[p->x][p->y] = p->layer;
                }

                else if (g->visibleOutsideFov && g->hasBeenSeen) {
                    fullColor = g->fgColor;
                    fadedColor = COLOR_FROM_RGBA (RED (fullColor), GREEN (fullColor), BLUE (fullColor), 0x77);
                    putCharAt (console, g->glyph, p->x, p->y, fadedColor, 0x000000FF);
                    layerRendered[p->x][p->y] = p->layer;
                }
            }
            
        }
    } */    

}

// we are only rendering the player...
static void renderMap (Console *console) {

    // setup the layer rendering    
    for (u32 x = 0; x < MAP_WIDTH; x++)
        for (u32 y = 0; y < MAP_HEIGHT; y ++)   
            layerRendered[x][y] = UNSET_LAYER;
        
    // render the player
    putCharAt (console, player->graphics->glyph, player->pos->x, player->pos->y, 
        player->graphics->fgColor, player->graphics->bgColor);

}

/*** INIT GAME SCREEN ***/

UIScreen *inGameScreen = NULL;

UIView *mapView = NULL;

// FIXME: map values
List *initGameViews (void) {

    List *views = initList (free);

    UIRect mapRect = { 0, 0, (16 * FULL_SCREEN_WIDTH), (16 * FULL_SCREEN_HEIGHT) };
    mapView = newView (mapRect, FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, tileset, 0, BLACK, true, renderMap);
    insertAfter (views, NULL, mapView);

    return views;

}

void destroyGameUI (void);

UIScreen *gameScene (void) {

    List *igViews = initGameViews ();

    if (inGameScreen == NULL) inGameScreen = (UIScreen *) malloc (sizeof (UIScreen));
    
    inGameScreen->views = igViews;
    inGameScreen->activeView = MAP_VIEW;
    inGameScreen->handleEvent = hanldeGameEvent;

    wallsFadedColor = COLOR_FROM_RGBA (RED (wallsFgColor), GREEN (wallsFgColor), BLUE (wallsFgColor), 0x77);

    destroyCurrentScreen = destroyGameUI;

    return inGameScreen;

}

/*** CLEAN UP ***/

void destroyGameUI (void) {

    if (inGameScreen != NULL) {
        fprintf (stdout, "Cleaning in game UI...\n");

        // while (LIST_SIZE (inGameScreen->views) > 0)
            // destroyView ((UIView *) removeElement (inGameScreen->views, LIST_END (inGameScreen->views)));
        
        free (inGameScreen->views);
        free (inGameScreen);
        inGameScreen = NULL;

        fprintf (stdout, "Done cleanning up game UI!\n");
    }

}