#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <pthread.h>

#include <cerver/admin.h>
#include <cerver/cerver.h>
#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/events.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include <app/auth.h>

#include "../test.h"

// a TCP cerver in the same process, with cerver clients:
// - the app, app error and custom packets reach their handlers, with POLL and THREADS
// - multiple app handlers selected by the packet handler id, and a packet for a missing one
// - authentication that succeeds, and one that fails until the connection is dropped
// - an on hold connection that never authenticates expires, unless the timeout is 0
// - a handler drops its connection with connection_drop_request (), with POLL and THREADS
// - inactive clients are closed by the cerver
// - the update and interval threads run and stop with the cerver
// - the teardown with connected clients, that see the remote close
// - a stop requested before or while cerver_start () runs ends with the cerver stopped
// - every cerver event is triggered once

#define CORE_ADDRESS					"127.0.0.1"
#define CORE_PORT_BASE					7200
#define CORE_NAME						"core-cerver"

#define CORE_THREADS					4
#define CORE_POLL_TIMEOUT				200
#define CORE_JOIN_SECONDS				8
#define CORE_TEARDOWN_MAX_SECONDS		4
#define CORE_START_STOP_ROUNDS			32
#define CORE_START_STOP_STEP_NANOSECONDS	(1 << 17)

#define CORE_PACKETS					32
#define CORE_CLIENTS					4

#define CORE_HANDLERS					3
#define CORE_HANDLERS_TEARDOWN_MAX_SECONDS	0.25

#define CORE_MULTIPLE_HANDLERS			2
#define CORE_MISSING_HANDLER_ID			7

#define CORE_AUTH_MAX_TRIES				2
#define CORE_AUTH_SINGLE_TRY			1
#define CORE_USERNAME					"ermiry"
#define CORE_PASSWORD					"049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b"
#define CORE_BAD_PASSWORD				"bad-password"

#define CORE_INACTIVE_SECONDS			1
#define CORE_INACTIVE_INTERVAL			1

#define CORE_UPDATE_FPS					32
#define CORE_UPDATE_INTERVAL			1
#define CORE_UPDATE_TICKS				4

#define CORE_REQUEST_ECHO				1

#define CORE_PROTOCOL_MAJOR				3
#define CORE_PROTOCOL_MINOR				1

#define CORE_UNKNOWN_PACKET_TYPE		200
#define CORE_MAX_BAD_PACKETS			4
#define CORE_ON_HOLD_MAX_BAD_PACKETS	2
#define CORE_ADMIN_MAX_BAD_PACKETS		3
#define CORE_MAX_PACKET_SIZE			256
#define CORE_BIG_PACKET_SIZE			1024
#define CORE_REQUEST_DROP				2
#define CORE_REQUEST_ADDRESS			3
#define CORE_REQUEST_SLOW				4

#define CORE_SLOW_EVENT_NANOSECONDS		(1L << 29)
#define CORE_SLOW_HANDLER_SECONDS		2
#define CORE_SLOW_UPDATE_FPS			1
#define CORE_DRAIN_TIMEOUT				1

#define CORE_IPV4_LOOPBACK				"127.0.0.1"
#define CORE_IPV6_LOOPBACK				"::1"
#define CORE_IPV6_ANY					"::"
#define CORE_LOCALHOST					"localhost"
#define CORE_INVALID_ADDRESS			"127.0.0.256"
#define CORE_LINK_LOCAL_ZONE			"fe80::1%lo"
#define CORE_UNKNOWN_ZONE				"fe80::1%cerver0"
#define CORE_CONNECT_SINGLE_ATTEMPT		1

// a connect () to each address can take up to 2^9 ms
#define CORE_CONNECT_TIMEOUT				512
#define CORE_CONNECT_TIMEOUT_MIN_SECONDS	0.5
#define CORE_CONNECT_TIMEOUT_MAX_SECONDS	4

// a listener that only takes one connection in its accept queue
#define CORE_BLACKHOLE_BACKLOG			0

#define CORE_ON_HOLD_SECONDS			1
#define CORE_ON_HOLD_POLL_TIMEOUT		200
#define CORE_ON_HOLD_KEPT_SECONDS		3

#define CORE_EVENTS						(CERVER_EVENT_UNKNOWN + 1)

typedef struct CoreCerver {

	Cerver *cerver;
	u16 port;

	pthread_t thread;
	u8 start_result;

} CoreCerver;

typedef struct CoreClient {

	Client *client;
	Connection *connection;

} CoreClient;

static u16 core_port = CORE_PORT_BASE;

// cerver side
static unsigned int app_packets = 0;
static unsigned int app_error_packets = 0;
static unsigned int custom_packets = 0;
static unsigned int handler_packets[CORE_MULTIPLE_HANDLERS] = { 0 };
static unsigned int queued_packets = 0;
static unsigned int slow_events = 0;
static unsigned int slow_started = 0;
static unsigned int slow_finished = 0;
static unsigned int update_ticks = 0;
static unsigned int interval_ticks = 0;
static unsigned int events[CORE_EVENTS] = { 0 };

// written by the handler before it counts the request, read after the count
static char client_ip[INET6_ADDRSTRLEN] = { 0 };
static unsigned int address_requests = 0;

// client side
static unsigned int echoes = 0;
static unsigned int packet_errors = 0;

static void core_counters_reset (void) {

	app_packets = 0;
	app_error_packets = 0;
	custom_packets = 0;
	(void) memset (handler_packets, 0, sizeof (handler_packets));
	queued_packets = 0;
	slow_events = 0;
	slow_started = 0;
	slow_finished = 0;
	update_ticks = 0;
	interval_ticks = 0;
	(void) memset (events, 0, sizeof (events));
	(void) memset (client_ip, 0, sizeof (client_ip));
	address_requests = 0;
	echoes = 0;
	packet_errors = 0;

}

#pragma region cerver

static void core_echo (const Packet *packet) {

	Packet *response = packet_generate_request (
		PACKET_TYPE_APP, CORE_REQUEST_ECHO, NULL, 0
	);

	test_check_ptr (response);

	packet_set_network_values (
		response, packet->cerver, packet->client, packet->connection, NULL
	);

	(void) packet_send (response, 0, NULL, false);

	packet_delete (response);

}

