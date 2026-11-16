#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <cerver/collections/dlist.h>

#include <cerver/admin/admin.h>
#include <cerver/admin/cerver.h>
#include <cerver/auth/sessions.h>
#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/client/events.h>
#include <cerver/core/cerver.h>
#include <cerver/core/data.h>
#include <cerver/core/end.h>
#include <cerver/core/report.h>
#include <cerver/core/serialization.h>
#include <cerver/core/start.h>
#include <cerver/handler/errors.h>
#include <cerver/handler/events.h>
#include <cerver/handler/handler.h>

#include "../test.h"

static const char *cerver_name = "test-cerver";

static Cerver *test_cerver_create (void) {

	Cerver *cerver = cerver_create (
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

	return cerver;

}

static void test_cerver_base_configuration (void) {

	Cerver *cerver = test_cerver_create ();

	cerver_set_receive_buffer_size (cerver, 4096);
	test_check_int_eq (cerver->receive_buffer_size, 4096, NULL);

	cerver_set_thpool_n_threads (cerver, 4);
	test_check_int_eq (cerver->n_thpool_threads, 4, NULL);

	cerver_set_reusable_address_flags (cerver, true);
	test_check_bool_eq (cerver->reusable, true, NULL);

	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	test_check_int_eq (cerver->handler_type, CERVER_HANDLER_TYPE_POLL, NULL);

	cerver_set_poll_time_out (cerver, 2000);
	test_check_int_eq (cerver->poll_timeout, 2000, NULL);

	cerver_delete (cerver);

}

#define CERVER_SETTERS_SIZE				2048

// the setters keep their values when they get a value that is not valid,
// and the sessions without a generator use the default one
static void test_cerver_setters_invalid (void) {

	Cerver *cerver = test_cerver_create ();

	test_check_unsigned_eq (cerver_set_welcome_msg (cerver, "welcome"), 0, NULL);
	test_check_unsigned_eq (cerver_set_welcome_msg (cerver, NULL), 1, "set a welcome message without a message");
	test_check_str_eq (cerver->info->welcome, "welcome", NULL);

	cerver_set_receive_buffer_size (cerver, CERVER_SETTERS_SIZE);
	cerver_set_receive_buffer_size (cerver, 0);
	test_check_unsigned_eq (cerver->receive_buffer_size, CERVER_SETTERS_SIZE, "set a receive buffer of 0 bytes");

	cerver_set_on_hold_receive_buffer_size (cerver, CERVER_SETTERS_SIZE);
	cerver_set_on_hold_receive_buffer_size (cerver, 0);
	test_check_unsigned_eq (cerver->on_hold_receive_buffer_size, CERVER_SETTERS_SIZE, "set an on hold receive buffer of 0 bytes");

	cerver_set_max_received_packet_size (cerver, CERVER_SETTERS_SIZE);
	cerver_set_max_received_packet_size (cerver, 0);
	test_check_unsigned_eq (cerver->max_received_packet_size, CERVER_SETTERS_SIZE, "set a max received packet of 0 bytes");

	test_check_unsigned_eq (cerver_set_sessions (cerver, NULL), 0, "the sessions did not use the default generator");
	test_check_bool_eq (cerver->use_sessions, true, NULL);
	test_check_ptr (cerver->session_id_generator);

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	admin_cerver_set_max_fds (cerver->admin, 0);
	test_check_unsigned_eq (cerver->admin->max_n_fds, ADMIN_CERVER_DEFAULT_POLL_FDS, "set an admin poll of 0 fds");

	(void) cerver_teardown (cerver);

}

// the lifecycle of a NULL cerver fails without touching it
static void test_cerver_lifecycle_null (void) {

	test_check_unsigned_eq (cerver_start (NULL), 1, "started a NULL cerver");
	test_check_unsigned_eq (cerver_shutdown (NULL), 1, "shut down a NULL cerver");
	test_check_unsigned_eq (cerver_teardown (NULL), 1, "tore down a NULL cerver");

}

#define CERVER_STOP_NAME				"test-cerver-stop"
#define CERVER_STOP_PORT				7402
#define CERVER_STOP_SLEEP_SECONDS		16
#define CERVER_STOP_JOIN_SECONDS		8
#define CERVER_STOP_SIGNAL_NANOSECONDS	256000000L
#define CERVER_STOP_POLL_NANOSECONDS	1000000L
#define CERVER_STOP_POLLS				(1 << 13)
#define CERVER_STOP_POLL_TIMEOUT		200

static Cerver *stop_cerver = NULL;
static u8 stop_cerver_result = 0;
static unsigned int stop_sleep_done = 0;

// what a signal handler of an application does: cerver_shutdown () must be async-signal-safe
static void test_cerver_stop_signal (int signal_number) {

	(void) signal_number;

	(void) cerver_shutdown (stop_cerver);

}

static void *test_cerver_stop_start (void *cerver_ptr) {

	stop_cerver_result = cerver_start ((Cerver *) cerver_ptr);

	return NULL;

}

static void *test_cerver_stop_sleep (void *cerver_ptr) {

	cerver_sleep ((Cerver *) cerver_ptr, CERVER_STOP_SLEEP_SECONDS);

	test_counter_add (&stop_sleep_done);

	return NULL;

}

static void test_cerver_stop_nap (const long nanoseconds) {

	const struct timespec interval = { 0, nanoseconds };
	(void) nanosleep (&interval, NULL);

}

static void test_cerver_stop_join (pthread_t thread, const char *message) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += CERVER_STOP_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (thread, NULL, &deadline), 0, message);

}

