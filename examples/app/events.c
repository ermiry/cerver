#include <stdlib.h>
#include <stdio.h>

#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/events.h>

#include <cerver/utils/log.h>

#include "events.h"

// the description of the event is its argument
static void *app_cerver_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	cerver_log (
		LOG_TYPE_EVENT, LOG_TYPE_CERVER,
		"Cerver %s: %s",
		event_data->cerver->info->name, (const char *) event_data->action_args
	);

	cerver_event_data_delete (event_data);

	return NULL;

}

static void app_cerver_event_register (Cerver *cerver, const CerverEventType type) {

	if ((type != CERVER_EVENT_NONE) && (type != CERVER_EVENT_UNKNOWN) && !cerver->events[type]) {
		(void) cerver_event_register (
			cerver, type,
			app_cerver_event, (void *) cerver_event_type_description (type), NULL,
			false, false
		);
	}

}

// registers every cerver event that the example did not register
void app_cerver_events_register (Cerver *cerver) {

	if (cerver) {
		#define XX(num, name, description) app_cerver_event_register (cerver, CERVER_EVENT_##name);
		CERVER_EVENT_MAP (XX)
		#undef XX
	}

}

static void *app_client_event (void *event_data_ptr) {

	ClientEventData *event_data = (ClientEventData *) event_data_ptr;

	cerver_log (
		LOG_TYPE_EVENT, LOG_TYPE_CLIENT,
		"Client %s: %s",
		event_data->client->name, (const char *) event_data->action_args
	);

	client_event_data_delete (event_data);

	return NULL;

}

static void app_client_event_register (Client *client, const ClientEventType type) {

	if ((type != CLIENT_EVENT_NONE) && (type != CLIENT_EVENT_UNKNOWN) && !client->events[type]) {
		(void) client_event_register (
			client, type,
			app_client_event, (void *) client_event_type_description (type), NULL,
			false, false
		);
	}

}

// registers every client event that the example did not register
void app_client_events_register (Client *client) {

	if (client) {
		#define XX(num, name, description) app_client_event_register (client, CLIENT_EVENT_##name);
		CLIENT_EVENT_MAP (XX)
		#undef XX
	}

}
