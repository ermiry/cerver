#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/cerver.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

char *getMsgType (LogMsgType type) {

    char temp[10];

    switch (type) {
        case ERROR: strcpy (temp, "[ERROR]"); break;
        case WARNING: strcpy (temp, "[WARNING]"); break;
        case SUCCESS: strcpy (temp, "[SUCCESS]"); break;
        case DEBUG_MSG: strcpy (temp, "[DEBUG]"); break;
        case TEST: strcpy (temp, "[TEST]"); break;

        case REQ: strcpy (temp, "[REQ]"); break;
        case FILE_REQ: strcpy (temp, "[FILE]"); break;
        case PACKET: strcpy (temp, "[PACKET]"); break;
        case PLAYER: strcpy (temp, "[PLAYER]"); break;
        case GAME: strcpy (temp, "[GAME]"); break;

        case SERVER: strcpy (temp, "[SERVER]"); break;
        case CLIENT: strcpy (temp, "[CLIENT]"); break;

        default: break;
    }

    char *retval = (char *) calloc (strlen (temp) + 1, sizeof (temp));
    strcpy (retval, temp);

    return retval;

}

void logMsg (FILE *__restrict __stream, LogMsgType firstType, LogMsgType secondType,
    const char *msg) {

    char *first = getMsgType (firstType);
    char *second = NULL;
    char *message = NULL;

    if (secondType != 0) {
        second = getMsgType (secondType);

        if (firstType == DEBUG_MSG)
            message = createString ("%s: %s\n", second, msg);
        
        else message = createString ("%s%s: %s\n", first, second, msg);
    }

    else if (firstType != DEBUG_MSG)
        message = createString ("%s: %s\n", first, msg);

    // log messages with color
    switch (firstType) {
        case DEBUG_MSG: 
            fprintf (__stream, COLOR_MAGENTA "%s: " COLOR_RESET "%s\n", first, msg); break;

        case ERROR: fprintf (__stream, COLOR_RED "%s" COLOR_RESET, message); break;
        case WARNING: fprintf (__stream, COLOR_YELLOW "%s" COLOR_RESET, message); break;
        case SUCCESS: fprintf (__stream, COLOR_GREEN "%s" COLOR_RESET, message); break;

        case SERVER: fprintf (__stream, COLOR_BLUE "%s" COLOR_RESET, message); break;

        default: fprintf (__stream, "%s", message); break;
    }

    if (message) free (message);

}

void log_newServer (Server *server) {

    if (server) {
        switch (server->type) {
            case FILE_SERVER: logMsg (stdout, SUCCESS, SERVER, "Created a new file server!"); break;
            case WEB_SERVER: logMsg (stdout, SUCCESS, SERVER, "Created a web server!"); break;
            case GAME_SERVER: logMsg (stdout, SUCCESS, SERVER, "Created a game server!"); break;
            default: break;
        }
    }

}