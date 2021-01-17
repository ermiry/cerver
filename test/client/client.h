#ifndef _CERVER_TESTS_CLIENT_H_
#define _CERVER_TESTS_CLIENT_H_

#pragma region events

extern void *client_event_connection_close (void *client_event_data_ptr);

extern void *client_event_auth_sent (void *client_event_data_ptr);

extern void *client_event_success_auth (void *client_event_data_ptr);

#pragma endregion

#pragma region errors

extern void *client_error_failed_auth (void *client_error_data_ptr);

#pragma endregion

#endif