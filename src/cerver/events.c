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

u8 cerver_event_unregister (Cerver *cerver, const CerverEventType event_type);

#pragma region types

// get the description for the current event type
const char *cerver_event_type_description (const CerverEventType type) {

	switch (type) {
		#define XX(num, name, description) case CERVER_EVENT_##name: return #description;
		CERVER_EVENT_MAP(XX)
		#undef XX
	}

	return cerver_event_type_description (CERVER_EVENT_UNKNOWN);

}

#pragma endregion

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

static CerverEventData *cerver_event_data_create (
	const Cerver *cerver,
	const Client *client, const Connection *connection,
	CerverEvent *event
) {

	CerverEventData *event_data = cerver_event_data_new ();
	if (event_data) {
		event_data->cerver = cerver;

		event_data->client = client;
		event_data->connection = connection;

		event_data->action_args = event->work_args;
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

		cerver_event->work = NULL;
		cerver_event->work_args = NULL;
		cerver_event->delete_action_args = NULL;
	}

	return cerver_event;

}

void cerver_event_delete (void *event_ptr) {

	if (event_ptr) {
		CerverEvent *event = (CerverEvent *) event_ptr;

		if (event->work_args) {
			if (event->delete_action_args)
				event->delete_action_args (event->work_args);
		}

		free (event);
	}

}

// registers an action to be triggered when the specified event occurs
// if there is an existing action registered to an event, it will be overrided
// a newly allocated CerverEventData structure will be passed to your method
// that should be free using the cerver_event_data_delete () method
// returns 0 on success, 1 on error
u8 cerver_event_register (
	Cerver *cerver,
	const CerverEventType event_type,
	Work work, void *work_args, Action delete_action_args,
	bool create_thread, bool drop_after_trigger
) {

	u8 retval = 1;

	if (cerver) {
		CerverEvent *event = cerver_event_new ();
		if (event) {
			event->type = event_type;

			event->create_thread = create_thread;
			event->drop_after_trigger = drop_after_trigger;

			event->work = work;
			event->work_args = work_args;
			event->delete_action_args = delete_action_args;

			// search if there is an action already registred for that event and remove it
			(void) cerver_event_unregister (cerver, event_type);

			cerver->events[event_type] = event;

			retval = 0;
		}
	}

	return retval;

}

// unregister the action associated with an event
// deletes the action args using the delete_action_args () if NOT NULL
// returns 0 on success, 1 on error or if event is NOT registered
u8 cerver_event_unregister (
	Cerver *cerver, const CerverEventType event_type
) {

	u8 retval = 1;

	if (cerver) {
		if (cerver->events[event_type]) {
			cerver_event_delete (cerver->events[event_type]);
			cerver->events[event_type] = NULL;

			retval = 0;
		}
	}

	return retval;

}

// triggers all the actions that are registred to an event
void cerver_event_trigger (
	const CerverEventType event_type,
	const Cerver *cerver,
	const Client *client, const Connection *connection
) {

	if (cerver) {
		CerverEvent *event = cerver->events[event_type];
		if (event) {
			// trigger the action
			if (event->work) {
				if (event->create_thread) {
					pthread_t thread_id = 0;
					(void) thread_create_detachable (
						&thread_id,
						event->work,
						cerver_event_data_create (
							cerver,
							client, connection,
							event
						)
					);
				}

				else {
					(void) event->work (cerver_event_data_create (
						cerver,
						client, connection,
						event
					));
				}

				if (event->drop_after_trigger) {
					(void) cerver_event_unregister ((Cerver *) cerver, event_type);
				}
			}
		}
	}

}

#pragma endregion