// a cerver_start () that fails returns at once: its result says why the cerver is not running
static void test_cerver_stop_wait_running (pthread_t start_thread) {

	for (unsigned int i = 0; (i < CERVER_STOP_POLLS) && !cerver_is_running (stop_cerver); i++) {
		test_cerver_stop_nap (CERVER_STOP_POLL_NANOSECONDS);
	}

	if (!cerver_is_running (stop_cerver)) {
		test_cerver_stop_join (start_thread, "cerver_start () did not return");
		test_check_unsigned_eq (stop_cerver_result, 0, "cerver_start () failed");
	}

	test_check (cerver_is_running (stop_cerver), "the cerver did not start");

}

// a signal that arrives at the thread that sleeps in cerver_sleep () stops the cerver from its handler,
// and the sleep wakes up long before its time: the stop never takes a lock that the sleep could hold
static void test_cerver_stop_from_signal (void) {

	stop_cerver = cerver_create (
		CERVER_TYPE_CUSTOM, CERVER_STOP_NAME,
		CERVER_STOP_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (stop_cerver);

	cerver_set_reusable_address_flags (stop_cerver, true);
	cerver_set_handler_type (stop_cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (stop_cerver, CERVER_STOP_POLL_TIMEOUT);

	struct sigaction action;
	(void) memset (&action, 0, sizeof (action));
	action.sa_handler = test_cerver_stop_signal;
	(void) sigemptyset (&action.sa_mask);
	test_check_int_eq (sigaction (SIGUSR1, &action, NULL), 0, NULL);

	pthread_t start_thread = 0;
	test_check_int_eq (pthread_create (&start_thread, NULL, test_cerver_stop_start, stop_cerver), 0, NULL);

	test_cerver_stop_wait_running (start_thread);

	pthread_t sleep_thread = 0;
	test_check_int_eq (pthread_create (&sleep_thread, NULL, test_cerver_stop_sleep, stop_cerver), 0, NULL);

	test_cerver_stop_nap (CERVER_STOP_SIGNAL_NANOSECONDS);
	test_check_int_eq (pthread_kill (sleep_thread, SIGUSR1), 0, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&stop_sleep_done, 1, CERVER_STOP_JOIN_SECONDS),
		1, "the sleep did not wake up when the cerver stopped from a signal handler"
	);

	test_cerver_stop_join (sleep_thread, "the sleep did not return");
	test_cerver_stop_join (start_thread, "cerver_start () did not return");

	test_check_unsigned_eq (stop_cerver_result, 0, NULL);
	test_check_unsigned_eq (cerver_teardown (stop_cerver), 0, NULL);

	(void) signal (SIGUSR1, SIG_DFL);

}

static void test_cerver_handler_method (void *data) {

	(void) data;

}

// a different handler deletes the one it replaces (a leak before), the same one again is kept
static void test_cerver_handlers_replaced (void) {

	Cerver *cerver = test_cerver_create ();

	cerver_set_app_handlers (
		cerver,
		handler_create (test_cerver_handler_method), handler_create (test_cerver_handler_method)
	);

	cerver_set_app_handlers (
		cerver,
		handler_create (test_cerver_handler_method), handler_create (test_cerver_handler_method)
	);

	Handler *custom = handler_create (test_cerver_handler_method);
	cerver_set_custom_handler (cerver, custom);
	cerver_set_custom_handler (cerver, custom);
	test_check (cerver->custom_packet_handler == custom, "the same handler was not kept");
	cerver_set_custom_handler (cerver, handler_create (test_cerver_handler_method));

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	admin_cerver_set_custom_handler (cerver->admin, handler_create (test_cerver_handler_method));
	admin_cerver_set_custom_handler (cerver->admin, handler_create (test_cerver_handler_method));
	admin_cerver_set_app_handlers (
		cerver->admin,
		handler_create (test_cerver_handler_method), handler_create (test_cerver_handler_method)
	);

	admin_cerver_set_app_handlers (cerver->admin, NULL, NULL);

	(void) cerver_teardown (cerver);

}

#define CERVER_THRESHOLD_SECONDS		1
#define CERVER_THRESHOLD_WAIT_SECONDS	2
#define CERVER_THRESHOLD_COUNT			5

// the counters that grow are reset after the threshold time, the current ones stay (it was only stored)
static void test_cerver_stats_threshold (void) {

	Cerver *cerver = test_cerver_create ();

	cerver_stats_set_threshold_time (cerver, CERVER_THRESHOLD_SECONDS);
	cerver->stats->total_n_clients = CERVER_THRESHOLD_COUNT;
	cerver->stats->unique_clients = CERVER_THRESHOLD_COUNT;
	cerver->stats->current_n_connected_clients = CERVER_THRESHOLD_COUNT;

	cerver_stats_print (cerver, false, false);
	test_check_unsigned_eq ((unsigned int) cerver->stats->total_n_clients, CERVER_THRESHOLD_COUNT, "reset before the time");

	const struct timespec threshold = { CERVER_THRESHOLD_WAIT_SECONDS, 0 };
	(void) nanosleep (&threshold, NULL);

	cerver_stats_print (cerver, false, false);
	test_check_unsigned_eq ((unsigned int) cerver->stats->total_n_clients, 0, "the stats were not reset after the time");
	test_check_unsigned_eq ((unsigned int) cerver->stats->unique_clients, 0, NULL);
	test_check_unsigned_eq ((unsigned int) cerver->stats->current_n_connected_clients, CERVER_THRESHOLD_COUNT, NULL);

	(void) cerver_teardown (cerver);

}

// enabling the admins again keeps the admin cerver that the cerver already has
static void test_cerver_admin_enable_twice (void) {

	Cerver *cerver = test_cerver_create ();

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	AdminCerver *admin = cerver->admin;

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	test_check (cerver->admin == admin, "enabling the admins again replaced the admin cerver");

	(void) cerver_teardown (cerver);

}

// the admin cerver answers its getters before it starts, and a lookup without a session id finds no admin
static void test_cerver_admin_lookups (void) {

	Cerver *cerver = test_cerver_create ();

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	test_check_unsigned_eq (admin_cerver_get_n_handlers_alive (cerver->admin), 0, "the alive handlers of an admin cerver that has not started");

	Client *client = client_create ();
	test_check_unsigned_eq (client_set_session_id (client, "session"), 0, NULL);

	Admin *admin = admin_create_with_client (client);
	test_check_ptr (admin);
	test_check_int_eq (dlist_insert_at_end (cerver->admin->admins, admin), 0, NULL);

	test_check (admin_get_with_session_id (cerver->admin, "session") == admin, "the admin of its session id was not found");
	test_check_null_ptr (admin_get_with_session_id (cerver->admin, NULL));

	(void) dlist_remove (cerver->admin->admins, admin, NULL);
	admin_delete (admin);

	(void) cerver_teardown (cerver);

}

static void *test_cerver_event_work (void *data) {

	(void) data;

	return NULL;

}

// an event or error type beyond the events of a cerver or a client is an error
static void test_cerver_events_out_of_range (void) {

	Cerver *cerver = test_cerver_create ();

	test_check_unsigned_eq (
		cerver_event_register (
			cerver, (CerverEventType) CERVER_MAX_EVENTS,
			test_cerver_event_work, NULL, NULL, false, false
		),
		1, "registered a cerver event beyond the events"
	);

	test_check_unsigned_eq (cerver_event_unregister (cerver, (CerverEventType) CERVER_MAX_EVENTS), 1, NULL);

	test_check_unsigned_eq (
		cerver_error_event_register (
			cerver, (CerverErrorType) CERVER_MAX_ERRORS,
			test_cerver_event_work, NULL, NULL, false, false
		),
		1, "registered a cerver error beyond the errors"
	);

	test_check_unsigned_eq (cerver_error_event_unregister (cerver, (CerverErrorType) CERVER_MAX_ERRORS), 1, NULL);

	(void) cerver_teardown (cerver);

	Client *client = client_create ();
	test_check_ptr (client);

	test_check_unsigned_eq (
		client_event_register (
			client, (ClientEventType) CLIENT_MAX_EVENTS,
			test_cerver_event_work, NULL, NULL, false, false
		),
		1, "registered a client event beyond the events"
	);

	test_check_unsigned_eq (
		client_error_register (
			client, (ClientErrorType) CLIENT_MAX_ERRORS,
			test_cerver_event_work, NULL, NULL, false, false
		),
		1, "registered a client error beyond the errors"
	);

	test_check_unsigned_eq (
		client_error_trigger ((ClientErrorType) CLIENT_MAX_ERRORS, client, NULL, NULL), 1, NULL
	);

	(void) client_teardown (client);

}

// the trigger of an event that is dropped returned, its action released its data,
// and the arguments were released while the action was still running
static unsigned int dropped_triggered = 0;
static unsigned int dropped_handled = 0;
static unsigned int dropped_early_deletes = 0;
static unsigned int dropped_deletes = 0;

static void test_cerver_dropped_reset (void) {

	dropped_triggered = 0;
	dropped_handled = 0;
	dropped_early_deletes = 0;
	dropped_deletes = 0;

}

static void test_cerver_dropped_args_delete (void *args) {

	(void) args;

	if (!__atomic_load_n (&dropped_handled, __ATOMIC_SEQ_CST)) {
		test_counter_add (&dropped_early_deletes);
	}

	test_counter_add (&dropped_deletes);

}

// runs in its own thread until the trigger has dropped the event
static void test_cerver_dropped_wait (void) {

	(void) test_wait_counter (&dropped_triggered, 1, TEST_WAIT_SECONDS);

	test_counter_add (&dropped_handled);

}

static void *test_cerver_dropped_event (void *data) {

	test_cerver_dropped_wait ();
	cerver_event_data_delete ((CerverEventData *) data);

	return NULL;

}

static void *test_cerver_dropped_error (void *data) {

	test_cerver_dropped_wait ();
	cerver_error_event_data_delete ((CerverErrorEventData *) data);

	return NULL;

}

static void *test_client_dropped_event (void *data) {

	test_cerver_dropped_wait ();
	client_event_data_delete ((ClientEventData *) data);

	return NULL;

}

static void *test_client_dropped_error (void *data) {

	test_cerver_dropped_wait ();
	client_error_data_delete ((ClientErrorData *) data);

	return NULL;

}

static void test_cerver_dropped_check (const char *kind) {

	test_counter_add (&dropped_triggered);

	test_check_unsigned_eq (test_wait_counter (&dropped_deletes, 1, TEST_WAIT_SECONDS), 1, kind);
	test_check_unsigned_eq (dropped_early_deletes, 0, kind);

}

static int dropped_args = 0;

// the arguments of an event or an error that is dropped after its trigger
// live until the action in its own thread releases its data
static void test_cerver_events_dropped (void) {

	Cerver *cerver = test_cerver_create ();

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		cerver_event_register (
			cerver, CERVER_EVENT_STARTED, test_cerver_dropped_event,
			&dropped_args, test_cerver_dropped_args_delete, true, true
		), 0, NULL
	);

	cerver_event_trigger (CERVER_EVENT_STARTED, cerver, NULL, NULL);
	test_cerver_dropped_check ("the arguments of a dropped cerver event were released before its action");

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		cerver_error_event_register (
			cerver, CERVER_ERROR_PACKET_ERROR, test_cerver_dropped_error,
			&dropped_args, test_cerver_dropped_args_delete, true, true
		), 0, NULL
	);

	cerver_error_event_trigger (CERVER_ERROR_PACKET_ERROR, cerver, NULL, NULL, NULL);
	test_cerver_dropped_check ("the arguments of a dropped cerver error were released before its action");

	(void) cerver_teardown (cerver);

	Client *client = client_create ();
	test_check_ptr (client);

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		client_event_register (
			client, CLIENT_EVENT_CONNECTED, test_client_dropped_event,
			&dropped_args, test_cerver_dropped_args_delete, true, true
		), 0, NULL
	);

	client_event_trigger (CLIENT_EVENT_CONNECTED, client, NULL);
	test_cerver_dropped_check ("the arguments of a dropped client event were released before its action");

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		client_error_register (
			client, CLIENT_ERROR_PACKET_ERROR, test_client_dropped_error,
			&dropped_args, test_cerver_dropped_args_delete, true, true
		), 0, NULL
	);

	(void) client_error_trigger (CLIENT_ERROR_PACKET_ERROR, client, NULL, NULL);
	test_cerver_dropped_check ("the arguments of a dropped client error were released before its action");

	(void) client_teardown (client);

}

