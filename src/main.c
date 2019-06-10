#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <signal.h>
#include <pthread.h>

#include "cerver/cerver.h"
#include "cerver/network.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"
#include "cerver/utils/config.h"

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

// TODO: maybe handle this in a separate list by a name?
Server *gameServer = NULL;

// correctly closes any on-going server and process when quitting the appplication
void closeProgram (int dummy) {

    if (gameServer) cerver_teardown (gameServer);
    else cerver_log_msg (stdout, NO_TYPE, NO_TYPE, "There isn't any server to teardown. Quitting application.");

}

// FIXME: how can we signal the process to end?
// TODO: recieve signals to init, retsart or teardown a server -> like a control panel
int main (void) {

    // TODO: if the server uses tcp, we need to listen and accept connections
    // but if the server uses udp, we just need to recieve packets

    /* expected workflow: 
    - crete server(s) with the desired type
    - start the servers
        - tcp servers need to accept connections and handle logic
            - but what if we can make the game server handle different protocols?
        - udp servers just need to handle packets
    */

    // init other program's values...
    signal (SIGINT, closeProgram);

    // create a new server
    // gameServer = cerver_createServer (NULL, GAME_SERVER, destroyGameServer);
    // if (gameServer) {
        // FIXME: if we have got a valid server, we are now ready to listen for connections
        // and we can handle requests of the connected clients

        // pthread_t handlerThread;
        // if (pthread_create (&handlerThread, NULL, connectionHandler, server) != THREAD_OK)
        //     die ("Error creating handler thread!");

        // listenForConnections (server);

        // // at this point we are ready to listen for connections...
        // cerver_log_msg (stdout, SERVER, NO_TYPE, "Waiting for connections...");
        // // TODO: we need to tell our game server to listen for connections
        // listenForConnections (server);

        // startServer (gameServer);
    // }

    // if we reach this point, be sure to correctly clean all of our data...
    // closeProgram (0);
    
    return 0;

}