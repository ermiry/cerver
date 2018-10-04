#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include "server.h"
#include "network.h"

#include "utils/myUtils.h"
#include "utils/config.h"

void die (char *msg) {

    fprintf (stderr, "\n%s\n", msg);

    // try to wrap things up before exit!
    teardown ();

    exit (EXIT_FAILURE);

}

char *getMsgType (LogMsgType type) {

    char temp[10];

    switch (type) {
        case ERROR: strcpy (temp, "[ERROR]"); break;
        case WARNING: strcpy (temp, "[WARNING]"); break;
        case DEBUG: strcpy (temp, "[DEBUG]"); break;
        case TEST: strcpy (temp, "[TEST]"); break;

        case REQ: strcpy (temp, "[REQ]"); break;
        case PACKET: strcpy (temp, "[PACKET]"); break;
        case PLAYER: strcpy (temp, "[PLAYER]"); break;

        default: break;
    }

    char *retval = (char *) calloc (strlen (temp) + 1, sizeof (temp));
    strcpy (retval, temp);

    return retval;

}

// TODO: handle different types at once
// TODO: maybe add some colors?
void logMsg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
    const char *msg) {

    char *first = getMsgType (firstType);
    if (secondType != 0) {
        char *second = getMsgType (secondType);
        fprintf (__stream, "%s", createString ("%s%s: %s\n", first, second, msg));
    }

    else fprintf (__stream, "%s", createString ("%s: %s\n", first, msg));

}

// TODO: have the idea of creating many virtual servers in different sockets?
// TODO: if we want to send a file, maybe create a new TCP socket in a new port?

int main (void) {

    Config *serverConfig = parseConfigFile ("./config/server.cfg");
    if (!serverConfig) die ("\n[ERROR]: Problems loading server config!\n");
    else {
        // use the first configuration
        u32 port = initServer (serverConfig, 1);
        if (port != 0) {
            fprintf (stdout, "\n\nServer has started!\n");
            fprintf (stdout, "Listening on port %i.\n\n", port);

            // we don't need the server config anymor I guess...
            clearConfig (serverConfig);
        }
    } 

    // at this point we are ready to listen for connections...
    fprintf (stdout, "Waiting for connections...\n");
    listenForConnections ();

    return teardown ();

}