static void *test_cerver_replaced_action (void *data) {

	cerver_event_data_delete ((CerverEventData *) data);

	return NULL;

}

// the arguments of an event or an error that is replaced while the thread of its trigger still runs
// live until the action releases its data (they were released by the replace)
static void test_cerver_events_replaced (void) {

	Cerver *cerver = test_cerver_create ();

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		cerver_event_register (
			cerver, CERVER_EVENT_STARTED, test_cerver_dropped_event,
			&dropped_args, test_cerver_dropped_args_delete, true, false
		), 0, NULL
	);

	cerver_event_trigger (CERVER_EVENT_STARTED, cerver, NULL, NULL);
	test_check_unsigned_eq (
		cerver_event_register (cerver, CERVER_EVENT_STARTED, test_cerver_replaced_action, NULL, NULL, false, false),
		0, NULL
	);

	test_cerver_dropped_check ("the arguments of a replaced cerver event were released before its action");

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		cerver_error_event_register (
			cerver, CERVER_ERROR_PACKET_ERROR, test_cerver_dropped_error,
			&dropped_args, test_cerver_dropped_args_delete, true, false
		), 0, NULL
	);

	cerver_error_event_trigger (CERVER_ERROR_PACKET_ERROR, cerver, NULL, NULL, NULL);
	test_check_unsigned_eq (cerver_error_event_unregister (cerver, CERVER_ERROR_PACKET_ERROR), 0, NULL);

	test_cerver_dropped_check ("the arguments of an unregistered cerver error were released before its action");

	(void) cerver_teardown (cerver);

}

