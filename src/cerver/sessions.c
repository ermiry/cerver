#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/client.h"
#include "cerver/sessions.h"
#include "cerver/auth.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/sha-256.h"

SessionData *session_data_new (Packet *packet, AuthData *auth_data, Client *client) {

    SessionData *session_data = (SessionData *) malloc (sizeof (SessionData));
    if (session_data) {
        session_data->packet = packet;
        session_data->auth_data = auth_data;
        session_data->client = client;
    }

    return session_data;

}

void session_data_delete (void *ptr) {

    if (ptr) {
        SessionData *session_data = (SessionData *) ptr;
        session_data->packet = NULL;
        session_data->auth_data = NULL;
        session_data->client = NULL;
    }

}

// create a unique session id for each client based on the current time
void *session_default_generate_id (const void *session_data) {

    char *retval = NULL;

    if (session_data) {
        // SessionData *data = (SessionData *) session_data;

        // get current time
        time_t now = time (0);
        struct tm *nowtm = localtime (&now);
        char buf[80] = { 0 };
        strftime (buf, sizeof(buf), "%Y-%m-%d.%X", nowtm);
        free (nowtm);

        uint8_t hash[32];
        char hash_string[65];

        sha_256_calc (hash, buf, strlen (buf));
        sha_256_hash_to_string (hash_string, hash);

        retval = c_string_create ("%s", hash_string);
    }

    return retval;

}