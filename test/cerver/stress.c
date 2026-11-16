#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../test.h"

// #275: a cerver that sends back the body of every request of the stress client,
// in the poll, threads or handlers mode (the app handler with its own thread and queue),
// test/stress.sh runs it with many clients at the same time

#define STRESS_NAME						"stress-cerver"
#define STRESS_PORT						7600
#define STRESS_THREADS					4
#define STRESS_POLL_TIMEOUT				200
#define STRESS_REQUEST_ECHO				1

#define STRESS_MODE_ARGUMENT			1
#define STRESS_MODE_THREADS				"threads"
#define STRESS_MODE_HANDLERS			"handlers"

static Cerver *cerver = NULL;

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

static void stress_answer (const Packet *packet) {

	if (packet->header.request_type == STRESS_REQUEST_ECHO) {
		Packet *response = packet_generate_request (
			PACKET_TYPE_APP, STRESS_REQUEST_ECHO, packet->data, packet->data_size
		);

		if (response) {
			packet_set_network_values (
				response, packet->cerver, packet->client, packet->connection, NULL
			);

			(void) packet_send (response, 0, NULL, false);

			packet_delete (response);
		}
	}

}

static void stress_handler (void *packet_ptr) {

	stress_answer ((const Packet *) packet_ptr);

}

static void stress_queued_handler (void *handler_data_ptr) {

	stress_answer (((const HandlerData *) handler_data_ptr)->packet);

}

static bool stress_mode_is (int argc, char **argv, const char *mode) {

	return (argc > STRESS_MODE_ARGUMENT) && !strcmp (argv[STRESS_MODE_ARGUMENT], mode);

}

static CerverHandlerType stress_handler_type (int argc, char **argv) {

	return stress_mode_is (argc, argv, STRESS_MODE_THREADS) ?
		CERVER_HANDLER_TYPE_THREADS : CERVER_HANDLER_TYPE_POLL;

}

static Handler *stress_app_handler (int argc, char **argv) {

	Handler *app_handler = NULL;
	if (stress_mode_is (argc, argv, STRESS_MODE_HANDLERS)) {
		app_handler = handler_create (stress_queued_handler);
	}

	else {
		app_handler = handler_create (stress_handler);
		handler_set_direct_handle (app_handler, true);
	}

	return app_handler;

}

int main (int argc, char **argv) {

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_log_set_quiet (true);

	cerver = cerver_create (
		CERVER_TYPE_CUSTOM, STRESS_NAME,
		STRESS_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, STRESS_THREADS);
	cerver_set_handler_type (cerver, stress_handler_type (argc, argv));
	cerver_set_poll_time_out (cerver, STRESS_POLL_TIMEOUT);

	// every connection of the clients stays open, so each one has its own thread
	cerver_set_handle_detachable_threads (cerver, stress_mode_is (argc, argv, STRESS_MODE_THREADS));

	cerver_set_app_handlers (cerver, stress_app_handler (argc, argv), NULL);

	test_check_unsigned_eq (cerver_start (cerver), 0, "Failed to start cerver!");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	cerver_end ();

	return 0;

}
