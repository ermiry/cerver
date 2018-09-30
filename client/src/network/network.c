#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netinet/in.h>

#include <errno.h>

#include "network/network.h"

#include "utils/myUtils.h"

#define PORT    9001

// FIXME:
// #define SERVER_ADDRESS  "192.168.1.7

// TODO: add the ability to read from a config file

// FIXME: hanlde errors in a message log in the main menu!!!

bool connected = false;

int clientSocket;

const char filepath[64] = "foo.txt";

void error (const char *msg) {

    // perror (msg);

    // close (clientSocket);

    // exit (1);

}

/*** REQUESTS ***/

int recieveFile (char *request) {

    // FIXME: make sure that we create a new file if it does not exists
    // prepare the file where we are copying to
    FILE *file = fopen (filepath, "w");
    if (file == NULL) {
        fprintf (stderr, "%s\n", strerror (errno));
        return 1;
    }

    if (write (clientSocket, request, strlen (request)) < 0) {
        fprintf (stderr, "Error on writing!\n\n");
        return 1; 
    }

    size_t len;
    char buffer[BUFSIZ];
    int fileSize;
    int remainData = 0;

    //fprintf(stdout, "\nFile size : %d\n", file_size);
    recv (clientSocket, buffer, BUFSIZ, 0);
    fileSize = atoi (buffer);
    fprintf (stdout, "\nRecieved file size : %d\n", fileSize);

    remainData = fileSize;

    // get the file data
    while (((len = recv (clientSocket, buffer, BUFSIZ, 0)) > 0) && (remainData > 0)) {
            fwrite (buffer, sizeof (char), len, file);
            remainData -= len;
            fprintf (stdout, "Received %ld bytes and we hope %d more bytes\n", len, remainData);
    }

    fprintf (stdout, "Done!");

    fclose (file);
    return 0;

}

int makeRequest (RequestType requestType) {

    char buffer[CLIENT_REQ_TYPE_SIZE];
    bzero (buffer, sizeof (buffer));

    char *request = itoa (requestType, buffer);

    fprintf (stdout, "Request: %s\n", request);
    if (write (clientSocket, request, strlen (request)) < 0) {
        fprintf (stderr, "Error on writing!\n\n");
        return 1; 
    }

    switch (requestType) {
        case REQ_GET_FILE: break;
        case POST_SEND_FILE: break;

        case REQ_CREATE_LOBBY: break;

        default: fprintf (stderr, "Invalid request!\n"); break;
    }

    return 0;   // success

}

/*** CONNECTION ***/

int initClient (void) {

    // create client socket
    int client = socket (AF_INET, SOCK_STREAM, 0);
    if (client < 0) error ("Error creating client socket!\n");

    return client;

}

// TODO: log the process to the message log...
int connectToServer (void) {

    clientSocket = initClient ();

    struct sockaddr_in serverAddress;
    memset (&serverAddress, 0, sizeof (struct sockaddr_in));

    // TODO: add ipv6 config option
    // TODO: add config options
    serverAddress.sin_family = AF_INET;
    // FIXME:
    // inet_pton(AF_INET, SERVER_ADDRESS, &(remote_addr.sin_addr));
    serverAddress.sin_addr.s_addr = INADDR_ANY;
    serverAddress.sin_port = htons (PORT);

    // try to connect to the server
    // FIXME: add the ability to try multiple times

    if (connect (clientSocket, (struct sockaddr *) &serverAddress, sizeof (struct sockaddr)) < 0) {
        fprintf (stderr, "%s\n", strerror (errno));
        error ("Error connecting to server!\n");
    }

    // we expect a welcome message from the server...
    char serverResponse [256];
    recv (clientSocket, &serverResponse, sizeof (serverResponse), 0);
    printf ("\n%s\n", serverResponse);

    connected = true;

    // return 0 on success
    return 0;

}

// TODO: wrap all necessary things before clossing connection
void disconnectFromServer (void) {

    close (clientSocket);

}