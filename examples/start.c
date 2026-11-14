#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/connection/connection.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/utils/log.h>

#define START_ADDRESS				"127.0.0.1"
#define START_PORT					7000
#define START_CONNECTION_QUEUE		16

#define START_REQUEST_TYPE			1

#define START_CONNECT_SECONDS		30
#define START_ECHO_SECONDS			8

typedef enum EchoState {

	ECHO_STATE_WAITING		= 0,
	ECHO_STATE_MATCHED		= 1,
	ECHO_STATE_DIFFERENT	= 2

} EchoState;

static const char *start_message = "Hello from cerver!";

static Cerver *echo_cerver = NULL;

static pthread_mutex_t echo_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t echo_cond = PTHREAD_COND_INITIALIZER;
static EchoState echo_state = ECHO_STATE_WAITING;

#pragma region server

// sends the data of every PACKET_TYPE_APP packet back to its connection
static void echo_handler (void *data) {

	const Packet *packet = (const Packet *) data;

	Packet *response = packet_create (
		PACKET_TYPE_APP, packet->header.request_type,
		packet->data, packet->data_size
	);

	if (!response) return;

	if (!packet_generate (response)) {
		size_t sent = 0;
		(void) packet_send_to (
			response, &sent, false,
			packet->cerver, packet->client, packet->connection, NULL
		);
	}

	packet_delete (response);

}

// the signal only stops the cerver, it is released when cerver_start () returns
static void echo_stop (int signal_number) {

	(void) signal_number;

	if (echo_cerver) (void) cerver_shutdown (echo_cerver);

}

static int echo_server (void) {

	echo_cerver = cerver_create (
		CERVER_TYPE_CUSTOM, "echo",
		START_PORT, PROTOCOL_TCP, false, START_CONNECTION_QUEUE
	);

	if (!echo_cerver) return EXIT_FAILURE;

	cerver_set_handler_type (echo_cerver, CERVER_HANDLER_TYPE_POLL);

	// a cerver that starts again right after it stopped can bind its port
	cerver_set_reusable_address_flags (echo_cerver, true);

	Handler *handler = handler_create (echo_handler);
	handler_set_direct_handle (handler, true);
	cerver_set_app_handlers (echo_cerver, handler, NULL);

	(void) signal (SIGINT, echo_stop);
	(void) signal (SIGTERM, echo_stop);

	// the teardown also releases a cerver that failed to start
	int result = EXIT_SUCCESS;
	if (cerver_start (echo_cerver)) {
		cerver_log_error ("Failed to start the echo cerver!");

		result = EXIT_FAILURE;
	}

	(void) cerver_teardown (echo_cerver);

	return result;

}

#pragma endregion

#pragma region client

static bool echo_matches (const Packet *packet) {

	size_t message_size = strlen (start_message) + 1;

	return (packet->data_size == message_size)
		&& !memcmp (packet->data, start_message, message_size);

}

// runs in the update thread of the connection with every PACKET_TYPE_APP packet
static void echo_client_handler (void *data) {

	const Packet *packet = (const Packet *) data;

	(void) pthread_mutex_lock (&echo_mutex);

	echo_state = echo_matches (packet) ? ECHO_STATE_MATCHED : ECHO_STATE_DIFFERENT;

	(void) pthread_cond_signal (&echo_cond);
	(void) pthread_mutex_unlock (&echo_mutex);

}

static u8 echo_send (Client *client, Connection *connection) {

	u8 result = 1;

	Packet *packet = packet_create (
		PACKET_TYPE_APP, START_REQUEST_TYPE,
		start_message, strlen (start_message) + 1
	);

	if (packet) {
		if (!packet_generate (packet)) {
			size_t sent = 0;
			result = packet_send_to (
				packet, &sent, false, NULL, client, connection, NULL
			);
		}

		packet_delete (packet);
	}

	return result;

}

// waits for the echo until START_ECHO_SECONDS have passed
static EchoState echo_wait (void) {

	struct timespec deadline = CERVER_ZERO_INITIALIZER;
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += START_ECHO_SECONDS;

	(void) pthread_mutex_lock (&echo_mutex);

	int wait_result = 0;
	while ((echo_state == ECHO_STATE_WAITING) && !wait_result) {
		wait_result = pthread_cond_timedwait (&echo_cond, &echo_mutex, &deadline);
	}

	EchoState state = echo_state;

	(void) pthread_mutex_unlock (&echo_mutex);

	return state;

}

static EchoState echo_request (Client *client, Connection *connection) {

	if (client_connect_and_start (client, connection)) {
		cerver_log_error ("Failed to connect to the echo cerver!");

		return ECHO_STATE_WAITING;
	}

	if (echo_send (client, connection)) {
		cerver_log_error ("Failed to send the message!");

		return ECHO_STATE_WAITING;
	}

	return echo_wait ();

}

static int echo_client (void) {

	Client *client = client_create ();
	if (!client) return EXIT_FAILURE;

	Handler *handler = handler_create (echo_client_handler);
	handler_set_direct_handle (handler, true);
	client_set_app_handlers (client, handler, NULL);

	EchoState state = ECHO_STATE_WAITING;

	Connection *connection = client_connection_create (
		client, START_ADDRESS, START_PORT, PROTOCOL_TCP, false
	);

	if (connection) {
		connection_set_max_sleep (connection, START_CONNECT_SECONDS);

		state = echo_request (client, connection);

		(void) client_connection_end (client, connection);
	}

	(void) client_teardown (client);

	if (state == ECHO_STATE_MATCHED) {
		cerver_log_success ("Received the echo: %s", start_message);
	}

	else {
		cerver_log_error ("Did not receive the echo!");
	}

	return (state == ECHO_STATE_MATCHED) ? EXIT_SUCCESS : EXIT_FAILURE;

}

#pragma endregion

#pragma region main

static int start_usage (const char *program) {

	(void) fprintf (stderr, "usage: %s server | client\n", program);

	return EXIT_FAILURE;

}

static int start_run (const char *mode) {

	if (!strcmp (mode, "server")) return echo_server ();

	if (!strcmp (mode, "client")) return echo_client ();

	return -1;

}

int main (int argc, char **argv) {

	if (argc != 2) return start_usage (argv[0]);

	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	int result = start_run (argv[1]);

	cerver_end ();

	return (result < 0) ? start_usage (argv[0]) : result;

}

#pragma endregion