static void core_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	test_counter_add (&app_packets);

	switch (packet->header.request_type) {
		case CORE_REQUEST_ECHO:
			core_echo (packet);
			break;

		// the address that the cerver sees for the client
		case CORE_REQUEST_ADDRESS:
			(void) snprintf (client_ip, sizeof (client_ip), "%s", packet->connection->ip);
			test_counter_add (&address_requests);
			break;

		// the echo is sent before the drop, so it still arrives
		case CORE_REQUEST_DROP:
			core_echo (packet);
			test_check_unsigned_eq (connection_drop_request (packet->connection), 0, NULL);
			break;

		// a handler that is still working when the drain times out
		case CORE_REQUEST_SLOW:
			test_counter_add (&slow_started);
			(void) sleep (CORE_SLOW_HANDLER_SECONDS);
			test_counter_add (&slow_finished);
			break;

		default: break;
	}

}

static void core_app_error_handler (void *packet_ptr) {

	(void) packet_ptr;

	test_counter_add (&app_error_packets);

}

static void core_custom_handler (void *packet_ptr) {

	(void) packet_ptr;

	test_counter_add (&custom_packets);

}

// a multiple handler gets a HandlerData with its id
static void core_multiple_handler (void *handler_data_ptr) {

	const HandlerData *handler_data = (const HandlerData *) handler_data_ptr;

	test_check (handler_data->handler_id < CORE_MULTIPLE_HANDLERS, "unknown handler id");
	test_check_ptr (handler_data->packet);

	test_counter_add (&handler_packets[handler_data->handler_id]);

}

// a handler with its own thread gets the packet inside a HandlerData
static void core_queued_handler (void *handler_data_ptr) {

	const HandlerData *handler_data = (const HandlerData *) handler_data_ptr;

	test_check_ptr (handler_data->packet);

	test_counter_add (&queued_packets);

}

static void core_update (void *update_ptr) {

	(void) update_ptr;

	test_counter_add (&update_ticks);

}

static void core_update_interval (void *update_ptr) {

	(void) update_ptr;

	test_counter_add (&interval_ticks);

}

static void *core_event (void *event_data_ptr) {

	CerverEventData *event_data = (CerverEventData *) event_data_ptr;

	test_check_ptr (event_data->cerver);

	test_counter_add ((unsigned int *) event_data->action_args);

	cerver_event_data_delete (event_data);

	return NULL;

}

// an event action in its own thread that is still running when the cerver stops
static void *core_slow_event (void *event_data_ptr) {

	const struct timespec interval = { 0, CORE_SLOW_EVENT_NANOSECONDS };
	(void) nanosleep (&interval, NULL);

	test_counter_add (&slow_events);

	cerver_event_data_delete ((CerverEventData *) event_data_ptr);

	return NULL;

}

static void core_events_register (Cerver *cerver) {

	for (unsigned int type = CERVER_EVENT_STARTED; type < CERVER_EVENT_LOBBY_CREATE; type++) {
		(void) cerver_event_register (
			cerver, (CerverEventType) type,
			core_event, &events[type], NULL,
			false, false
		);
	}

}

static Cerver *core_cerver_create_base (
	const u16 port, const CerverHandlerType handler_type
) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_CUSTOM, CORE_NAME,
		port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);

	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_thpool_n_threads (cerver, CORE_THREADS);
	cerver_set_handler_type (cerver, handler_type);
	cerver_set_poll_time_out (cerver, CORE_POLL_TIMEOUT);

	core_events_register (cerver);

	return cerver;

}

// the app, app error and custom handlers run in each packet's thread
static Cerver *core_cerver_create (
	const u16 port, const CerverHandlerType handler_type
) {

	Cerver *cerver = core_cerver_create_base (port, handler_type);

	Handler *app_handler = handler_create (core_app_handler);
	handler_set_direct_handle (app_handler, true);

	Handler *app_error_handler = handler_create (core_app_error_handler);
	handler_set_direct_handle (app_error_handler, true);

	cerver_set_app_handlers (cerver, app_handler, app_error_handler);

	Handler *custom_handler = handler_create (core_custom_handler);
	handler_set_direct_handle (custom_handler, true);
	cerver_set_custom_handler (cerver, custom_handler);

	return cerver;

}

// the app, app error and custom handlers have their own threads and queues
static Cerver *core_queued_cerver_create (
	const u16 port, const CerverHandlerType handler_type
) {

	Cerver *cerver = core_cerver_create_base (port, handler_type);

	cerver_set_app_handlers (
		cerver,
		handler_create (core_queued_handler),
		handler_create (core_queued_handler)
	);

	cerver_set_custom_handler (cerver, handler_create (core_queued_handler));

	return cerver;

}

static void *core_cerver_thread (void *core_ptr) {

	CoreCerver *core = (CoreCerver *) core_ptr;

	core->start_result = cerver_start (core->cerver);

	return NULL;

}

// the cerver thread is created without waiting for the cerver to start,
// it writes its result in the caller's structure
static void core_cerver_launch (CoreCerver *core, Cerver *cerver, const u16 port) {

	core->cerver = cerver;
	core->port = port;
	core->start_result = 0;

	test_check_int_eq (pthread_create (&core->thread, NULL, core_cerver_thread, core), 0, NULL);

}

static void core_cerver_start (CoreCerver *core, Cerver *cerver, const u16 port) {

	core_cerver_launch (core, cerver, port);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_STARTED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not start"
	);

}

// cerver_start () returns in time after the cerver was stopped
static void core_cerver_join (CoreCerver *core) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += CORE_JOIN_SECONDS;

	test_check_int_eq (pthread_timedjoin_np (core->thread, NULL, &deadline), 0, "cerver_start () did not return");
	test_check_unsigned_eq (core->start_result, 0, NULL);

}

// cerver_start () returns in time after the cerver was stopped,
// and the teardown drains and releases everything
static void core_cerver_stop (CoreCerver *core) {

	const double start = test_wait_now ();

	test_check_unsigned_eq (cerver_shutdown (core->cerver), 0, NULL);

	core_cerver_join (core);

	test_check_unsigned_eq (cerver_teardown (core->cerver), 0, NULL);

	test_check ((test_wait_now () - start) < CORE_TEARDOWN_MAX_SECONDS, "the cerver took too long to stop");

	test_check_unsigned_eq (events[CERVER_EVENT_TEARDOWN], 1, NULL);

}

// the teardown joins the handlers threads at once, without polling them
static void core_cerver_stop_quickly (CoreCerver *core) {

	test_check_unsigned_eq (cerver_shutdown (core->cerver), 0, NULL);

	core_cerver_join (core);

	const double start = test_wait_now ();

	test_check_unsigned_eq (cerver_teardown (core->cerver), 0, NULL);

	test_check (
		(test_wait_now () - start) < CORE_HANDLERS_TEARDOWN_MAX_SECONDS,
		"the handlers took too long to stop"
	);

	test_check_unsigned_eq (events[CERVER_EVENT_TEARDOWN], 1, NULL);

}

