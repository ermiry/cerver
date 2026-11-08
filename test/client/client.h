#ifndef _CERVER_TESTS_CLIENT_H_
#define _CERVER_TESTS_CLIENT_H_

#define CLIENT_TEST_SESSION_ID_SIZE			1024

// the first session id received, copied by the update thread that receives it
extern char client_session_id[CLIENT_TEST_SESSION_ID_SIZE];

// the number of times each event has been triggered
extern unsigned int client_events_connection_close;
extern unsigned int client_events_success_auth;
extern unsigned int client_errors_failed_auth;

#pragma region events

extern void *client_event_connection_close (void *client_event_data_ptr);

extern void *client_event_auth_sent (void *client_event_data_ptr);

extern void *client_event_success_auth (void *client_event_data_ptr);

#pragma endregion

#pragma region errors

extern void *client_error_failed_auth (void *client_error_data_ptr);

#pragma endregion

#endif