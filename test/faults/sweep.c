#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <malloc.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/connection/connection.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/packets/packets.h>

#include "../test.h"
#include "faults.h"
#include "sweep.h"

// the paths of the core with each one of their allocations failing, one in each run:
// a path that fails returns its error and leaves alive only what a run without a failure leaves

#define SWEEP_ADDRESS					"127.0.0.1"
#define SWEEP_CERVER_NAME				"sweep-cerver"

// the cervers of the sweep listen in this port, one at a time
#define SWEEP_CERVER_PORT				7801

#define SWEEP_THREADS					2

// the runs without a failure before a sweep: the first ones fill what the process keeps
#define SWEEP_WARM_RUNS					2

// the most runs that a sweep makes, more than the allocations of any of its paths
#define SWEEP_MAX_RUNS					(1 << 11)

#define SWEEP_WAIT_STEP_NANOSECONDS		(1000 * 1000)

#define SWEEP_PACKET_DATA				"sweep"

typedef struct SweepPath {

	const char *name;
	FaultsSweepWork work;
	void *data;

	// what a run without a failure leaves alive, like the caches of the thread
	unsigned int baseline;

	// the function that is swept and the runs with a failure of it
	FaultsFunction function;
	unsigned int runs;

} SweepPath;

#pragma region runs

// a run of a path whose allocations are watched: returns the ones that are still alive
static unsigned int sweep_run_watched (const SweepPath *path) {

	faults_watch_start (FAULTS_ANY_SIZE);
	path->work (path->data);
	faults_watch_stop ();

	test_check (
		faults_watch_allocated () <= FAULTS_WATCH_CAPACITY,
		"the path made more allocations than the watch keeps"
	);

	return faults_watch_alive ();

}

// the same failure in a run of its own: a cache that the first one filled (like the ones of OpenSSL)
// is not filled again, a leak leaves its allocations again
static unsigned int sweep_run_again (const SweepPath *path) {

	faults_arm_after (path->function, true, FAULTS_ANY_SIZE, path->runs);
	const unsigned int alive = sweep_run_watched (path);
	faults_disarm (path->function);

	return alive;

}

// a run with a failure leaves alive at most what a run without one leaves
static void sweep_run_failing (void *path_ptr) {

	SweepPath *path = (SweepPath *) path_ptr;

	unsigned int alive = sweep_run_watched (path);
	if (alive > path->baseline) alive = sweep_run_again (path);

	if (alive > path->baseline) {
		(void) fprintf (
			stderr, "[SWEEP] %s leaked %u allocations in the run %u (a run without a failure leaves %u), "
			"the first one of %zu bytes\n",
			path->name, alive - path->baseline, path->runs, path->baseline,
			malloc_usable_size (faults_watch_alive_first ())
		);
	}

	path->runs += 1;

	test_check (alive <= path->baseline, "a run with a failed allocation leaked");

}

static void sweep_path_warm (SweepPath *path) {

	for (unsigned int i = 0; i < SWEEP_WARM_RUNS; i++) path->work (path->data);

	path->baseline = sweep_run_watched (path);

}

// each call of the function fails in its own run, after the calls that the run leaves to pass
static unsigned int sweep_function (SweepPath *path, const FaultsFunction function) {

	unsigned int runs = 0;
	path->function = function;
	path->runs = 0;

	bool failed = true;
	while (failed && (runs < SWEEP_MAX_RUNS)) {
		const unsigned int triggered = faults_triggered (function);

		faults_arm_after (function, true, FAULTS_ANY_SIZE, runs);
		sweep_run_failing (path);
		faults_disarm (function);

		failed = faults_triggered (function) != triggered;
		if (failed) runs += 1;
	}

	test_check (runs < SWEEP_MAX_RUNS, "the sweep did not end");

	return runs;

}

// the path with each of its calls to malloc (), calloc (), realloc () and pthread_create () failing
static void sweep_path (SweepPath *path) {

	sweep_path_warm (path);

	path->function = FAULTS_FUNCTION_MALLOC;
	path->runs = 0;
	const unsigned int mallocs = faults_sweep (sweep_run_failing, path);
	const unsigned int callocs = sweep_function (path, FAULTS_FUNCTION_CALLOC);
	const unsigned int reallocs = sweep_function (path, FAULTS_FUNCTION_REALLOC);
	const unsigned int threads = sweep_function (path, FAULTS_FUNCTION_THREAD);

	(void) printf (
		"\t%s: %u malloc, %u calloc, %u realloc and %u threads failed\n",
		path->name, mallocs, callocs, reallocs, threads
	);

	test_check (mallocs > 0, "the path made no allocation");

}

void faults_sweep_path (const char *name, FaultsSweepWork work, void *data) {

	SweepPath path = { name, work, data, 0, FAULTS_FUNCTION_MALLOC, 0 };
	sweep_path (&path);

}

#pragma endregion

#pragma region cerver

static Cerver *sweep_cerver_create (void) {

	Cerver *cerver = cerver_create (
		CERVER_TYPE_CUSTOM, SWEEP_CERVER_NAME,
		SWEEP_CERVER_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	if (cerver) {
		cerver_set_reusable_address_flags (cerver, true);
		cerver_set_thpool_n_threads (cerver, SWEEP_THREADS);
		cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_THREADS);
	}

	return cerver;

}

