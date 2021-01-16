#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "app.h"

const char *app_request_to_string (AppRequest type) {

	switch (type) {
		#define XX(num, name, string) case APP_REQUEST_##name: return #string;
		APP_REQUEST_MAP(XX)
		#undef XX
	}

	return NULL;

}

AppMessage *app_data_new (void) {

	AppMessage *app_message = (AppMessage *) malloc (sizeof (AppMessage));
	if (app_message) {
		(void) memset (app_message, 0, sizeof (AppMessage));
	}

	return app_message;

}

void app_data_delete (void *app_data_ptr) {

	if (app_data_ptr) free (app_data_ptr);

}

AppMessage *app_data_create (const char *message) {

	AppMessage *app_message = app_data_new ();
	if (app_message) {
		(void) time (&app_message->timestamp);

		if (message) {
			app_message->len = strlen (message);
			(void) strncpy (
				app_message->message,
				message,
				APP_MESSAGE_LEN - 1
			);
		}
	}

	return app_message;

}

void app_data_print (AppMessage *app_message) {

	if (app_message) {
		(void) printf (
			"Message (%lu): %s\n",
			app_message->len, app_message->message
		);
	}

}