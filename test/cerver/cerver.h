#ifndef _CERVER_TESTS_CERVER_H_
#define _CERVER_TESTS_CERVER_H_

extern void *on_cever_started (void *event_data_ptr);

extern void *on_cever_teardown (void *event_data_ptr);

extern void *on_client_connected (void *event_data_ptr);

extern void *on_client_close_connection (void *event_data_ptr);

#endif