#pragma endregion

#pragma region client

static void core_client_app_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	if (packet->header.request_type == CORE_REQUEST_ECHO) {
		test_counter_add (&echoes);
	}

}

// the cerver sends a CERVER_ERROR_PACKET_ERROR for every bad packet
static void *core_client_packet_error (void *error_data_ptr) {

	client_error_data_delete ((ClientErrorData *) error_data_ptr);

	test_counter_add (&packet_errors);

	return NULL;

}

static CoreClient core_client_create_at (
	const char *address, const u16 port, Credentials *credentials, const bool admin
) {

	CoreClient core = { NULL, NULL };

	core.client = client_create ();
	test_check_ptr (core.client);

	test_check_unsigned_eq (
		client_error_register (
			core.client, CLIENT_ERROR_PACKET_ERROR,
			core_client_packet_error, NULL, NULL,
			false, false
		), 0, NULL
	);

	Handler *app_handler = handler_create (core_client_app_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (core.client, app_handler, NULL);

	core.connection = client_connection_create (
		core.client, address, port, PROTOCOL_TCP, false
	);

	test_check_ptr (core.connection);

	if (credentials) {
		connection_set_auth_data (
			core.connection,
			credentials, sizeof (Credentials),
			credentials_delete, admin
		);
	}

	return core;

}

static CoreClient core_client_create (const u16 port, Credentials *credentials, const bool admin) {

	return core_client_create_at (CORE_ADDRESS, port, credentials, admin);

}

static CoreClient core_client_start (CoreClient core) {

	test_check_int_eq (
		client_connect_and_start (core.client, core.connection), 0,
		"failed to connect to the core cerver"
	);

	return core;

}

static CoreClient core_admin_connect (const u16 port) {

	return core_client_start (
		core_client_create (port, credentials_new (CORE_USERNAME, CORE_PASSWORD), true)
	);

}

static CoreClient core_client_connect (const u16 port, Credentials *credentials) {

	CoreClient core = core_client_create (port, credentials, false);

	test_check_int_eq (
		client_connect_and_start (core.client, core.connection), 0,
		"failed to connect to the core cerver"
	);

	return core;

}

static void core_client_send (
	const CoreClient *core,
	const PacketType packet_type, const u32 request_type,
	const u16 handler_id, const unsigned int count
) {

	for (unsigned int i = 0; i < count; i++) {
		// the request packet sends its own header
		Packet *packet = packet_create_request (packet_type, request_type);
		test_check_ptr (packet);

		packet->header.handler_id = handler_id;

		packet_set_network_values (packet, NULL, core->client, core->connection, NULL);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);

		packet_delete (packet);
	}

}

static void core_client_end (CoreClient *core) {

	if (client_is_running (core->client)) {
		(void) client_connection_end (core->client, core->connection);
	}

	test_check_unsigned_eq (client_teardown (core->client), 0, NULL);

}

static bool core_client_stopped (const CoreClient *core) {

	const struct timespec interval = { 0, TEST_WAIT_POLL_NANOSECONDS };
	const double deadline = test_wait_now () + TEST_WAIT_SECONDS;

	while (client_is_running (core->client) && (test_wait_now () < deadline)) {
		(void) nanosleep (&interval, NULL);
	}

	return !client_is_running (core->client);

}

#pragma endregion

#pragma region tests

// the packets of a cerver without app, app error and custom handlers are released,
// and the ones without a handler in a cerver (none, cerver and game) too (LSan checks it)
static void test_core_without_handlers (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create_base (port, CERVER_HANDLER_TYPE_POLL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	// the packets of one connection are handled in order, so the custom ones are the last
	core_client_send (&client, PACKET_TYPE_NONE, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_CERVER, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_GAME, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_APP, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_APP_ERROR, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_CUSTOM, 0, 0, CORE_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter_64 (&cerver->stats->received_packets->n_custom_packets, CORE_PACKETS, TEST_WAIT_SECONDS),
		CORE_PACKETS, NULL
	);

	core_client_end (&client);

	core_cerver_stop (&core);

}

// app, app error and custom packets reach their handlers
static void test_core_dispatch (const CerverHandlerType handler_type) {

	core_counters_reset ();

	const u16 port = core_port++;
	CoreCerver core;
	core_cerver_start (&core, core_cerver_create (port, handler_type), port);

	CoreClient client = core_client_connect (port, NULL);

	core_client_send (&client, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_APP_ERROR, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_CUSTOM, 0, 0, CORE_PACKETS);

	test_check_unsigned_eq (test_wait_counter (&app_packets, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);
	test_check_unsigned_eq (test_wait_counter (&app_error_packets, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);
	test_check_unsigned_eq (test_wait_counter (&custom_packets, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);
	test_check_unsigned_eq (test_wait_counter (&echoes, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);

	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_CONNECTED], 1, NULL);

	core_client_end (&client);

	// closing its only connection drops the client
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not drop the client"
	);

	core_cerver_stop (&core);

	test_check_unsigned_eq (events[CERVER_EVENT_STARTED], 1, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_CONNECTED], 1, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_CLOSE_CONNECTION], 1, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_DROPPED], 1, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_DISCONNECTED], 0, NULL);

}

static void test_core_dispatch_poll (void) {

	test_core_dispatch (CERVER_HANDLER_TYPE_POLL);

}

static void test_core_dispatch_threads (void) {

	test_core_dispatch (CERVER_HANDLER_TYPE_THREADS);

}

// each packet goes to the handler selected by its id,
// and a packet for a missing handler is released (ASan checks it)
static void test_core_multiple_handlers (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS);

	test_check_int_eq (cerver_set_multiple_handlers (cerver, CORE_MULTIPLE_HANDLERS), 0, NULL);
	for (int id = 0; id < CORE_MULTIPLE_HANDLERS; id++) {
		test_check_unsigned_eq (
			cerver_handlers_add (cerver, handler_create_with_id (id, core_multiple_handler)),
			0, NULL
		);
	}

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	core_client_send (&client, PACKET_TYPE_APP, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_APP, 0, 1, CORE_PACKETS / 2);
	core_client_send (&client, PACKET_TYPE_APP, 0, CORE_MISSING_HANDLER_ID, CORE_PACKETS);

	test_check_unsigned_eq (test_wait_counter (&handler_packets[0], CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);
	test_check_unsigned_eq (test_wait_counter (&handler_packets[1], CORE_PACKETS / 2, TEST_WAIT_SECONDS), CORE_PACKETS / 2, NULL);

	core_client_end (&client);

	core_cerver_stop_quickly (&core);

	test_check_unsigned_eq (handler_packets[0], CORE_PACKETS, NULL);
	test_check_unsigned_eq (handler_packets[1], CORE_PACKETS / 2, NULL);
	test_check_unsigned_eq (app_packets, 0, NULL);

}

