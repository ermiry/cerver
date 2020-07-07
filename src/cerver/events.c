#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/events.h"

#include "cerver/threads/thread.h"

u8 cerver_event_unregister (Cerver *cerver, CerverEventType event_type);

#pragma region data

static CerverEventData *cerver_event_data_new (void) {

	CerverEventData *event_data = (CerverEventData *) malloc (sizeof (CerverEventData));
	if (event_data) {
		event_data->cerver = NULL;

		event_data->client = NULL;
		event_data->connection = NULL;

		event_data->action_args = NULL;
		event_data->delete_action_args = NULL;
	}

	return event_data;

}

void cerver_event_data_delete (CerverEventData *event_data) {

	if (event_data) free (event_data);

}

static CerverEventData *cerver_event_data_create (Cerver *cerver,
	Client *client, Connection *connection, 
	CerverEvent *event) {

	CerverEventData *event_data = cerver_event_data_new ();
	if (event_data) {
		event_data->cerver = cerver;

		event_data->client = client;
		event_data->connection = connection;

		event_data->action_args = event->action_args;
		event_data->delete_action_args = event->delete_action_args;
	}

	return event_data;

}

#pragma endregion

#pragma region events

static CerverEvent *cerver_event_new (void) {

	CerverEvent *cerver_event = (CerverEvent *) malloc (sizeof (CerverEvent));
	if (cerver_event) {
		cerver_event->type = CERVER_EVENT_NONE;

		cerver_event->create_thread = false;
		cerver_event->drop_after_trigger = false;

		cerver_event->action = NULL;
		cerver_event->action_args = NULL;
		cerver_event->delete_action_args = NULL;
	}

	return cerver_event;

}

void cerver_event_delete (void *event_ptr) {

	if (event_ptr) {
		CerverEvent *event = (CerverEvent *) event_ptr;
		
		if (event->action_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->action_args);
		}

		free (event);
	}

}

static CerverEvent *cerver_event_get (Cerver *cerver, CerverEventType event_type, 
    ListElement **le_ptr) {

    if (cerver) {
        if (cerver->events) {
            CerverEvent *event = NULL;
            for (ListElement *le = dlist_start (cerver->events); le; le = le->next) {
                event = (CerverEvent *) le->data;
                if (event->type == event_type) {
                    if (le_ptr) *le_ptr = le;
                    return event;
                } 
            }
        }
    }

    return NULL;

}

static void cerver_event_pop (DoubleList *list, ListElement *le) {

    if (le) {
        void *data = dlist_remove_element (list, le);
        if (data) cerver_event_delete (data);
    }

}

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated CerverEventData structure will be passed to your method 
// that should be free using the cerver_event_data_delete () method
// returns 0 on success, 1 on error
u8 cerver_event_register (Cerver *cerver, CerverEventType event_type, 
    Action action, void *action_args, Action delete_action_args, 
    bool create_thread, bool drop_after_trigger) {

    u8 retval = 1;

    if (cerver) {
        if (cerver->events) {
            CerverEvent *event = cerver_event_new ();
            if (event) {
                event->type = event_type;

                event->create_thread = create_thread;
                event->drop_after_trigger = drop_after_trigger;

                event->action = action;
                event->action_args = action_args;
                event->delete_action_args = delete_action_args;

                // search if there is an action already registred for that event and remove it
                (void) cerver_event_unregister (cerver, event_type);

                if (!dlist_insert_after (
                    cerver->events, 
                    dlist_end (cerver->events),
                    event
                )) {
                    retval = 0;
                }
            }
        }
    }

    return retval;
    
}

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error
u8 cerver_event_unregister (Cerver *cerver, CerverEventType event_type) {

    u8 retval = 1;

    if (cerver) {
        if (cerver->events) {
            CerverEvent *event = NULL;
            for (ListElement *le = dlist_start (cerver->events); le; le = le->next) {
                event = (CerverEvent *) le->data;
                if (event->type == event_type) {
                    cerver_event_delete (dlist_remove_element (cerver->events, le));
                    retval = 0;

                    break;
                }
            }
        }
    }

    return retval;

}

// triggers all the actions that are registred to an event
void cerver_event_trigger (CerverEventType event_type, Cerver *cerver, 
	Client *client, Connection *connection) {

    if (client) {
        ListElement *le = NULL;
        CerverEvent *event = cerver_event_get (cerver, event_type, &le);
        if (event) {
            // trigger the action
            if (event->action) {
                if (event->create_thread) {
                    pthread_t thread_id = 0;
                    thread_create_detachable (
                        &thread_id,
                        (void *(*)(void *)) event->action, 
                        cerver_event_data_create (
							cerver,
                            client, connection,
                            event
                        )
                    );
                }

                else {
                    event->action (cerver_event_data_create (
						cerver,
                        client, connection, 
                        event
                    ));
                }
                
                if (event->drop_after_trigger) cerver_event_pop (cerver->events, le);
            }
        }
    }

}

#pragma endregion