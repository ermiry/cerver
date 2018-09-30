#include "ui/ui.h"
#include "ui/console.h"
#include "ui/gameUI.h"

#include "utils/list.h"

#include "input.h"

#define MAIN_MENU_COLOR         0x4B6584FF

#define MULTI_MENU_COLOR        0x34495EFF
#define MULTI_MENU_WIDTH        60
#define MULTI_MENU_HEIGHT       35
#define MULTI_MENU_TOP          5
#define MULTI_MENU_LEFT         10

UIScreen *menuScreen = NULL;

UIView *multiMenu = NULL;

static void renderMultiplayerMenu (Console *console) {

    putStringAtCenter (console, "Multiplayer Menu", 3, WHITE, NO_COLOR);

    putStringAt (console, "[c]reate new game", 10, 10, WHITE, NO_COLOR);
    putStringAt (console, "[j]oin game in progress", 10, 12, WHITE, NO_COLOR);

    putReverseString (console, "kca]b[", 55, 32, WHITE, NO_COLOR);

}

void toggleMultiplayerMenu (void) {

    if (multiMenu == NULL) {
        UIRect menu = { (16 * MULTI_MENU_LEFT), (16 * MULTI_MENU_TOP), (16 * MULTI_MENU_WIDTH), (16 * MULTI_MENU_HEIGHT) };
        multiMenu = newView (menu, MULTI_MENU_WIDTH, MULTI_MENU_HEIGHT, tileset, 0, MULTI_MENU_COLOR, true, renderMultiplayerMenu);
        insertAfter (menuScreen->views, LIST_END (menuScreen->views), multiMenu);

        menuScreen->activeView = MULTI_MENU_VIEW;
    }

    else {
        ListElement *multi = getListElement (activeScene->views, multiMenu);
        destroyView ((UIView *) removeElement (activeScene->views, multi));
        multiMenu = NULL;
    }

}

static void renderMainMenu (Console *console) {

    putStringAtCenter (console, "Main Menu", 3, WHITE, NO_COLOR);

    putStringAt (console, "[s]olo play", 10, 10, WHITE, NO_COLOR);
    putStringAt (console, "[m]ultiplayer", 10, 12, WHITE, NO_COLOR);

}

void createMainMenu (void) {

    UIRect menu = { (16 * FULL_SCREEN_LEFT), (16 * FULL_SCREEN_TOP), (16 * FULL_SCREEN_WIDTH), (16 * FULL_SCREEN_HEIGHT) };
    UIView *mainMenu = newView (menu, FULL_SCREEN_WIDTH, FULL_SCREEN_HEIGHT, tileset, 0, MAIN_MENU_COLOR, true, renderMainMenu);
    insertAfter (menuScreen->views, LIST_END (menuScreen->views), mainMenu);

    menuScreen->activeView = MAIN_MENU_VIEW;

}

/*** MENU SCENE ***/

void cleanUpMenuScene (void) {

    if (menuScreen != NULL) {
        fprintf (stdout, "Cleaning up menu scene...\n");

        while (LIST_SIZE (menuScreen->views) > 0)
            destroyView ((UIView *) removeElement (menuScreen->views, LIST_END (menuScreen->views)));

        destroyList (menuScreen->views);
        free (menuScreen);
        menuScreen = NULL;

        fprintf (stdout, "Done cleaning up menu.\n");
    }

}

UIScreen *menuScene (void) {

    menuScreen = (UIScreen *) malloc (sizeof (UIScreen));
    menuScreen->views = initList (free);
    menuScreen->handleEvent = hanldeMenuEvent;

    createMainMenu ();

    destroyCurrentScreen = cleanUpMenuScene;

    return menuScreen;

}