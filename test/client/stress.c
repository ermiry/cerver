#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <pthread.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/handler/handler.h>
#include <cerver/packets/packets.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../test.h"

// #275: many connections to the stress cerver at the same time,
// each one sends its requests one after the other and checks every answer,
// that has to bring back the same body, then it prints the packets per second and the latencies
// usage: stress [connections] [requests] [body size]

#define STRESS_PORT						7600
#define STRESS_MAX_SLEEP				30
#define STRESS_REQUEST_ECHO				1

#define STRESS_CONNECTIONS_ARGUMENT		1
#define STRESS_REQUESTS_ARGUMENT		2
#define STRESS_BODY_SIZE_ARGUMENT		3
#define STRESS_NUMBER_BASE				10

#define STRESS_DEFAULT_CONNECTIONS		64
#define STRESS_DEFAULT_REQUESTS			1024
#define STRESS_DEFAULT_BODY_SIZE		64
#define STRESS_MAX_BODY_SIZE			(1 << 16)

#define STRESS_RESPONSE_SECONDS			16
#define STRESS_BYTE_MASK				0xFF

#define STRESS_NANOSECONDS_PER_SECOND		1000000000LL
#define STRESS_NANOSECONDS_PER_MICROSECOND	1000LL

#define STRESS_PERCENT					100
#define STRESS_MEDIAN					50
#define STRESS_TAIL						99

// the start of the body of every request, the rest of the body is a pattern of them
typedef struct StressBody {

	u32 connection;
	u32 sequence;
	u64 sent;			// nanoseconds of CLOCK_MONOTONIC

} StressBody;

typedef struct StressConnection {

	unsigned int index;

	Client *client;
	Connection *connection;

	pthread_t thread;

	// the answer of the request that is waiting
	pthread_mutex_t mutex;
	pthread_cond_t answered;
	bool has_answer;

	u32 sequence;
	unsigned int bad_answers;

	u64 *latencies;		// microseconds of each request
	unsigned int completed;

} StressConnection;

static unsigned int connections_count = STRESS_DEFAULT_CONNECTIONS;
static unsigned int requests_count = STRESS_DEFAULT_REQUESTS;
static size_t body_size = STRESS_DEFAULT_BODY_SIZE;

static u64 stress_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((u64) now.tv_sec * STRESS_NANOSECONDS_PER_SECOND) + (u64) now.tv_nsec;

}

static unsigned int stress_argument (int argc, char **argv, const int index, const unsigned int fallback) {

	return (argc > index) ? (unsigned int) strtoul (argv[index], NULL, STRESS_NUMBER_BASE) : fallback;

}

static char stress_pattern (const StressBody *body, const size_t position) {

	return (char) ((body->connection + body->sequence + position) & STRESS_BYTE_MASK);

}

static void stress_body_fill (char *data, const unsigned int connection, const u32 sequence) {

	StressBody *body = (StressBody *) data;
	body->connection = connection;
	body->sequence = sequence;
	body->sent = stress_now ();

	for (size_t position = sizeof (StressBody); position < body_size; position++) {
		data[position] = stress_pattern (body, position);
	}

}

// the answer has the body of the request that is waiting
static bool stress_body_check (
	const StressConnection *stress, const char *data, const size_t size
) {

	bool valid = data && (size == body_size);

	const StressBody *body = (const StressBody *) data;
	valid = valid && (body->connection == stress->index) && (body->sequence == stress->sequence);

	for (size_t position = sizeof (StressBody); valid && (position < body_size); position++) {
		valid = (data[position] == stress_pattern (body, position));
	}

	return valid;

}

static void stress_client_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	StressConnection *stress = (StressConnection *) client_get_data (packet->client);

	(void) pthread_mutex_lock (&stress->mutex);

	if (stress_body_check (stress, (const char *) packet->data, packet->data_size)) {
		const StressBody *body = (const StressBody *) packet->data;
		stress->latencies[stress->completed] = (stress_now () - body->sent) / STRESS_NANOSECONDS_PER_MICROSECOND;
		stress->completed += 1;
	}

	else {
		stress->bad_answers += 1;
	}

	stress->has_answer = true;
	(void) pthread_cond_signal (&stress->answered);

	(void) pthread_mutex_unlock (&stress->mutex);

}

// the connections live in an array that main () releases
static void stress_data_keep (void *data) {

	(void) data;

}

static void stress_connect (StressConnection *stress, const unsigned int index) {

	stress->index = index;
	stress->has_answer = false;
	stress->sequence = 0;
	stress->bad_answers = 0;
	stress->completed = 0;

	stress->latencies = (u64 *) calloc (requests_count, sizeof (u64));
	test_check_ptr (stress->latencies);

	test_check_int_eq (pthread_mutex_init (&stress->mutex, NULL), 0, NULL);
	test_check_int_eq (pthread_cond_init (&stress->answered, NULL), 0, NULL);

	stress->client = client_create ();
	test_check_ptr (stress->client);

	client_set_data (stress->client, stress, stress_data_keep);
	client_set_max_received_packet_size (stress->client, PACKET_HEADER_SIZE + STRESS_MAX_BODY_SIZE);

	Handler *app_handler = handler_create (stress_client_handler);
	handler_set_direct_handle (app_handler, true);
	client_set_app_handlers (stress->client, app_handler, NULL);

	stress->connection = client_connection_create (
		stress->client, test_address (), STRESS_PORT, PROTOCOL_TCP, false
	);

	test_check_ptr (stress->connection);
	connection_set_max_sleep (stress->connection, STRESS_MAX_SLEEP);

	test_check_int_eq (
		client_connect_and_start (stress->client, stress->connection), 0,
		"Failed to connect to the stress cerver!"
	);

}