static void *test_client_replaced_action (void *data) {

	client_event_data_delete ((ClientEventData *) data);

	return NULL;

}

static int replaced_response = 0;

// the arguments and the response of a client event, and the arguments of a client error,
// live until the action in its thread releases its data, also when they are replaced or unregistered before
static void test_client_events_replaced (void) {

	Client *client = client_create ();
	test_check_ptr (client);

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		client_event_register (
			client, CLIENT_EVENT_CONNECTED, test_client_dropped_event,
			&dropped_args, test_cerver_dropped_args_delete, true, false
		), 0, NULL
	);

	client_event_trigger (CLIENT_EVENT_CONNECTED, client, NULL);
	test_check_unsigned_eq (
		client_event_register (client, CLIENT_EVENT_CONNECTED, test_client_replaced_action, NULL, NULL, false, false),
		0, NULL
	);

	test_cerver_dropped_check ("the arguments of a replaced client event were released before its action");

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		client_event_register (
			client, CLIENT_EVENT_CONNECTION_CLOSE, test_client_dropped_event, NULL, NULL, true, false
		), 0, NULL
	);

	client_event_set_response (
		client, CLIENT_EVENT_CONNECTION_CLOSE, &replaced_response, test_cerver_dropped_args_delete
	);

	client_event_trigger (CLIENT_EVENT_CONNECTION_CLOSE, client, NULL);
	test_check_unsigned_eq (client_event_unregister (client, CLIENT_EVENT_CONNECTION_CLOSE), 0, NULL);

	test_cerver_dropped_check ("the response of an unregistered client event was released before its action");

	test_cerver_dropped_reset ();
	test_check_unsigned_eq (
		client_error_register (
			client, CLIENT_ERROR_PACKET_ERROR, test_client_dropped_error,
			&dropped_args, test_cerver_dropped_args_delete, true, false
		), 0, NULL
	);

	(void) client_error_trigger (CLIENT_ERROR_PACKET_ERROR, client, NULL, NULL);
	test_check_unsigned_eq (client_error_unregister (client, CLIENT_ERROR_PACKET_ERROR), 0, NULL);

	test_cerver_dropped_check ("the arguments of an unregistered client error were released before its action");

	(void) client_teardown (client);

}

