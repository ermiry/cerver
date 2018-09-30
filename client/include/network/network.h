#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

/*** REQUESTS ***/

typedef enum RequestType {

    REQ_GET_FILE = 1,
    POST_SEND_FILE,
    
    REQ_CREATE_LOBBY = 3

} RequestType;

#define CLIENT_REQ_TYPE_SIZE     8

extern int makeRequest (RequestType requestType) ;

/*** CONNECTION ***/

extern bool connected;

extern int connectToServer (void);
extern void disconnectFromServer (void);

#endif