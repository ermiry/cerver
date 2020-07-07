#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/events.h"

#include "cerver/threads/thread.h"

#pragma region data

static CerverEventData *cerver_event_data_new (void) {

	CerverEventData *event_data = (CerverEventData *) malloc (sizeof (CerverEventData));
	if (event_data) {
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

static CerverEventData *cerver_event_data_create (Client *client, Connection *connection, 
	CerverEvent *event) {

	CerverEventData *event_data = cerver_event_data_new ();
	if (event_data) {
		event_data->client = client;
		event_data->connection = connection;

		event_data->action_args = event->action_args;
		event_data->delete_action_args = event->delete_action_args;
	}

	return event_data;

}

#pragma endregion

#pragma region event

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


#pragma endregion