// the app, app error and custom handlers threads handle their packets,
// and the teardown stops them without waiting a second for each
static void test_core_handlers_teardown (const CerverHandlerType handler_type) {

	core_counters_reset ();

	const u16 port = core_port++;
	CoreCerver core;
	core_cerver_start (&core, core_queued_cerver_create (port, handler_type), port);

	CoreClient client = core_client_connect (port, NULL);

	core_client_send (&client, PACKET_TYPE_APP, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_APP_ERROR, 0, 0, CORE_PACKETS);
	core_client_send (&client, PACKET_TYPE_CUSTOM, 0, 0, CORE_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter (&queued_packets, CORE_HANDLERS * CORE_PACKETS, TEST_WAIT_SECONDS),
		CORE_HANDLERS * CORE_PACKETS, NULL
	);

	core_client_end (&client);

	core_cerver_stop_quickly (&core);

}

static void test_core_handlers_teardown_poll (void) {

	test_core_handlers_teardown (CERVER_HANDLER_TYPE_POLL);

}

static void test_core_handlers_teardown_threads (void) {

	test_core_handlers_teardown (CERVER_HANDLER_TYPE_THREADS);

}

// valid credentials authenticate, and the packets are handled after it
static void test_core_auth_success (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, credentials_new (CORE_USERNAME, CORE_PASSWORD));

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_SUCCESS_AUTH], 1, TEST_WAIT_SECONDS),
		1, "the client did not authenticate"
	);

	core_client_send (&client, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);
	test_check_unsigned_eq (test_wait_counter (&echoes, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);

	core_client_end (&client);

	core_cerver_stop (&core);

	test_check_unsigned_eq (events[CERVER_EVENT_ON_HOLD_CONNECTED], 1, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_SUCCESS_AUTH], 1, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_FAILED_AUTH], 0, NULL);

}

// wrong credentials fail, and the cerver drops the connection after its last try
static void test_core_auth_failed (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_SINGLE_TRY, app_auth_method), 0, NULL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, credentials_new (CORE_USERNAME, CORE_BAD_PASSWORD));

	test_check (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_FAILED_AUTH], 1, TEST_WAIT_SECONDS) >= 1,
		"the cerver did not reject the credentials"
	);

	test_check (core_client_stopped (&client), "the cerver did not close the connection");

	core_client_end (&client);

	core_cerver_stop (&core);

	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_SUCCESS_AUTH], 0, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_CONNECTED], 0, NULL);
	test_check_unsigned_eq (events[CERVER_EVENT_ON_HOLD_DROPPED], 1, NULL);

}

// with tries left, a failed authentication keeps the connection on hold
static void test_core_auth_retry (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, credentials_new (CORE_USERNAME, CORE_BAD_PASSWORD));

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_FAILED_AUTH], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not reject the credentials"
	);

	test_check (client_is_running (client.client), "the connection was dropped with tries left");
	test_check_unsigned_eq (events[CERVER_EVENT_ON_HOLD_DROPPED], 0, NULL);

	core_client_end (&client);

	core_cerver_stop (&core);

}

// a connection that never authenticates is dropped after the on hold timeout
static void test_core_on_hold_timeout (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);
	cerver_set_on_hold_timeout (cerver, CORE_ON_HOLD_SECONDS);
	cerver_set_on_hold_poll_timeout (cerver, CORE_ON_HOLD_POLL_TIMEOUT);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	// without credentials the client never authenticates
	CoreClient client = core_client_connect (port, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ON_HOLD_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the on hold connection was not dropped"
	);

	test_check (core_client_stopped (&client), "the client did not see the drop");

	core_client_end (&client);

	core_cerver_stop (&core);

	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_SUCCESS_AUTH], 0, NULL);

}

// a timeout of 0 keeps the on hold connection
static void test_core_on_hold_without_timeout (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);
	cerver_set_on_hold_timeout (cerver, 0);
	cerver_set_on_hold_poll_timeout (cerver, CORE_ON_HOLD_POLL_TIMEOUT);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	// nothing can be waited for, the drop must not happen in this time
	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ON_HOLD_DROPPED], 1, CORE_ON_HOLD_KEPT_SECONDS),
		0, "the on hold connection was dropped without a timeout"
	);

	test_check (client_is_running (client.client), "the client was disconnected");

	core_client_end (&client);

	core_cerver_stop (&core);

}

// a handler drops its connection with connection_drop_request (),
// the echo it sent before arrives, and the cerver keeps serving other clients
static void test_core_drop_request (const CerverHandlerType handler_type) {

	core_counters_reset ();

	const u16 port = core_port++;
	CoreCerver core;
	core_cerver_start (&core, core_cerver_create (port, handler_type), port);

	CoreClient dropped = core_client_connect (port, NULL);
	core_client_send (&dropped, PACKET_TYPE_APP, CORE_REQUEST_DROP, 0, 1);

	test_check_unsigned_eq (test_wait_counter (&echoes, 1, TEST_WAIT_SECONDS), 1, "the echo before the drop did not arrive");
	test_check (core_client_stopped (&dropped), "the client did not see the drop");

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not remove the dropped client"
	);

	core_client_end (&dropped);

	CoreClient other = core_client_connect (port, NULL);
	core_client_send (&other, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);
	test_check_unsigned_eq (test_wait_counter (&echoes, 1 + CORE_PACKETS, TEST_WAIT_SECONDS), 1 + CORE_PACKETS, NULL);

	core_client_end (&other);

	core_cerver_stop (&core);

}

static void test_core_drop_request_poll (void) {

	test_core_drop_request (CERVER_HANDLER_TYPE_POLL);

}

static void test_core_drop_request_threads (void) {

	test_core_drop_request (CERVER_HANDLER_TYPE_THREADS);

}

