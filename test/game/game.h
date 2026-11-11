#ifndef _CERVER_TESTS_GAME_H_
#define _CERVER_TESTS_GAME_H_

// the lobbies in memory, without network
extern void game_tests_lobbies (void);

// the game requests of clients to a game cerver in the same process, with poll and threads
extern void game_tests_requests (void);

#endif
