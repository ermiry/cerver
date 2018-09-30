#include "game.h"

typedef struct Player {

    char *name;
    u8 genre;       // 0 female, 1 male
    u32 color;      // for accessibility
    u8 level;
    u16 money [3];  // gold, silver, copper

    Position *pos;
    Graphics *graphics;
    Physics *physics;

} Player;

extern Player *player;

extern Player *createPlayer (void);
void initPlayer (Player *);
extern void resetPlayer (Player *);
extern void destroyPlayer (void);