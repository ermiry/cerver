#include "ui/ui.h"
#include "ui/console.h"
#include "ui/gameUI.h"

#include "utils/list.h"

#include "input.h"

#define MAIN_MENU_COLOR     0x4B6584FF

UIScreen *menuScreen = NULL;

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