// waits for the answer of the request that was sent
// returns false if it did not arrive in time
static bool stress_wait_answer (StressConnection *stress) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += STRESS_RESPONSE_SECONDS;

	int waited = 0;

	(void) pthread_mutex_lock (&stress->mutex);

	while (!stress->has_answer && (waited != ETIMEDOUT)) {
		waited = pthread_cond_timedwait (&stress->answered, &stress->mutex, &deadline);
	}

	const bool answered = stress->has_answer;
	stress->has_answer = false;

	(void) pthread_mutex_unlock (&stress->mutex);

	return answered;

}

static bool stress_send (StressConnection *stress, char *data) {

	stress_body_fill (data, stress->index, stress->sequence);

	Packet *request = packet_generate_request (PACKET_TYPE_APP, STRESS_REQUEST_ECHO, data, body_size);
	test_check_ptr (request);

	packet_set_network_values (request, NULL, stress->client, stress->connection, NULL);
	const bool sent = !packet_send (request, 0, NULL, false);

	packet_delete (request);

	return sent;

}

static void *stress_connection_thread (void *stress_ptr) {

	StressConnection *stress = (StressConnection *) stress_ptr;

	char *data = (char *) malloc (body_size);
	test_check_ptr (data);

	bool working = true;
	for (u32 sequence = 0; working && (sequence < requests_count); sequence++) {
		(void) pthread_mutex_lock (&stress->mutex);
		stress->sequence = sequence;
		(void) pthread_mutex_unlock (&stress->mutex);

		working = stress_send (stress, data) && stress_wait_answer (stress);
	}

	free (data);

	return NULL;

}

static int stress_latency_compare (const void *first_ptr, const void *second_ptr) {

	const u64 first = *((const u64 *) first_ptr);
	const u64 second = *((const u64 *) second_ptr);

	return (first > second) - (first < second);

}

static u64 stress_percentile (const u64 *latencies, const size_t count, const unsigned int percentile) {

	return count ? latencies[((count - 1) * percentile) / STRESS_PERCENT] : 0;

}

// prints the results of every connection
// returns true if every request was answered with its body
static bool stress_report (StressConnection *connections, const double seconds) {

	u64 *latencies = (u64 *) calloc ((size_t) connections_count * requests_count, sizeof (u64));
	test_check_ptr (latencies);

	size_t completed = 0;
	unsigned int bad_answers = 0;
	for (unsigned int index = 0; index < connections_count; index++) {
		(void) memcpy (latencies + completed, connections[index].latencies, sizeof (u64) * connections[index].completed);
		completed += connections[index].completed;
		bad_answers += connections[index].bad_answers;
	}

	qsort (latencies, completed, sizeof (u64), stress_latency_compare);

	const size_t expected = (size_t) connections_count * requests_count;

	(void) printf (
		"stress: %u connections, %u requests of %zu bytes: %zu / %zu answered, %u bad answers\n",
		connections_count, requests_count, body_size, completed, expected, bad_answers
	);

	(void) printf (
		"stress: %.0f packets per second, p%u %lu us, p%u %lu us, max %lu us\n",
		(seconds > 0) ? ((double) completed / seconds) : 0,
		(unsigned int) STRESS_MEDIAN, stress_percentile (latencies, completed, STRESS_MEDIAN),
		(unsigned int) STRESS_TAIL, stress_percentile (latencies, completed, STRESS_TAIL),
		completed ? latencies[completed - 1] : 0
	);

	free (latencies);

	return (completed == expected) && !bad_answers;

}

static void stress_end (StressConnection *stress) {

	(void) client_connection_end (stress->client, stress->connection);
	(void) client_teardown (stress->client);

	(void) pthread_cond_destroy (&stress->answered);
	(void) pthread_mutex_destroy (&stress->mutex);

	free (stress->latencies);

}

int main (int argc, char **argv) {

	(void) signal (SIGPIPE, SIG_IGN);

	test_output_init ();

	cerver_log_set_quiet (true);

	connections_count = stress_argument (argc, argv, STRESS_CONNECTIONS_ARGUMENT, STRESS_DEFAULT_CONNECTIONS);
	requests_count = stress_argument (argc, argv, STRESS_REQUESTS_ARGUMENT, STRESS_DEFAULT_REQUESTS);
	body_size = stress_argument (argc, argv, STRESS_BODY_SIZE_ARGUMENT, STRESS_DEFAULT_BODY_SIZE);

	test_check (connections_count && requests_count, "the connections and the requests can not be 0");
	test_check (
		(body_size >= sizeof (StressBody)) && (body_size <= STRESS_MAX_BODY_SIZE),
		"the body does not fit the stress values"
	);

	StressConnection *connections = (StressConnection *) calloc (connections_count, sizeof (StressConnection));
	test_check_ptr (connections);

	// every connection is open before the requests start
	for (unsigned int index = 0; index < connections_count; index++) {
		stress_connect (&connections[index], index);
	}

	const double start = test_wait_now ();

	for (unsigned int index = 0; index < connections_count; index++) {
		test_check_int_eq (
			pthread_create (&connections[index].thread, NULL, stress_connection_thread, &connections[index]),
			0, NULL
		);
	}

	for (unsigned int index = 0; index < connections_count; index++) {
		test_check_int_eq (pthread_join (connections[index].thread, NULL), 0, NULL);
	}

	const double seconds = test_wait_now () - start;

	const bool passed = stress_report (connections, seconds);

	for (unsigned int index = 0; index < connections_count; index++) {
		stress_end (&connections[index]);
	}

	free (connections);

	return passed ? 0 : 1;

}
