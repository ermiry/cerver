#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/client.h"
#include "cerver/connection.h"
#include "cerver/events.h"

#include "cerver/threads/thread.h"

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

    if (ptr) {
        CerverEvent *event = (CerverEvent *) ptr;
        
        if (event->action_args) {
            if (event->delete_action_args)
                event->delete_action_args (event->action_args);
        }

        free (event);
    }

}


#pragma endregion