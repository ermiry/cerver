#ifndef _CERVER_TESTS_CERVER_H_
#define _CERVER_TESTS_CERVER_H_

extern void *on_cever_started (void *event_data_ptr);

extern void *on_cever_teardown (void *event_data_ptr);

extern void *on_client_connected (void *event_data_ptr);

extern void *on_client_new_connection (void *event_data_ptr);

extern void *on_client_close_connection (void *event_data_ptr);

extern void *on_hold_connected (void *event_data_ptr);

extern void *on_hold_disconnected (void *event_data_ptr);

extern void *on_hold_drop (void *event_data_ptr);

extern void *on_client_success_auth (void *event_data_ptr);

extern void *on_client_failed_auth (void *event_data_ptr);

#endif