// with check packets, a packet of another major is rejected and the valid ones are handled
static void test_core_check_packets (void) {

	core_counters_reset ();

	const ProtocolVersion previous = packets_get_protocol_version ();
	const ProtocolVersion local = { CORE_PROTOCOL_MAJOR, CORE_PROTOCOL_MINOR };
	packets_set_protocol_version (local);

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_check_packets (cerver, true);

	// only the rejection, test_core_bad_packets () checks the limit
	cerver_set_max_bad_packets (cerver, 0);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	// the bad ones go first, so they were handled when the good ones arrive
	for (unsigned int i = 0; i < CORE_PACKETS; i++) {
		Packet *packet = packet_create_request (PACKET_TYPE_APP, CORE_REQUEST_ECHO);
		test_check_ptr (packet);

		packet->header.version.protocol_version.major = CORE_PROTOCOL_MAJOR + 1;

		packet_set_network_values (packet, NULL, client.client, client.connection, NULL);
		test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);
		packet_delete (packet);
	}

	core_client_send (&client, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);

	test_check_unsigned_eq (test_wait_counter (&echoes, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);
	test_check_unsigned_eq (app_packets, CORE_PACKETS, "a packet of another major was handled");

	core_client_end (&client);

	core_cerver_stop (&core);

	packets_set_protocol_version (previous);

}

static void core_client_send_unknown (const CoreClient *core, const unsigned int count) {

	core_client_send (core, (PacketType) CORE_UNKNOWN_PACKET_TYPE, 0, 0, count);

}

// every bad packet gets an error, and the connection is dropped at the limit
static void test_core_bad_packets (const CerverHandlerType handler_type) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, handler_type);
	cerver_set_max_bad_packets (cerver, CORE_MAX_BAD_PACKETS);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	core_client_send_unknown (&client, CORE_MAX_BAD_PACKETS - 1);
	test_check_unsigned_eq (
		test_wait_counter (&packet_errors, CORE_MAX_BAD_PACKETS - 1, TEST_WAIT_SECONDS),
		CORE_MAX_BAD_PACKETS - 1, "the bad packets did not get their errors"
	);

	// below the limit the connection keeps working
	core_client_send (&client, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, 1);
	test_check_unsigned_eq (test_wait_counter (&echoes, 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check (client_is_running (client.client), "the connection was dropped before the limit");

	core_client_send_unknown (&client, 1);
	test_check (core_client_stopped (&client), "the connection was not dropped at the limit");
	test_check_unsigned_eq (test_wait_counter (&packet_errors, CORE_MAX_BAD_PACKETS, TEST_WAIT_SECONDS), CORE_MAX_BAD_PACKETS, NULL);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not remove the client"
	);

	test_check_unsigned_eq (CERVER_STATS_GET (cerver->stats->received_packets->n_bad_packets), CORE_MAX_BAD_PACKETS, NULL);

	core_client_end (&client);

	core_cerver_stop (&core);

}

static void test_core_bad_packets_poll (void) {

	test_core_bad_packets (CERVER_HANDLER_TYPE_POLL);

}

static void test_core_bad_packets_threads (void) {

	test_core_bad_packets (CERVER_HANDLER_TYPE_THREADS);

}

// a packet bigger than the maximum closes the connection right away, after its error
static void test_core_bad_packet_size (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_max_received_packet_size (cerver, CORE_MAX_PACKET_SIZE);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	char data[CORE_BIG_PACKET_SIZE];
	(void) memset (data, 0, CORE_BIG_PACKET_SIZE);

	Packet *packet = packet_create (PACKET_TYPE_APP, CORE_REQUEST_ECHO, data, CORE_BIG_PACKET_SIZE);
	test_check_ptr (packet);
	test_check_unsigned_eq (packet_generate (packet), 0, NULL);
	packet_set_network_values (packet, NULL, client.client, client.connection, NULL);
	test_check_unsigned_eq (packet_send (packet, 0, NULL, false), 0, NULL);
	packet_delete (packet);

	test_check (core_client_stopped (&client), "the connection was not closed");
	test_check_unsigned_eq (test_wait_counter (&packet_errors, 1, TEST_WAIT_SECONDS), 1, "the big packet did not get its error");
	test_check_unsigned_eq (app_packets, 0, NULL);

	core_client_end (&client);

	core_cerver_stop (&core);

}

// the bad packets of an on hold connection get errors, and it is dropped at the on hold limit
static void test_core_on_hold_bad_packets (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);
	cerver_set_on_hold_max_bad_packets (cerver, CORE_ON_HOLD_MAX_BAD_PACKETS);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	// an app packet can not be handled before the authentication
	CoreClient client = core_client_connect (port, NULL);
	core_client_send (&client, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_ON_HOLD_MAX_BAD_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ON_HOLD_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the on hold connection was not dropped"
	);

	test_check (core_client_stopped (&client), "the client did not see the drop");
	test_check_unsigned_eq (
		test_wait_counter (&packet_errors, CORE_ON_HOLD_MAX_BAD_PACKETS, TEST_WAIT_SECONDS),
		CORE_ON_HOLD_MAX_BAD_PACKETS, NULL
	);

	test_check_unsigned_eq (app_packets, 0, NULL);

	core_client_end (&client);

	core_cerver_stop (&core);

}

static unsigned int admin_packets = 0;

static void core_admin_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	test_counter_add (&admin_packets);

	if (packet->header.request_type == CORE_REQUEST_ECHO) core_echo (packet);

}

// an admin authenticates with the admin credentials, its packets reach the admin handlers,
// a broadcast reaches it, and its disconnection removes it
static void test_core_admin (void) {

	core_counters_reset ();
	__atomic_store_n (&admin_packets, 0, __ATOMIC_SEQ_CST);

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	admin_cerver_set_authenticate (cerver->admin, app_auth_method);

	Handler *admin_handler = handler_create (core_admin_handler);
	handler_set_direct_handle (admin_handler, true);
	admin_cerver_set_app_handlers (cerver->admin, admin_handler, NULL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient admin = core_admin_connect (port);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ADMIN_CONNECTED], 1, TEST_WAIT_SECONDS),
		1, "the admin did not connect"
	);

	test_check_unsigned_eq (admin_cerver_get_current_admins (cerver->admin), 1, NULL);

	core_client_send (&admin, PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);
	test_check_unsigned_eq (test_wait_counter (&admin_packets, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);
	test_check_unsigned_eq (test_wait_counter (&echoes, CORE_PACKETS, TEST_WAIT_SECONDS), CORE_PACKETS, NULL);

	// the admin packets do not reach the clients handlers
	test_check_unsigned_eq (app_packets, 0, NULL);

	Packet *broadcast = packet_generate_request (PACKET_TYPE_APP, CORE_REQUEST_ECHO, NULL, 0);
	test_check_ptr (broadcast);
	test_check_unsigned_eq (admin_cerver_broadcast_to_admins (cerver->admin, broadcast), 0, NULL);
	packet_delete (broadcast);

	test_check_unsigned_eq (test_wait_counter (&echoes, CORE_PACKETS + 1, TEST_WAIT_SECONDS), CORE_PACKETS + 1, "the broadcast did not arrive");

	core_client_end (&admin);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ADMIN_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the admin was not removed"
	);

	test_check_unsigned_eq (admin_cerver_get_current_admins (cerver->admin), 0, NULL);

	core_cerver_stop (&core);

	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_CONNECTED], 0, NULL);

}

