#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app.h"

const char *app_request_to_string (const AppRequest type) {

	switch (type) {
		#define XX(num, name, string) case APP_REQUEST_##name: return #string;
		APP_REQUEST_MAP(XX)
		#undef XX
	}

	return NULL;

}

AppMessage *app_message_new (void) {

	AppMessage *app_message = (AppMessage *) malloc (sizeof (AppMessage));
	if (app_message) {
		(void) memset (app_message, 0, sizeof (AppMessage));
	}

	return app_message;

}

void app_message_delete (void *app_message_ptr) {

	if (app_message_ptr) free (app_message_ptr);

}

void app_message_create_internal (
	AppMessage *app_message,
	const size_t id, const char *message
) {

	app_message->id = id;

	if (message) {
		app_message->len = strlen (message);
		(void) strncpy (
			app_message->message,
			message,
			APP_MESSAGE_SIZE - 1
		);
	}

}

AppMessage *app_message_create (
	const size_t id, const char *message
) {

	AppMessage *app_message = app_message_new ();
	if (app_message) {
		app_message_create_internal (
			app_message,
			id, message
		);
	}

	return app_message;

}

void app_message_print (const AppMessage *app_message) {

	if (app_message) {
		(void) printf (
			"Message [%lu] (%lu): %s\n",
			app_message->id,
			app_message->len, app_message->message
		);
	}

}
