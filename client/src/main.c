#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include <netinet/in.h>

#include <errno.h>

#define PORT    9001

// FIXME:
// #define SERVER_ADDRESS  "192.168.1.7

int clientSocket;

const char filepath[64] = "foo.txt";

void error (const char *msg) {

    perror (msg);

    close (clientSocket);

    exit (1);

}

int initClient () {

    // create client socket
    int client = socket (AF_INET, SOCK_STREAM, 0);
    if (client < 0) error ("Error creating client socket!\n");

    return client;

}

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

int main (void) {

    clientSocket = initClient ();
    struct sockaddr_in serverAddress;

    memset (&serverAddress, 0, sizeof (struct sockaddr_in));

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

    // recieve data from server
    char serverResponse [256];
    recv (clientSocket, &serverResponse, sizeof (serverResponse), 0);

    // handle the server response
    printf ("\n\nThe server sent the data:\n\n%s\n\n", serverResponse);

    int request; 
    // select a request type
    char buffer[8];
    bzero (buffer, sizeof (buffer));
    fprintf (stdout, "Request type: ");
    fflush (stdin);
    fgets (buffer, sizeof (buffer), stdin);

    // request = atoi (buffer);

    if (write (clientSocket, buffer, strlen (buffer)) < 0) {
        fprintf (stderr, "Error on writing!\n\n");
        return 1; 
    }

    /* switch (request) {
        // case 1: 
        //     if (recieveFile (buffer) == 0) fprintf (stdout, "Got the file!\n");
        //     else  fprintf (stderr, "Error recieving file!\n");
        //     break;
        case 1: break;
        case 2: break;
        case 3: break;
        default: fprintf (stderr, "Invalid request!\n"); break;
    } */

    // do {
        // FIXME:
    // } while (request != 0);

    close (clientSocket);

    return 0;

}