// the bad packets of an admin get errors, and the admin is dropped at the admin cerver limit
static void test_core_admin_bad_packets (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	test_check_unsigned_eq (cerver_set_auth (cerver, CORE_AUTH_MAX_TRIES, app_auth_method), 0, NULL);

	test_check_unsigned_eq (cerver_set_admin_enable (cerver), 0, NULL);
	admin_cerver_set_authenticate (cerver->admin, app_auth_method);
	admin_cerver_set_bad_packets_limit (cerver->admin, CORE_ADMIN_MAX_BAD_PACKETS);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient admin = core_admin_connect (port);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ADMIN_CONNECTED], 1, TEST_WAIT_SECONDS),
		1, "the admin did not connect"
	);

	core_client_send_unknown (&admin, CORE_ADMIN_MAX_BAD_PACKETS);

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_ADMIN_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the admin was not dropped at the limit"
	);

	test_check (core_client_stopped (&admin), "the admin did not see the drop");
	test_check_unsigned_eq (
		test_wait_counter (&packet_errors, CORE_ADMIN_MAX_BAD_PACKETS, TEST_WAIT_SECONDS),
		CORE_ADMIN_MAX_BAD_PACKETS, NULL
	);

	core_client_end (&admin);

	core_cerver_stop (&core);

}

// an idle client is closed by the cerver, an active one is kept
static void test_core_inactive_clients (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_inactive_clients (cerver, CORE_INACTIVE_SECONDS, CORE_INACTIVE_INTERVAL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);

	test_check (core_client_stopped (&client), "the inactive client was not closed");

	test_check_unsigned_eq (
		test_wait_counter (&events[CERVER_EVENT_CLIENT_DROPPED], 1, TEST_WAIT_SECONDS),
		1, "the cerver did not drop the inactive client"
	);

	core_client_end (&client);

	core_cerver_stop (&core);

}

// the update threads tick while the cerver runs and stop with it
static void test_core_update (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	cerver_set_update (cerver, core_update, NULL, NULL, CORE_UPDATE_FPS);
	cerver_set_update_interval (cerver, core_update_interval, NULL, NULL, CORE_UPDATE_INTERVAL);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	test_check (test_wait_counter (&update_ticks, CORE_UPDATE_TICKS, TEST_WAIT_SECONDS) >= CORE_UPDATE_TICKS, NULL);
	test_check (test_wait_counter (&interval_ticks, 1, TEST_WAIT_SECONDS) >= 1, NULL);

	core_cerver_stop (&core);

	// the threads were joined, so the counters do not change anymore
	const unsigned int ticks = __atomic_load_n (&update_ticks, __ATOMIC_SEQ_CST);
	test_check_unsigned_eq (test_wait_counter (&update_ticks, ticks + 1, 1), ticks, NULL);

}

// the teardown drains and closes the connected clients, that see the remote close
static void test_core_teardown_connected (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	CoreCerver core;
	core_cerver_start (&core, core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS), port);

	CoreClient clients[CORE_CLIENTS];
	for (unsigned int i = 0; i < CORE_CLIENTS; i++) {
		clients[i] = core_client_connect (port, NULL);
		core_client_send (&clients[i], PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);
	}

	test_check_unsigned_eq (
		test_wait_counter (&echoes, CORE_CLIENTS * CORE_PACKETS, TEST_WAIT_SECONDS),
		CORE_CLIENTS * CORE_PACKETS, NULL
	);

	core_cerver_stop (&core);

	for (unsigned int i = 0; i < CORE_CLIENTS; i++) {
		test_check (core_client_stopped (&clients[i]), "a client did not see the cerver close");
		core_client_end (&clients[i]);
	}

	test_check_unsigned_eq (events[CERVER_EVENT_CLIENT_CONNECTED], CORE_CLIENTS, NULL);

}

// the teardown waits for an event action that runs in its own thread
static void test_core_event_thread_teardown (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);

	test_check_unsigned_eq (
		cerver_event_register (
			cerver, CERVER_EVENT_CLIENT_CONNECTED,
			core_slow_event, NULL, NULL,
			true, false
		), 0, NULL
	);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);
	core_client_end (&client);

	core_cerver_stop (&core);

	test_check_unsigned_eq (
		__atomic_load_n (&slow_events, __ATOMIC_SEQ_CST), 1,
		"the teardown did not wait for the event thread"
	);

}

// the detachable connection threads are drained before the clients are released
static void test_core_detachable_threads (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_handle_detachable_threads (cerver, true);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient clients[CORE_CLIENTS];
	for (unsigned int i = 0; i < CORE_CLIENTS; i++) {
		clients[i] = core_client_connect (port, NULL);
		core_client_send (&clients[i], PACKET_TYPE_APP, CORE_REQUEST_ECHO, 0, CORE_PACKETS);
	}

	test_check_unsigned_eq (
		test_wait_counter (&echoes, CORE_CLIENTS * CORE_PACKETS, TEST_WAIT_SECONDS),
		CORE_CLIENTS * CORE_PACKETS, NULL
	);

	core_cerver_stop (&core);

	for (unsigned int i = 0; i < CORE_CLIENTS; i++) {
		test_check (core_client_stopped (&clients[i]), "a client did not see the cerver close");
		core_client_end (&clients[i]);
	}

}

// an update with a low fps stops with the cerver, it does not wait for its frame
static void test_core_slow_update_teardown (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);
	cerver_set_update (cerver, core_update, NULL, NULL, CORE_SLOW_UPDATE_FPS);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	test_check (test_wait_counter (&update_ticks, 1, TEST_WAIT_SECONDS) >= 1, NULL);

	core_cerver_stop_quickly (&core);

}

