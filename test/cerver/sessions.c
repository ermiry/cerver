#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/sessions.h>

#include <app/auth.h>
#include <app/handler.h>

#include "cerver.h"

#include "../test.h"

static const char *cerver_name = "test-cerver";
static const char *welcome_message = "Hello there!";

static Cerver *cerver = NULL;

static void end (int dummy) {
	
	cerver_teardown (cerver);

	// cerver_end ();

	exit (0);

}

int main (int argc, char **argv) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGKILL, end);

	cerver = cerver_create (
		CERVER_TYPE_CUSTOM,
		cerver_name,
		CERVER_DEFAULT_PORT,
		PROTOCOL_TCP,
		false,
		CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_int_eq (cerver->type, CERVER_TYPE_CUSTOM, NULL);
	test_check_ptr (cerver->info);
	test_check_str_eq (cerver->info->name, cerver_name, NULL);
	test_check_str_len (cerver->info->name, strlen (cerver_name), NULL);
	test_check_int_eq (cerver->port, CERVER_DEFAULT_PORT, NULL);
	test_check_int_eq (cerver->protocol, PROTOCOL_TCP, NULL);
	test_check_bool_eq (cerver->use_ipv6, false, NULL);
	test_check_int_eq (cerver->connection_queue, CERVER_DEFAULT_CONNECTION_QUEUE, NULL);

	cerver_set_welcome_msg (cerver, welcome_message);
	test_check_str_eq (cerver->info->welcome, welcome_message, NULL);
	test_check_str_len (cerver->info->welcome, strlen (welcome_message), NULL);

	cerver_set_receive_buffer_size (cerver, 4096);
	test_check_unsigned_eq (cerver->receive_buffer_size, 4096, NULL);

	cerver_set_thpool_n_threads (cerver, 4);
	test_check_unsigned_eq (cerver->n_thpool_threads, 4, NULL);

	cerver_set_reusable_address_flags (cerver, true);
	test_check_bool_eq (cerver->reusable, true, NULL);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	test_check_int_eq (cerver->handler_type, CERVER_HANDLER_TYPE_POLL, NULL);

	cerver_set_poll_time_out (cerver, 1000);
	test_check_int_eq (cerver->poll_timeout, 1000, NULL);

	/*** handlers ***/
	Handler *app_packet_handler = handler_create (app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	cerver_set_app_handlers (cerver, app_packet_handler, NULL);

	/*** auth ***/
	test_check_unsigned_eq (
		cerver_set_auth (cerver, 1, app_auth_method),
		0, NULL
	);

	test_check_unsigned_eq (
		cerver_set_sessions (cerver, session_default_generate_id),
		0, NULL
	);

	/*** events ***/
	u8 event_result = 0;

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_ON_HOLD_CONNECTED,
		on_hold_connected, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_ON_HOLD_DISCONNECTED,
		on_hold_disconnected, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_ON_HOLD_DROPPED,
		on_hold_drop, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_CLIENT_SUCCESS_AUTH,
		on_client_success_auth, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_CLIENT_FAILED_AUTH,
		on_client_failed_auth, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_CLIENT_CONNECTED,
		on_client_connected, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_CLIENT_NEW_CONNECTION,
		on_client_new_connection, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	event_result = cerver_event_register (
		cerver, 
		CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
		on_client_close_connection, NULL, NULL,
		false, false
	);

	test_check_unsigned_eq (event_result, 0, NULL);

	/*** start ***/
	test_check_unsigned_eq (
		cerver_start (cerver), 0, "Failed to start cerver!"
	);

	return 0;

}