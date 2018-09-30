#include "ui/ui.h"
#include "ui/console.h"
#include "ui/gameUI.h"

#include "utils/list.h"

#include "input.h"

UIScreen *menuScreen = NULL;

void toggleMainMenu (void) {

    

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

    toggleMainMenu ();

    destroyCurrentScreen = cleanUpMenuScene;

    return menuScreen;

}