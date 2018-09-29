#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "server.h"
#include "network.h"

#include "utils/config.h"

// FIXME: add the option to restart the server to load a new cfg file
// FIXME: add a cfg value to set the protocol
// FIXME: fix ip address

i32 server;

const char welcome[256] = "You have reached the Multiplayer Server!";

u32 initServer (Config *cfg, u8 type) {

	// 28/09/2018 --- we only have one server cfg option
	ConfigEntity *cfgEntity = getEntityWithId (cfg, type);
	if (!cfgEntity) die ("\n[ERROR]: Problems with server config!\n");

	u8 use_ipv6 = atoi (getEntityValue (cfgEntity, "ipv6"));
    server = socket ((use_ipv6 == 1 ? AF_INET6 : AF_INET), SOCK_DGRAM, IPPROTO_UDP);
    if (server < 0) die ("\n[ERROR]: Failed to create server socket!\n");

    if (!sock_setNonBlocking (server))
        die ("\n[ERROR]: Failed to set server socket to non-blocking mode!\n");

    struct sockaddr_storage address;
	memset(&address, 0, sizeof (struct sockaddr_storage));

	u32 port = atoi (getEntityValue (cfgEntity, "port"));
	if (use_ipv6) {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &address;
		addr->sin6_family = AF_INET6;
		addr->sin6_addr = in6addr_any;
		addr->sin6_port = htons (port);
	} 

    else {
		struct sockaddr_in *addr = (struct sockaddr_in *) &address;
		addr->sin_family = AF_INET;
		addr->sin_addr.s_addr = INADDR_ANY;
		addr->sin_port = htons (port);
	}

    if (bind (server, (const struct sockaddr *) &address, sizeof (address)) < 0) 
        die ("\n[ERROR]: Failed to bind server socket!");

	// return the port we war listenning to upon success
	return port;

}

void connectionHandler (i32 client) {

	// send welcome message
	send (client, welcome, sizeof (welcome), 0);

	// handle client request type
	u16 readSize;
	char clientReq[CLIENT_REQ_TYPE_SIZE];

	// FIXME: 29/09/2018 -- we can only hanlde a ONE each connection
	if (readSize = recv (client, clientReq, CLIENT_REQ_TYPE_SIZE, 0) > 0) {
		u8 request = atoi (clientReq);

		switch (request) {
			case 1: fprintf (stdout, "Request type: %i.\n", request); break;
			case 2: fprintf (stdout, "Request type: %i.\n", request); break;
			case 3: fprintf (stdout, "Request type: %i.\n", request); break;

			// TODO: send an error to the client
			default: fprintf (stderr, "[WARNING]: Invalid request type: %i", request); break;
		}
	}

}

// TODO: handle ipv6 configuration
// TODO: do we need to hanlde time here?
void listenForConnections (void) {

	listen (server, 5);

	socklen_t sockLen = sizeof (struct sockaddr_in);
	struct sockaddr_in clientAddress;
	memset(&clientAddress, 0, sizeof (struct sockaddr_in));
	i32 clientSocket;

	// FIXME: 29/09/2018 -- 10:40 -- we only hanlde one client connected at a time
	while ((clientSocket = accept (server, (struct sockaddr *) &clientAddress, &sockLen))) {
		fprintf (stdout, "Client connected: %s.\n", inet_ntoa (clientAddress.sin_addr));

		connectionHandler (clientSocket);
	}

}

// wrap things up and exit
u8 teardown (void) {

    fprintf (stdout, "\nClosing server...\n");

    close (server);

    return 0;   // teardown was successfull

}