#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/packets.h"

#include "cerver/threads/thread.h"

static SError *error_serialize (CerverError *error);
static inline void serror_delete (void *ptr);

u8 cerver_error_event_unregister (Cerver *cerver, const CerverErrorType error_type);

#pragma region data

static CerverErrorEventData *cerver_error_event_data_new (void) {

	CerverErrorEventData *error_event_data = (CerverErrorEventData *) malloc (sizeof (CerverErrorEventData));
	if (error_event_data) {
		error_event_data->cerver = NULL;

		error_event_data->client = NULL;
		error_event_data->connection = NULL;

		error_event_data->action_args = NULL;

        error_event_data->error_message = NULL;
	}

	return error_event_data;

}

void cerver_error_event_data_delete (CerverErrorEventData *error_event_data) {

	if (error_event_data) {
        estring_delete (error_event_data->error_message);

        free (error_event_data);
    }

}

static CerverErrorEventData *cerver_error_event_data_create (
    const Cerver *cerver,
	const Client *client, const Connection *connection, 
	void *action_args,
    const char *error_message
) {

	CerverErrorEventData *error_event_data = cerver_error_event_data_new ();
	if (error_event_data) {
		error_event_data->cerver = cerver;

		error_event_data->client = client;
		error_event_data->connection = connection;

		error_event_data->action_args = action_args;

        error_event_data->error_message = error_message ? estring_new (error_message) : NULL;
	}

	return error_event_data;

}

#pragma endregion

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

static CerverErrorEvent *cerver_error_event_get (const Cerver *cerver, const CerverErrorType error_type, 
    ListElement **le_ptr) {

    if (cerver) {
        if (cerver->errors) {
            CerverErrorEvent *error = NULL;
            for (ListElement *le = dlist_start (cerver->errors); le; le = le->next) {
                error = (CerverErrorEvent *) le->data;
                if (error->type == error_type) {
                    if (le_ptr) *le_ptr = le;
                    return error;
                } 
            }
        }
    }

    return NULL;

}

static void cerver_error_event_pop (DoubleList *list, ListElement *le) {

    if (le) {
        void *data = dlist_remove_element (list, le);
        if (data) cerver_error_event_delete (data);
    }

}

// registers an action to be triggered when the specified error event occurs
// if there is an existing action registered to an error event, it will be overrided
// a newly allocated CerverErrorEventData structure will be passed to your method 
// that should be free using the cerver_error_event_data_delete () method
// returns 0 on success, 1 on error
u8 cerver_error_event_register (Cerver *cerver, const CerverErrorType error_type, 
    Action action, void *action_args, Action delete_action_args, 
    bool create_thread, bool drop_after_trigger) {

    u8 retval = 1;

    if (cerver) {
        if (cerver->errors) {
            CerverErrorEvent *error = cerver_error_event_new ();
            if (error) {
                error->type = error_type;

                error->create_thread = create_thread;
                error->drop_after_trigger = drop_after_trigger;

                error->action = action;
                error->action_args = action_args;
                error->delete_action_args = delete_action_args;

                // search if there is an action already registred for that error and remove it
                (void) cerver_error_event_unregister (cerver, error_type);

                if (!dlist_insert_after (
                    cerver->errors, 
                    dlist_end (cerver->errors),
                    error
                )) {
                    retval = 0;
                }
            }
        }
    }

    return retval;
    
}

// unregister the action associated with an error event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
u8 cerver_error_event_unregister (Cerver *cerver, const CerverErrorType error_type) {

    u8 retval = 1;

    if (cerver) {
        if (cerver->errors) {
            CerverErrorEvent *error = NULL;
            for (ListElement *le = dlist_start (cerver->errors); le; le = le->next) {
                error = (CerverErrorEvent *) le->data;
                if (error->type == error_type) {
                    cerver_error_event_delete (dlist_remove_element (cerver->errors, le));
                    retval = 0;

                    break;
                }
            }
        }
    }

    return retval;

}

// triggers all the actions that are registred to an error
void cerver_error_event_trigger (const CerverErrorType error_type, 
    const Cerver *cerver, 
	const Client *client, const Connection *connection,
    const char *error_message
) {

    if (cerver) {
        ListElement *le = NULL;
        CerverErrorEvent *error = cerver_error_event_get (cerver, error_type, &le);
        if (error) {
            // trigger the action
            if (error->action) {
                if (error->create_thread) {
                    pthread_t thread_id = 0;
                    thread_create_detachable (
                        &thread_id,
                        (void *(*)(void *)) error->action, 
                        cerver_error_event_data_create (
							cerver,
                            client, connection,
                            error->action_args,
                            error_message
                        )
                    );
                }

                else {
                    error->action (cerver_error_event_data_create (
						cerver,
                        client, connection, 
                        error->action_args,
                        error_message
                    ));
                }
                
                if (error->drop_after_trigger) cerver_error_event_pop (cerver->errors, le);
            }
        }
    }

}

#pragma endregion

#pragma region error

CerverError *cerver_error_new (const CerverErrorType type, const char *msg) {

    CerverError *error = (CerverError *) malloc (sizeof (CerverError));
    if (error) {
        error->type = type;
        time (&error->timestamp);
        error->msg = msg ? estring_new (msg) : NULL;
    }

    return error;

}

void cerver_error_delete (void *cerver_error_ptr) {

    if (cerver_error_ptr) {
        CerverError *error = (CerverError *) cerver_error_ptr;

        estring_delete (error->msg);
        
        free (error);
    }

}

#pragma endregion

#pragma region packets

// creates an error packet ready to be sent
Packet *error_packet_generate (const CerverErrorType type, const char *msg) {

    Packet *packet = NULL;

    CerverError *error = cerver_error_new (type, msg);
    if (error) {
        SError *serror = error_serialize (error);
        if (serror) {
            packet = packet_create (ERROR_PACKET, serror, sizeof (SError));
            packet_generate (packet);

            serror_delete (serror);
        }

        cerver_error_delete (error);
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

static SError *error_serialize (CerverError *error) {

    SError *serror = NULL;

    if (error) {
        serror = serror_new ();
        serror->timestamp = error->timestamp;
        serror->error_type = error->type;
        memset (serror->msg, 0, ERROR_MESSAGE_LENGTH);
        strncpy (serror->msg, error->msg->str, ERROR_MESSAGE_LENGTH);
    }

    return serror;

}

#pragma endregion