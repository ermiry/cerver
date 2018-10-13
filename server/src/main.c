#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <pthread.h>

#include "server.h"
#include "network.h"

#include "utils/myUtils.h"
#include "utils/config.h"

/*** LOG ***/

#define COLOR_RED       "\x1b[31m"
#define COLOR_GREEN     "\x1b[32m"
#define COLOR_YELLOW    "\x1b[33m"
#define COLOR_BLUE      "\x1b[34m"
#define COLOR_MAGENTA   "\x1b[35m"
#define COLOR_CYAN      "\x1b[36m"
#define COLOR_RESET     "\x1b[0m"

char *getMsgType (LogMsgType type) {

    char temp[10];

    switch (type) {
        case ERROR: strcpy (temp, "[ERROR]"); break;
        case WARNING: strcpy (temp, "[WARNING]"); break;
        case SUCCESS: strcpy (temp, "[SUCCESS]"); break;
        case DEBUG: strcpy (temp, "[DEBUG]"); break;
        case TEST: strcpy (temp, "[TEST]"); break;

        case REQ: strcpy (temp, "[REQ]"); break;
        case FILE_REQ: strcpy (temp, "[FILE]"); break;
        case PACKET: strcpy (temp, "[PACKET]"); break;
        case PLAYER: strcpy (temp, "[PLAYER]"); break;
        case GAME: strcpy (temp, "[GAME]"); break;

        case SERVER: strcpy (temp, "[SERVER]"); break;

        default: break;
    }

    char *retval = (char *) calloc (strlen (temp) + 1, sizeof (temp));
    strcpy (retval, temp);

    return retval;

}

// TODO: maybe log from which server the msg is comming?
void logMsg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
    const char *msg) {

    char *first = getMsgType (firstType);
    char *second = NULL;

    char *message = NULL;

    if (secondType != 0) {
        second = getMsgType (secondType);
        message = createString ("%s%s: %s\n", first, second, msg);
    }

    else message = createString ("%s: %s\n", first, msg);

    // log messages with color
    switch (firstType) {
        case ERROR: fprintf (__stream, COLOR_RED "%s" COLOR_RESET, message); break;
        case WARNING: fprintf (__stream, COLOR_YELLOW "%s" COLOR_RESET, message); break;
        case SUCCESS: fprintf (__stream, COLOR_GREEN "%s" COLOR_RESET, message); break;

        case SERVER: fprintf (__stream, COLOR_BLUE "%s" COLOR_RESET, message); break;

        default: fprintf (__stream, "%s", message); break;
    }

    if (!first) free (first);
    if (!second) free (second);

}

/*** THREAD ***/

// TODO: have the idea of creating many virtual servers in different sockets?
// or maybe we can create many servers and handle the requests via a load balancer,
// that is only listening on one port?
// TODO: if we want to send a file, maybe create a new TCP socket in a new port?

// 13/10/2018 -- the idea here is to have multiple servers that serve different purposses, for example:
// we can have the game servers that handle the in game multiplayer logic,
// but we can also have a server that can handle other types of requests such as getting files
// maybe all the requests can arrive to the load balancer and depending on the request type and 
// any other paramater that we give it, it can redirect the request to the correct server

// FIXME: how can we signal the process to end?
int main (void) {

    // create a new server
    Server *gameServer = createServer (NULL, GAME_SERVER);
    if (gameServer) {
        // FIXME: if we have got a valid server, we are now ready to listen for connections
        // and we can handle requests of the connected clients

        // pthread_t handlerThread;
        // if (pthread_create (&handlerThread, NULL, connectionHandler, server) != THREAD_OK)
        //     die ("Error creating handler thread!");

        // listenForConnections (server);

        // // at this point we are ready to listen for connections...
        // logMsg (stdout, SERVER, NO_TYPE, "Waiting for connections...");
        // // TODO: we need to tell our game server to listen for connections
        // listenForConnections (server);
    }
    
    return 0;

}