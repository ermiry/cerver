// /*** Code for the main menu of the game ***/

// // TODO: later we will want to add a menu to create your profile, etc
// // TODO: also add a menu to create your character, select races and classes
// // TODO: add a settings menu

// #include "blackrock.h"

// #include "resources.h"

// #include "utils/list.h"

// #include "ui/ui.h"
// #include "ui/console.h"

// #include "input.h"

// #define BG_WIDTH    80
// #define BG_HEIGHT   45

// UIScreen *menuScreen = NULL;

// char *launchImg = "./resources/blackrock-small.png";  

// /*** LAUNCH IMAGE ***/

// UIView *launchView = NULL;
// BitmapImage *bgImage = NULL;

// static void renderLaunch (Console *console) {

//     if (bgImage == NULL) bgImage = loadImageFromFile (launchImg);

//     drawImageAt (console, bgImage, 0, 0);

// }

// void toggleLaunch (void) {

//     if (launchView == NULL) {
//         UIRect bgRect = { 0, 0, (16 * BG_WIDTH), (16 * BG_HEIGHT) };
//         launchView = newView (bgRect, BG_WIDTH, BG_HEIGHT, tileset, 0, 0x000000FF, true, renderLaunch);
//         insertAfter (menuScreen->views, NULL, launchView);

//         // menuScreen->activeView = launchView;
//     }

//     else {
//         if (launchView != NULL) {
//             // if (bgImage != NULL) destroyImage (bgImage);

//             ListElement *launch = getListElement (activeScene->views, launchView);
//             destroyView ((UIView *) removeElement (activeScene->views, launch));
//             launchView = NULL;
//         }
//     }

// }

// /*** MENUS ***/

// /*** CHARACTER CREATION ***/

// #define CHAR_CREATION_LEFT		0
// #define CHAR_CREATION_TOP		0
// #define CHAR_CREATION_WIDTH		80
// #define CHAR_CREATION_HEIGHT	45

// #define CHAR_CREATION_COLOR     0x4B6584FF

// #define CHAR_CREATION_TEXT      0xEEEEEEFF

// UIView *characterMenu = NULL;

// // FIXME:
// // 09/09/2018 -- this is only temporary, later we will want to have a better character and profile menu
// static void renderCharacterMenu (Console *console) {

//     UIRect rect = { 0, 0, CHAR_CREATION_WIDTH, CHAR_CREATION_HEIGHT };
//     drawRect (console, &rect, CHAR_CREATION_COLOR, 0, 0xFFFFFFFF);

//     putStringAtCenter (console, "Character Creation", 2, CHAR_CREATION_TEXT, 0x00000000);

// }

// void toggleCharacterMenu (void) {

//     if (characterMenu == NULL) {
//         toggleLaunch ();

//         UIRect charMenu = { (16 * CHAR_CREATION_LEFT), (16 * CHAR_CREATION_TOP), (16 * CHAR_CREATION_WIDTH), (16 * CHAR_CREATION_HEIGHT) };
//         characterMenu = newView (charMenu, CHAR_CREATION_WIDTH, CHAR_CREATION_HEIGHT, tileset, 0, 0x000000FF, true, renderCharacterMenu);
//         insertAfter(activeScene->views, LIST_END (activeScene->views), characterMenu);

//         menuScreen->activeView = characterMenu;
//     }

//     else {
//         if (characterMenu != NULL) {
//             toggleLaunch ();

//             ListElement *charMenu = getListElement (activeScene->views, characterMenu);
//             destroyView ((UIView *) removeElement (activeScene->views, charMenu));
//             characterMenu = NULL;
//         }
//     }

// }


// /*** MENU SCENE ***/

// void cleanUpMenuScene (void) {

//     if (menuScreen != NULL) {
//         if (bgImage != NULL) {
//             free (bgImage->pixels);
//             free (bgImage);
//         }

//         for (ListElement *e = LIST_START (menuScreen->views); e != NULL; e = e->next) 
//             destroyView ((UIView *) e->data);

//         destroyList (menuScreen->views);

//         free (menuScreen);

//         fprintf (stdout, "Done cleaning up menu.\n");
//     }

// }

// UIScreen *menuScene (void) {

//     menuScreen = (UIScreen *) malloc (sizeof (UIScreen));
//     menuScreen->views = initList (NULL);
//     menuScreen->handleEvent = hanldeMenuEvent;

//     toggleLaunch ();

//     destroyCurrentScreen = cleanUpMenuScene;

//     return menuScreen;

// }