#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/errors.h"
#include "cerver/packets.h"

static SError *error_serialize (Error *error);
static inline void serror_delete (void *ptr);

#pragma region event

static CerverErrorEvent *cerver_error_event_new (void) {

	CerverErrorEvent *cerver_error_event = (CerverErrorEvent *) malloc (sizeof (CerverErrorEvent));
	if (cerver_error_event) {
		cerver_error_event->type = CERVER_ERROR_NONE;

		cerver_error_event->create_thread = false;
		cerver_error_event->drop_after_trigger = false;

		cerver_error_event->action = NULL;
		cerver_error_event->action_args = NULL;
		cerver_error_event->delete_action_args = NULL;
	}

	return cerver_error_event;

}

void cerver_error_event_delete (void *event_ptr) {

	if (event_ptr) {
		CerverErrorEvent *event = (CerverErrorEvent *) event_ptr;
		
		if (event->action_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->action_args);
		}

		free (event);
	}

}

#pragma endregion

#pragma region error

Error *error_new (u32 error_type, const char *msg) {

    Error *error = (Error *) malloc (sizeof (Error));
    if (error) {
        time (&error->timestamp);
        error->error_type = error_type;
        error->msg = msg ? estring_new (msg) : NULL;
    }

    return error;

}

void error_delete (void *ptr) {

    if (ptr) {
        Error *error = (Error *) ptr;

        estring_delete (error->msg);
        
        free (error);
    }

}

#pragma endregion

#pragma region packets

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

// creates and send a new error packet
// returns 0 on success, 1 on error
u8 error_packet_generate_and_send (u32 error_type, const char *msg,
    Cerver *cerver, Client *client, Connection *connection) {

    u8 retval = 1;

    Packet *error_packet = error_packet_generate (error_type, msg);
    if (error_packet) {
        packet_set_network_values (error_packet, cerver, client, connection, NULL);
        retval = packet_send (error_packet, 0, NULL, false);
        packet_delete (error_packet);
    }

    return retval;

}

#pragma endregion

#pragma region serialization

static inline SError *serror_new (void) { 

    SError *serror = (SError *) malloc (sizeof (SError));
    if (serror) memset (serror, 0, sizeof (SError));
    return serror;

}

static inline void serror_delete (void *ptr) { if (ptr) free (ptr); }

static SError *error_serialize (Error *error) {

    SError *serror = NULL;

    if (error) {
        serror = serror_new ();
        serror->timestamp = error->timestamp;
        serror->error_type = error->error_type;
        memset (serror->msg, 0, ERROR_MESSAGE_LENGTH);
        strncpy (serror->msg, error->msg->str, ERROR_MESSAGE_LENGTH);
    }

    return serror;

}

#pragma endregion