#define CERVER_SESSION_IDS				1024
#define CERVER_SESSION_ID_LENGTH		64

static bool test_cerver_session_id_is_hex (const char *session_id) {

	bool hex = (strlen (session_id) == CERVER_SESSION_ID_LENGTH);
	for (size_t idx = 0; hex && session_id[idx]; idx++) {
		hex = (strchr ("0123456789abcdef", session_id[idx]) != NULL);
	}

	return hex;

}

static unsigned int test_cerver_session_ids_repeated (char *session_ids[], const unsigned int count) {

	unsigned int repeated = 0;
	for (unsigned int first = 0; first < count; first++) {
		for (unsigned int second = first + 1; second < count; second++) {
			if (!strcmp (session_ids[first], session_ids[second])) repeated += 1;
		}
	}

	return repeated;

}

// the default session ids are random, never repeated in the same second
static void test_cerver_session_default_ids (void) {

	char *session_ids[CERVER_SESSION_IDS] = { NULL };
	const char *session_data = "session";

	for (unsigned int idx = 0; idx < CERVER_SESSION_IDS; idx++) {
		session_ids[idx] = (char *) session_default_generate_id (session_data);
		test_check_ptr (session_ids[idx]);
		test_check (test_cerver_session_id_is_hex (session_ids[idx]), "a session id is not 64 hex characters");
	}

	test_check_unsigned_eq (
		test_cerver_session_ids_repeated (session_ids, CERVER_SESSION_IDS), 0,
		"the default session ids were repeated"
	);

	for (unsigned int idx = 0; idx < CERVER_SESSION_IDS; idx++) {
		free (session_ids[idx]);
	}

}