// a connection that is still working when the drain times out finishes in the grace,
// so the cerver is released (LSan checks it)
static void test_core_drain_grace (void) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS);
	cerver_set_drain_timeout (cerver, CORE_DRAIN_TIMEOUT);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_connect (port, NULL);
	core_client_send (&client, PACKET_TYPE_APP, CORE_REQUEST_SLOW, 0, 1);

	test_check_unsigned_eq (test_wait_counter (&slow_started, 1, TEST_WAIT_SECONDS), 1, NULL);

	core_cerver_stop (&core);

	test_check_unsigned_eq (
		__atomic_load_n (&slow_finished, __ATOMIC_SEQ_CST), 1,
		"the teardown did not wait for the connection in the grace"
	);

	core_client_end (&client);

}

// a stop requested before cerver_start () cancels it, the cerver never listens
static void test_core_stop_before_start (const CerverHandlerType handler_type) {

	core_counters_reset ();

	const u16 port = core_port++;
	Cerver *cerver = core_cerver_create (port, handler_type);

	test_check_unsigned_eq (cerver_shutdown (cerver), 0, NULL);

	CoreCerver core;
	core_cerver_launch (&core, cerver, port);
	core_cerver_join (&core);

	test_check_unsigned_eq (events[CERVER_EVENT_STARTED], 0, "a stopped cerver started");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

static void test_core_stop_before_start_poll (void) {

	test_core_stop_before_start (CERVER_HANDLER_TYPE_POLL);

}

static void test_core_stop_before_start_threads (void) {

	test_core_stop_before_start (CERVER_HANDLER_TYPE_THREADS);

}

// a stop that races with cerver_start () always ends with the cerver stopped,
// whether it arrives before the running flag, before listen () or inside accept (),
// each round waits a bit longer before the stop to land in a later moment of the start
static void test_core_stop_while_starting (void) {

	for (unsigned int round = 0; round < CORE_START_STOP_ROUNDS; round++) {
		core_counters_reset ();

		const u16 port = core_port++;
		const struct timespec wait = { 0, (long) round * CORE_START_STOP_STEP_NANOSECONDS };

		CoreCerver core;
		core_cerver_launch (&core, core_cerver_create (port, CERVER_HANDLER_TYPE_THREADS), port);

		(void) nanosleep (&wait, NULL);

		test_check_unsigned_eq (cerver_shutdown (core.cerver), 0, NULL);

		core_cerver_join (&core);

		test_check_unsigned_eq (cerver_teardown (core.cerver), 0, NULL);
	}

}

#pragma endregion

#pragma region address

// a cerver that binds bind_address (NULL for every address)
static Cerver *core_address_cerver_create (
	const u16 port, const char *bind_address, const bool ipv6_only
) {

	Cerver *cerver = core_cerver_create (port, CERVER_HANDLER_TYPE_POLL);

	if (bind_address) {
		test_check_unsigned_eq (cerver_set_bind_address (cerver, bind_address), 0, NULL);
	}

	cerver_set_ipv6_only (cerver, ipv6_only);

	return cerver;

}

// the client asks for the ip that the cerver sees
static void core_address_expect (CoreClient *client, const char *expected_ip) {

	core_client_send (client, PACKET_TYPE_APP, CORE_REQUEST_ADDRESS, 0, 1);
	test_check_unsigned_eq (test_wait_counter (&address_requests, 1, TEST_WAIT_SECONDS), 1, NULL);
	test_check_str_eq (client_ip, expected_ip, NULL);

}

// the client connects to client_address preferring a family, and the cerver sees it as expected_ip
static void core_address_check_preferring (
	const char *bind_address, const bool ipv6_only,
	const char *client_address, const bool prefer_ipv6,
	const char *expected_ip
) {

	core_counters_reset ();

	const u16 port = core_port++;
	CoreCerver core;
	core_cerver_start (&core, core_address_cerver_create (port, bind_address, ipv6_only), port);

	CoreClient client = core_client_create_at (client_address, port, NULL, false);
	client.connection->use_ipv6 = prefer_ipv6;
	connection_set_max_sleep (client.connection, CORE_CONNECT_SINGLE_ATTEMPT);

	client = core_client_start (client);

	core_address_expect (&client, expected_ip);

	core_client_end (&client);

	core_cerver_stop (&core);

}

// the client connects to client_address and the cerver sees it as expected_ip
static void core_address_check (
	const char *bind_address, const bool ipv6_only,
	const char *client_address, const char *expected_ip
) {

	core_address_check_preferring (bind_address, ipv6_only, client_address, false, expected_ip);

}

// a cerver that only listens in the loopback of one family
static void test_core_address_loopback (void) {

	core_address_check (CORE_IPV4_LOOPBACK, false, CORE_IPV4_LOOPBACK, CORE_IPV4_LOOPBACK);
	core_address_check (CORE_IPV6_LOOPBACK, false, CORE_IPV6_LOOPBACK, CORE_IPV6_LOOPBACK);

}

// a dual stack cerver sees an IPv4 client as IPv4, not as ::ffff:127.0.0.1
static void test_core_address_dual_stack (void) {

	core_address_check (CORE_IPV6_ANY, false, CORE_IPV4_LOOPBACK, CORE_IPV4_LOOPBACK);
	core_address_check (CORE_IPV6_ANY, false, CORE_IPV6_LOOPBACK, CORE_IPV6_LOOPBACK);

}

// the client resolves a host name
static void test_core_address_host_name (void) {

	core_address_check (CORE_IPV4_LOOPBACK, false, CORE_LOCALHOST, CORE_IPV4_LOOPBACK);

}

// a client fails to connect to a cerver that does not listen in its family
static void core_address_check_refused (
	const char *bind_address, const bool ipv6_only, const char *client_address
) {

	core_counters_reset ();

	const u16 port = core_port++;
	CoreCerver core;
	core_cerver_start (&core, core_address_cerver_create (port, bind_address, ipv6_only), port);

	CoreClient client = core_client_create_at (client_address, port, NULL, false);
	connection_set_max_sleep (client.connection, CORE_CONNECT_SINGLE_ATTEMPT);

	test_check_unsigned_ne (client_connect (client.client, client.connection), 0);
	test_check_unsigned_eq (client_teardown (client.client), 0, NULL);

	core_cerver_stop (&core);

}

static void test_core_address_refused (void) {

	// IPV6_V6ONLY does not take IPv4 connections
	core_address_check_refused (CORE_IPV6_ANY, true, CORE_IPV4_LOOPBACK);

	// a cerver in the IPv4 loopback does not take IPv6 connections
	core_address_check_refused (CORE_IPV4_LOOPBACK, false, CORE_IPV6_LOOPBACK);

}

static void test_core_address_invalid (void) {

	Cerver *cerver = core_cerver_create (core_port++, CERVER_HANDLER_TYPE_POLL);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, CORE_INVALID_ADDRESS), 1, NULL);
	test_check_str_empty (cerver->bind_address);
	test_check_false (cerver->use_ipv6);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, CORE_IPV6_LOOPBACK), 0, NULL);
	test_check_true (cerver->use_ipv6);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, NULL), 0, NULL);
	test_check_str_empty (cerver->bind_address);

	// a link local address with its zone, and a zone without an interface
	test_check_unsigned_eq (cerver_set_bind_address (cerver, CORE_LINK_LOCAL_ZONE), 0, NULL);
	test_check_str_eq (cerver->bind_address, CORE_LINK_LOCAL_ZONE, NULL);
	test_check_true (cerver->use_ipv6);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, CORE_UNKNOWN_ZONE), 1, NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// localhost resolves to 127.0.0.1 and ::1, the cerver only listens in one of them:
