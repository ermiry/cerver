#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include <time.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "server.h"
#include "network.h"

#include "utils/config.h"

void die (char *msg) {

    fprintf (stderr, "%s", msg);

    // try to wrap things up before exit!
    close (server);

    exit (EXIT_FAILURE);

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