#define CERVER_INFO_FILL				'a'
#define CERVER_INFO_PORT				7400

// the info of a cerver comes from the network as a summary that its schema validates,
// the longest texts fit in the biggest encoded summary
static void test_cerver_deserialize (void) {

	CerverSummary summary;
	serializer_object_init (&cerver_summary_schema, &summary);

	summary.type = CERVER_TYPE_CUSTOM;
	(void) memset (summary.name, CERVER_INFO_FILL, CERVER_SUMMARY_NAME_SIZE - 1);
	(void) memset (summary.welcome, CERVER_INFO_FILL, CERVER_SUMMARY_WELCOME_SIZE - 1);
	summary.protocol = PROTOCOL_TCP;
	summary.port = CERVER_INFO_PORT;
	summary.use_ipv6 = true;
	summary.auth_required = true;

	u8 buffer[CERVER_SUMMARY_MAX_ENCODED_SIZE];
	size_t written = 0;
	test_check_unsigned_eq (
		serializer_encode (&cerver_summary_schema, &summary, buffer, CERVER_SUMMARY_MAX_ENCODED_SIZE, &written),
		SERIALIZER_RESULT_NONE, NULL
	);

	CerverSummary decoded;
	test_check_unsigned_eq (
		serializer_decode (&cerver_summary_schema, buffer, written, NULL, &decoded),
		SERIALIZER_RESULT_NONE, NULL
	);

	CerverReport *report = cerver_deserialize (&decoded);
	test_check_ptr (report);

	test_check_unsigned_eq (report->name->len, CERVER_SUMMARY_NAME_SIZE - 1, NULL);
	test_check_unsigned_eq (report->welcome->len, CERVER_SUMMARY_WELCOME_SIZE - 1, NULL);
	test_check_true (report->use_ipv6);
	test_check_true (report->auth_required);
	test_check_false (report->uses_sessions);
	test_check_unsigned_eq (report->port, CERVER_INFO_PORT, NULL);

	cerver_report_delete (report);

	// an empty welcome is not created
	decoded.welcome[0] = '\0';
	report = cerver_deserialize (&decoded);
	test_check_ptr (report);
	test_check_null_ptr (report->welcome);
	cerver_report_delete (report);

}

int main (void) {

	test_output_init ();

	srand ((unsigned) time (NULL));

	(void) printf ("Testing CERVER...\n");

	test_cerver_base_configuration ();
	test_cerver_setters_invalid ();
	test_cerver_lifecycle_null ();
	test_cerver_stop_from_signal ();
	test_cerver_handlers_replaced ();
	test_cerver_stats_threshold ();
	test_cerver_admin_enable_twice ();
	test_cerver_admin_lookups ();
	test_cerver_session_default_ids ();
	test_cerver_events_out_of_range ();
	test_cerver_events_dropped ();
	test_cerver_events_replaced ();
	test_client_events_replaced ();
	test_cerver_deserialize ();

	(void) printf ("\nDone with CERVER tests!\n\n");

	return 0;

}
