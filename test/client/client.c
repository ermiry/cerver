#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/client.h>

#include "../test.h"

#pragma region events

void *client_event_connection_close (void *client_event_data_ptr) {

	test_check_ptr (client_event_data_ptr);

	ClientEventData *client_event_data = (ClientEventData *) client_event_data_ptr;

	test_check_ptr (client_event_data->client);

	test_check_ptr (client_event_data->connection);

	client_event_data_delete (client_event_data);

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

	client_event_data_delete (client_event_data);

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

	return NULL;

}

#pragma endregion