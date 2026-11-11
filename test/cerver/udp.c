#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/sessions.h>
#include <cerver/udp.h>

#include <app/auth.h>
#include <app/handler.h>

#include "../test.h"

// an UDP cerver with authentication and sessions, its client is test/client/udp.c:
// every peer that authenticated is removed by its close connection packet before the cerver stops

#define UDP_CERVER_NAME				"udp-cerver"
#define UDP_CERVER_THREADS			4
#define UDP_CERVER_POLL_TIMEOUT		1000
#define UDP_CERVER_AUTH_TRIES		2

static Cerver *cerver = NULL;

static unsigned int success_auths = 0;
static unsigned int connected_clients = 0;
static unsigned int closed_connections = 0;

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

// a peer of an UDP cerver does not have a socket of its own
static void *udp_cerver_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_check_ptr (event_data->client);
	test_check_ptr (event_data->connection);
	test_check_int_eq (event_data->connection->socket->sock_fd, -1, NULL);
	test_check_int_eq (event_data->connection->protocol, PROTOCOL_UDP, NULL);

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

static void udp_cerver_events_register (void) {

	test_check_unsigned_eq (
		cerver_event_register (
			cerver, CERVER_EVENT_CLIENT_SUCCESS_AUTH,
			udp_cerver_event, &success_auths, NULL, false, false
		), 0, NULL
	);

	test_check_unsigned_eq (
		cerver_event_register (
			cerver, CERVER_EVENT_CLIENT_CONNECTED,
			udp_cerver_event, &connected_clients, NULL, false, false
		), 0, NULL
	);

	test_check_unsigned_eq (
		cerver_event_register (
			cerver, CERVER_EVENT_CLIENT_CLOSE_CONNECTION,
			udp_cerver_event, &closed_connections, NULL, false, false
		), 0, NULL
	);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);

	cerver_init ();

	cerver = cerver_create (
		CERVER_TYPE_CUSTOM, UDP_CERVER_NAME,
		CERVER_DEFAULT_PORT, PROTOCOL_UDP, false,
		CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_int_eq (cerver->protocol, PROTOCOL_UDP, NULL);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, UDP_CERVER_THREADS);
	cerver_set_poll_time_out (cerver, UDP_CERVER_POLL_TIMEOUT);

	Handler *app_packet_handler = handler_create (app_handler);
	handler_set_direct_handle (app_packet_handler, true);
	cerver_set_app_handlers (cerver, app_packet_handler, NULL);

	test_check_unsigned_eq (cerver_set_auth (cerver, UDP_CERVER_AUTH_TRIES, app_auth_method), 0, NULL);
	test_check_unsigned_eq (cerver_set_sessions (cerver, session_default_generate_id), 0, NULL);

	udp_cerver_events_register ();

	test_check_unsigned_eq (cerver_start (cerver), 0, NULL);

	test_check (success_auths >= 1, "no client authenticated");
	test_check (connected_clients >= 1, "no client connected");
	test_check_unsigned_eq (closed_connections, success_auths, "a peer was not closed by its client");
	test_check_unsigned_eq (cerver_udp_peers_count (cerver), 0, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	cerver_end ();

	return 0;

}
