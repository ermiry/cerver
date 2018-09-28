#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "server.h"
#include "network.h"

// FIXME: move this option to the config file
const bool USE_IPV6 = false;

i32 server;

// TODO: load config settings
u8 initServer (void) {

    server = socket ((USE_IPV6 ? AF_INET6 : AF_INET), SOCK_DGRAM, IPPROTO_UDP);
    if (server < 0) die ("\nERROR: Failed to create server socket!\n");

    if (!sock_setNonBlocking (server))
        die ("\nERROR: Failed to set server socket to non-blocking mode!\n");

    struct sockaddr_storage address;
	memset(&address, 0, sizeof (address));

	if (USE_IPV6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &address;
		addr->sin6_family = AF_INET6;
		addr->sin6_addr = in6addr_any;
		addr->sin6_port = htons (PORT);
	} 

    else {
		struct sockaddr_in *addr = (struct sockaddr_in *) &address;
		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = INADDR_ANY;
		addr->sin_port = htons (PORT);
	}

    if (bind (server, (const struct sockaddr *) &address, sizeof (address)) < 0) 
        die ("\nERROR: Failed to bind server socket!");

}

// wrap things up and exit
u8 teardown (void) {

    fprintf (stdout, "\nClosing server...\n");

    close (server);

    return 0;   // teardown was successfull

}