// the client is refused by the first address and connects to the other one,
// before it only tried the first address of its preferred family
static void test_core_address_fallback (void) {

	core_address_check_preferring (CORE_IPV6_LOOPBACK, false, CORE_LOCALHOST, false, CORE_IPV6_LOOPBACK);
	core_address_check_preferring (CORE_IPV4_LOOPBACK, false, CORE_LOCALHOST, true, CORE_IPV4_LOOPBACK);

}

// a listener whose accept queue is full drops new connections without an answer,
// so a connect () to it never ends by itself
static int core_blackhole_create (const char *ip, const u16 port, int *queued) {

	struct sockaddr_storage address;
	test_check_unsigned_eq (network_parse_address (ip, port, &address), 0, NULL);

	const int listener = socket (address.ss_family, SOCK_STREAM, 0);
	test_check (listener >= 0, NULL);
	test_check_int_eq (sock_set_reusable (listener), 0, NULL);
	test_check_int_eq (bind (listener, (const struct sockaddr *) &address, sock_address_size (&address)), 0, NULL);
	test_check_int_eq (listen (listener, CORE_BLACKHOLE_BACKLOG), 0, NULL);

	*queued = socket (address.ss_family, SOCK_STREAM, 0);
	test_check (*queued >= 0, NULL);
	test_check_int_eq (connect (*queued, (const struct sockaddr *) &address, sock_address_size (&address)), 0, NULL);

	return listener;

}

// the first address never answers: the client waits its connect timeout and tries the next one,
// before the connect () blocked until the kernel gave up on the first address
static void test_core_address_connect_timeout (void) {

	core_counters_reset ();

	const u16 port = core_port++;

	int queued = -1;
	const int blackhole = core_blackhole_create (CORE_IPV4_LOOPBACK, port, &queued);

	CoreCerver core;
	core_cerver_start (&core, core_address_cerver_create (port, CORE_IPV6_LOOPBACK, false), port);

	CoreClient client = core_client_create_at (CORE_LOCALHOST, port, NULL, false);
	connection_set_connect_timeout (client.connection, CORE_CONNECT_TIMEOUT);
	test_check_unsigned_eq (client.connection->connect_timeout, CORE_CONNECT_TIMEOUT, NULL);
	connection_set_max_sleep (client.connection, CORE_CONNECT_SINGLE_ATTEMPT);

	const double start = test_wait_now ();
	client = core_client_start (client);
	const double elapsed = test_wait_now () - start;

	test_check (elapsed >= CORE_CONNECT_TIMEOUT_MIN_SECONDS, "the client did not wait for the first address");
	test_check (elapsed < CORE_CONNECT_TIMEOUT_MAX_SECONDS, "the client waited too long for the first address");

	core_address_expect (&client, CORE_IPV6_LOOPBACK);

	core_client_end (&client);

	core_cerver_stop (&core);

	(void) close (queued);
	(void) close (blackhole);

}

// cerver_set_network_values () after cerver_set_bind_address () changed use_ipv6,
// and the IPv4 socket could not bind the IPv6 address
static void test_core_address_network_values (void) {

	core_counters_reset ();

	const u16 port = core_port++;

	Cerver *cerver = core_address_cerver_create (port, CORE_IPV6_LOOPBACK, false);
	cerver_set_network_values (cerver, port, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE);

	CoreCerver core;
	core_cerver_start (&core, cerver, port);

	CoreClient client = core_client_start (core_client_create_at (CORE_IPV6_LOOPBACK, port, NULL, false));

	core_address_expect (&client, CORE_IPV6_LOOPBACK);

	core_client_end (&client);

	core_cerver_stop (&core);

}

#pragma endregion

int main (int argc, char **argv) {

	test_output_init ();

	(void) argc;
	(void) argv;

	cerver_init ();

	(void) printf ("Testing CERVER core...\n");

	test_core_dispatch_poll ();
	test_core_dispatch_threads ();
	test_core_without_handlers ();
	test_core_multiple_handlers ();
	test_core_handlers_teardown_poll ();
	test_core_handlers_teardown_threads ();
	test_core_auth_success ();
	test_core_auth_failed ();
	test_core_auth_retry ();
	test_core_on_hold_timeout ();
	test_core_on_hold_without_timeout ();
	test_core_drop_request_poll ();
	test_core_drop_request_threads ();
	test_core_admin ();
	test_core_admin_bad_packets ();
	test_core_check_packets ();
	test_core_bad_packets_poll ();
	test_core_bad_packets_threads ();
	test_core_bad_packet_size ();
	test_core_on_hold_bad_packets ();
	test_core_inactive_clients ();
	test_core_update ();
	test_core_teardown_connected ();
	test_core_event_thread_teardown ();
	test_core_detachable_threads ();
	test_core_slow_update_teardown ();
	test_core_drain_grace ();
	test_core_stop_before_start_poll ();
	test_core_stop_before_start_threads ();
	test_core_stop_while_starting ();
	test_core_address_loopback ();
	test_core_address_dual_stack ();
	test_core_address_host_name ();
	test_core_address_refused ();
	test_core_address_invalid ();
	test_core_address_fallback ();
	test_core_address_connect_timeout ();
	test_core_address_network_values ();

	(void) printf ("\nDone with CERVER core tests!\n\n");

	cerver_end ();

	return 0;

}