// a cerver that is created and released without starting
static void sweep_cerver_created (void *data) {

	(void) data;

	Cerver *cerver = sweep_cerver_create ();
	if (cerver) test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

// the thread that stops the cerver of each run once it runs: it lives for the whole sweep,
// so the only threads that a run creates are the ones of its cerver
typedef struct SweepStopper {

	pthread_t thread;
	pthread_mutex_t mutex;

	// the cerver that the run starts, NULL between the runs
	Cerver *cerver;

	// read and written atomically
	bool ended;

} SweepStopper;

static SweepStopper sweep_stopper;

static void sweep_wait_step (void) {

	const struct timespec step = { 0, SWEEP_WAIT_STEP_NANOSECONDS };
	(void) nanosleep (&step, NULL);

}

// the cerver of the run is stopped once it runs, with the mutex that its run takes to release it
static void sweep_stopper_check (void) {

	(void) pthread_mutex_lock (&sweep_stopper.mutex);

	if (sweep_stopper.cerver && cerver_is_running (sweep_stopper.cerver)) {
		(void) cerver_shutdown (sweep_stopper.cerver);
		sweep_stopper.cerver = NULL;
	}

	(void) pthread_mutex_unlock (&sweep_stopper.mutex);

}

static void *sweep_stopper_work (void *data) {

	(void) data;

	while (!__atomic_load_n (&sweep_stopper.ended, __ATOMIC_ACQUIRE)) {
		sweep_stopper_check ();
		sweep_wait_step ();
	}

	return NULL;

}

static void sweep_stopper_set (Cerver *cerver) {

	(void) pthread_mutex_lock (&sweep_stopper.mutex);
	sweep_stopper.cerver = cerver;
	(void) pthread_mutex_unlock (&sweep_stopper.mutex);

}

static void sweep_stopper_start (void) {

	sweep_stopper.cerver = NULL;
	sweep_stopper.ended = false;

	test_check_int_eq (pthread_mutex_init (&sweep_stopper.mutex, NULL), 0, NULL);
	test_check_int_eq (pthread_create (&sweep_stopper.thread, NULL, sweep_stopper_work, NULL), 0, NULL);

}

static void sweep_stopper_end (void) {

	__atomic_store_n (&sweep_stopper.ended, true, __ATOMIC_RELEASE);

	test_check_int_eq (pthread_join (sweep_stopper.thread, NULL), 0, NULL);
	(void) pthread_mutex_destroy (&sweep_stopper.mutex);

}

// a cerver that starts in this thread, so each of the allocations of its start fails in a run
static void sweep_cerver_started (void *data) {

	(void) data;

	Cerver *cerver = sweep_cerver_create ();
	if (!cerver) return;

	sweep_stopper_set (cerver);
	(void) cerver_start (cerver);

	// a start that failed is not stopped, and the stopper does not see the cerver that is released
	sweep_stopper_set (NULL);

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

}

#pragma endregion

#pragma region client

// a socket that listens in a free port of the address: the connect completes in its backlog
static int sweep_listen (u16 *port) {

	const int sock_fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (sock_fd >= 0, "socket () failed");

	struct sockaddr_in address;
	(void) memset (&address, 0, sizeof (struct sockaddr_in));
	address.sin_family = AF_INET;
	(void) inet_pton (AF_INET, SWEEP_ADDRESS, &address.sin_addr);

	test_check_int_eq (bind (sock_fd, (struct sockaddr *) &address, sizeof (struct sockaddr_in)), 0, NULL);
	test_check_int_eq (listen (sock_fd, SOMAXCONN), 0, NULL);

	socklen_t length = sizeof (struct sockaddr_in);
	test_check_int_eq (getsockname (sock_fd, (struct sockaddr *) &address, &length), 0, NULL);
	*port = ntohs (address.sin_port);

	return sock_fd;

}

// a client that connects and ends its connection, without its update thread
static void sweep_client_connected (void *port_ptr) {

	const u16 port = *((const u16 *) port_ptr);

	Client *client = client_create ();
	if (!client) return;

	Connection *connection = client_connection_create (
		client, SWEEP_ADDRESS, port, PROTOCOL_TCP, false
	);

	if (connection && !client_connect (client, connection)) {
		(void) client_connection_end (client, connection);
	}

	test_check_unsigned_eq (client_teardown (client), 0, NULL);

}

#pragma endregion

#pragma region packets

// a request and an application packet that are created, generated and released
static void sweep_packets (void *data) {

	(void) data;

	Packet *request = packet_create_request (PACKET_TYPE_APP, 0);
	if (request) packet_delete (request);

	Packet *packet = packet_create (
		PACKET_TYPE_APP, 0, SWEEP_PACKET_DATA, sizeof (SWEEP_PACKET_DATA)
	);

	if (packet) {
		(void) packet_generate (packet);
		packet_delete (packet);
	}

}

#pragma endregion

void faults_tests_sweep (void) {

	(void) printf ("Testing FAULTS sweep...\n");

	faults_sweep_path ("cerver created", sweep_cerver_created, NULL);

	sweep_stopper_start ();
	faults_sweep_path ("cerver started", sweep_cerver_started, NULL);
	sweep_stopper_end ();

	u16 port = 0;
	const int listener = sweep_listen (&port);

	faults_sweep_path ("client connected", sweep_client_connected, &port);

	(void) close (listener);

	faults_sweep_path ("packets", sweep_packets, NULL);

	(void) printf ("Done!\n");

}
