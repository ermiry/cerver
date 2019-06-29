#include <stdlib.h>
#include <string.h>

#include "cerver/errors.h"
#include "cerver/packets.h"

Error *error_new (u32 error_type, const char *msg) {

    Error *error = (Error *) malloc (sizeof (Error));
    if (error) {
        memset (error, 0, sizeof (Error));
        error->error_type = error_type;
        error->msg = msg ? str_new (msg) : NULL;
    }

    return error;

}

void error_delete (void *ptr) {

    if (ptr) {
        Error *error = (Error *) ptr;
        str_delete (error->msg);
        free (error);
    }

}

static inline SError *serror_new (void) { 

    SError *serror = (SError *) malloc (sizeof (SError));
    if (serror) memset (serror, 0, sizeof (SError));
    return serror;

}

static inline void serror_delete (void *ptr) { if (ptr) free (ptr); }

static SError *error_serialize (Error *error) {

    if (error) {
        SError *serror = serror_new ();
        serror->error_type = error->error_type;
        memset (serror->msg, 0, 64);
        strncpy (serror->msg, error->msg->str, 64);

        return serror;
    }

    return NULL;

}

// creates an error packet ready to be sent
Packet *error_packet_generate (u32 error_type, const char *msg) {

    Packet *packet = NULL;

    Error *error = error_new (error_type, msg);
    if (error) {
        SError *serror = error_serialize (error);
        if (serror) {
            packet = packet_create (ERROR_PACKET, serror, sizeof (SError));
            packet_generate (packet);

            serror_delete (serror);
        }

        error_delete (error);
    }

    return packet;

}