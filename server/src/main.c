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

void die (char *msg) {

    fprintf (stderr, "%s", msg);

    // try to wrap things up before exit!
    close (server);

    exit (EXIT_FAILURE);

}

int main (void) {

    // TODO: load config settings

    initServer ();

    fprintf (stdout, "\n\nServer has started!\n\n");

    return teardown ();

}