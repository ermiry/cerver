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

        case SERVER: fprintf (__stream, COLOR_BLUE "%s" COLOR_RESET, message); break;

        default: fprintf (__stream, "%s", message); break;
    }

    if (!first) free (first);
    if (!second) free (second);

}

/*** THREAD ***/

// FIXME:
void die (char *msg) {

    fprintf (stderr, COLOR_RED "\n%s\n" COLOR_RESET, msg);

    // try to wrap things up before exit!
    // teardown ();

    exit (EXIT_FAILURE);

}

// TODO: have the idea of creating many virtual servers in different sockets?
// TODO: if we want to send a file, maybe create a new TCP socket in a new port?

// TODO: as of 05/10/2018 -- 00:40 -- we only have one server, but we need to make all our functions
// take as a parameter a server to use

int main (void) {

    // create a new server
    Server *server = newServer ();

    Config *serverConfig = parseConfigFile ("./config/server.cfg");
    if (!serverConfig) die ("\n[ERROR]: Problems loading server config!\n");
    else {
        // init our server as a game server
        u32 port = initServer (server, serverConfig, GAME_SERVER);
        if (port > 0) {
            fprintf (stdout, COLOR_GREEN "\n\nServer has started!\n" COLOR_RESET);
            logMsg (stdout, SERVER, NO_TYPE, createString ("Listening on port %i.", port));

            // we don't need the server config anymore
            clearConfig (serverConfig);
        }
    } 

    pthread_t handlerThread;
    if (pthread_create (&handlerThread, NULL, connectionHandler, server) != THREAD_OK)
        die ("Error creating handler thread!");

    listenForConnections (server);

    // at this point we are ready to listen for connections...
    logMsg (stdout, SERVER, NO_TYPE, "Waiting for connections...");
    // TODO: we need to tell our game server to listen for connections
    listenForConnections (server);

    return teardown (server);

}