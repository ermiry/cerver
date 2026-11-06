#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/client.h>

#include "../test.h"

#include "client.h"

char client_session_id[CLIENT_TEST_SESSION_ID_SIZE] = { 0 };

unsigned int client_events_connection_close = 0;
unsigned int client_events_success_auth = 0;
unsigned int client_errors_failed_auth = 0;

#pragma region events

void *client_event_connection_close (void *client_event_data_ptr) {

	test_check_ptr (client_event_data_ptr);

	ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

	test_check_ptr (client_event_data->client);

	test_check_ptr (client_event_data->connection);

	client_event_data_delete (client_event_data);

	test_counter_add (&client_events_connection_close);

	return NULL;

}

void *client_event_auth_sent (void *client_event_data_ptr) {

	test_check_ptr (client_event_data_ptr);

	ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

	test_check_ptr (client_event_data->client);

	test_check_ptr (client_event_data->connection);

	client_event_data_delete (client_event_data);

	return NULL;

}

void *client_event_success_auth (void *client_event_data_ptr) {

	test_check_ptr (client_event_data_ptr);

	ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

	// test_check_null_ptr (client_event_data->client);

	test_check_ptr (client_event_data->connection);

	const Client *client = client_event_data->client;
	if (!client_session_id[0] && client && client->session_id) {
		(void) strncpy (
			client_session_id, client->session_id->str,
			CLIENT_TEST_SESSION_ID_SIZE - 1
		);
	}

	client_event_data_delete (client_event_data);

	test_counter_add (&client_events_success_auth);

	return NULL;

}

#pragma endregion

#pragma region errors

void *client_error_failed_auth (void *client_error_data_ptr) {

	test_check_ptr (client_error_data_ptr);

	ClientErrorData *client_error_data = (ClientErrorData *) client_error_data_ptr;

	test_check_ptr (client_error_data->client);

	test_check_ptr (client_error_data->connection);

	client_error_data_delete (client_error_data);

	test_counter_add (&client_errors_failed_auth);

	return NULL;

}

#pragma endregion