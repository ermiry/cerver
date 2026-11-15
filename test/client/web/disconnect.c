#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

#include "../../address.h"

// a client of test/web/disconnect.c behind nginx (test/nginx) that leaves in every part of the process,
// it checks what each cerver instance saw through their metrics, the states that the gone handler found
// and the files left in their uploads paths, for the three nginx configurations:
// - direct: HTTP/1.1 to the upstream with keepalive, and an unbuffered location
// - services: HTTP/1.0 to the upstream, with the request and the response buffered by nginx
// - pair: two instances in the same upstream, one of them crashes in the middle of a request

#define DISCONNECT_PORT_VARIABLE			"DISCONNECT_PORT"
#define DISCONNECT_INSTANCES_VARIABLE		"DISCONNECT_INSTANCES"
#define DISCONNECT_CONFIG_VARIABLE			"DISCONNECT_CONFIG"

#define DISCONNECT_DEFAULT_PORT				"80"
#define DISCONNECT_INSTANCE_PORT			"8080"
#define DISCONNECT_DEFAULT_INSTANCES		"cerver-disconnect"

#define DISCONNECT_CONFIG_DIRECT			"direct"
#define DISCONNECT_CONFIG_SERVICES			"services"
#define DISCONNECT_CONFIG_PAIR				"pair"

#define DISCONNECT_PREFIX					"/disconnect"
#define DISCONNECT_UNBUFFERED_PREFIX		"/disconnect/unbuffered"

#define DISCONNECT_INSTANCES_MAX			2
#define DISCONNECT_HOST_SIZE				128

#define DISCONNECT_REQUEST_SIZE				(1 << 12)
#define DISCONNECT_RESPONSE_SIZE			(1 << 16)

#define DISCONNECT_STAGES					7
#define DISCONNECT_STAGE_SIZE				32

// secs
#define DISCONNECT_RECEIVE_TIMEOUT			16
#define DISCONNECT_WAIT_SECONDS				16

// milliseconds
#define DISCONNECT_FORWARD_WAIT				300
#define DISCONNECT_SETTLE_WAIT				1000
#define DISCONNECT_POLL_WAIT				50

// nginx takes an instance out after its fails, and tries it again after fail_timeout (secs)
#define DISCONNECT_FAIL_TIMEOUT				11

#define DISCONNECT_NANOSECONDS_PER_MILLISECOND	1000000L
#define DISCONNECT_MILLISECONDS_PER_SECOND		1000

#define DISCONNECT_REQUESTS					8
#define DISCONNECT_CRASH_REQUESTS			4

#define DISCONNECT_BODY_LENGTH				64
#define DISCONNECT_BODY_SENT				10

#define DISCONNECT_STATUS_OK				200
#define DISCONNECT_STATUS_BAD_GATEWAY		502

#define DISCONNECT_BOUNDARY					"disconnectboundary"

static const char *disconnect_stages[DISCONNECT_STAGES] = {
	"queued", "request_line", "headers", "body", "upload", "handler", "response"
};

static const char *nginx_address = NULL;
static const char *nginx_port = DISCONNECT_DEFAULT_PORT;

static unsigned int n_instances = 0;
static char instances[DISCONNECT_INSTANCES_MAX][DISCONNECT_HOST_SIZE];

static unsigned int failures = 0;

#pragma region network

static void disconnect_milliseconds_sleep (const unsigned int milliseconds) {

	const struct timespec wait = {
		milliseconds / DISCONNECT_MILLISECONDS_PER_SECOND,
		(long) (milliseconds % DISCONNECT_MILLISECONDS_PER_SECOND) * DISCONNECT_NANOSECONDS_PER_MILLISECOND
	};

	(void) nanosleep (&wait, NULL);

}

static double disconnect_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / (double) (DISCONNECT_NANOSECONDS_PER_MILLISECOND * DISCONNECT_MILLISECONDS_PER_SECOND));

}

// connects to any address of the host, returns -1 on error
static int disconnect_connect (const char *host, const char *port) {

	int fd = -1;

	struct addrinfo hints;
	(void) memset (&hints, 0, sizeof (struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *addresses = NULL;
	if (!getaddrinfo (host, port, &hints, &addresses)) {
		for (struct addrinfo *address = addresses; address && (fd < 0); address = address->ai_next) {
			fd = socket (address->ai_family, address->ai_socktype, address->ai_protocol);
			if ((fd > -1) && connect (fd, address->ai_addr, address->ai_addrlen)) {
				(void) close (fd);
				fd = -1;
			}
		}

		freeaddrinfo (addresses);
	}

	if (fd > -1) {
		const struct timeval timeout = { DISCONNECT_RECEIVE_TIMEOUT, 0 };
		(void) setsockopt (fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof (struct timeval));
	}

	return fd;

}

static bool disconnect_send (const int fd, const char *data, const size_t length) {

	size_t sent = 0;
	while (sent < length) {
		const ssize_t rc = send (fd, data + sent, length - sent, MSG_NOSIGNAL);
		if (rc <= 0) break;

		sent += (size_t) rc;
	}

	return sent == length;

}

// reads until the connection ends, keeps the first bytes
static size_t disconnect_read (const int fd, char *response, const size_t size) {

	size_t total = 0;

	char buffer[DISCONNECT_RESPONSE_SIZE];
	ssize_t rc = 0;
	while ((rc = recv (fd, buffer, sizeof (buffer), 0)) > 0) {
		if (total < (size - 1)) {
			const size_t copied = (((size_t) rc) < (size - 1 - total)) ? (size_t) rc : (size - 1 - total);
			(void) memcpy (response + total, buffer, copied);
		}

		total += (size_t) rc;
	}

	response[(total < size) ? total : (size - 1)] = '\0';

	return total;

}

static void disconnect_reset (const int fd) {

	const struct linger linger = { 1, 0 };
	(void) setsockopt (fd, SOL_SOCKET, SO_LINGER, &linger, sizeof (struct linger));
	(void) close (fd);

}

// the status of the response, 0 if there is none
static int disconnect_status (const char *response) {

	int major = 0, minor = 0, status = 0;

	return (sscanf (response, "HTTP/%d.%d %d", &major, &minor, &status) == 3) ? status : 0;

}

// sends a complete request and reads its complete response, returns its status
static int disconnect_request (
	const char *host, const char *port, const char *method, const char *path,
	char *response, const size_t size
) {

	int status = 0;

	const int fd = disconnect_connect (host, port);
	if (fd > -1) {
		char request[DISCONNECT_REQUEST_SIZE] = { 0 };
		const int length = snprintf (
			request, DISCONNECT_REQUEST_SIZE,
			"%s %s HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\nContent-Length: 0\r\n\r\n",
			method, path
		);

		if (disconnect_send (fd, request, (size_t) length)) {
			(void) disconnect_read (fd, response, size);
			status = disconnect_status (response);
		}

		(void) close (fd);
	}

	return status;

}

static int disconnect_nginx_request (const char *method, const char *path) {

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };

	return disconnect_request (nginx_address, nginx_port, method, path, response, DISCONNECT_RESPONSE_SIZE);

}

#pragma endregion

#pragma region instances

// what an instance tells about the requests whose client left
typedef struct DisconnectCounters {

	size_t aborted[DISCONNECT_STAGES];
	size_t vanished[DISCONNECT_STAGES];
	size_t failed_responses;
	size_t ok_responses;

	unsigned int closed;
	unsigned int reset;
	unsigned int uploads;

} DisconnectCounters;

static int disconnect_stage_index (const char *stage) {

	int index = -1;
	for (int i = 0; (i < DISCONNECT_STAGES) && (index < 0); i++) {
		if (!strcmp (disconnect_stages[i], stage)) index = i;
	}

	return index;

}

// a line of the metrics: the aborted and the vanished requests by stage,
// the failed responses and the 200 responses
static void disconnect_metrics_line (const char *line, DisconnectCounters *counters) {

	char stage[DISCONNECT_STAGE_SIZE] = { 0 };
	char reason[DISCONNECT_STAGE_SIZE] = { 0 };
	size_t value = 0;

	if (sscanf (line, "cerver_http_aborted_requests_total{stage=\"%31[^\"]\",reason=\"%31[^\"]\"} %zu", stage, reason, &value) == 3) {
		const int index = disconnect_stage_index (stage);
		if (index > -1) counters->aborted[index] += value;
	}

	else if (sscanf (line, "cerver_http_vanished_requests_total{stage=\"%31[^\"]\"} %zu", stage, &value) == 2) {
		const int index = disconnect_stage_index (stage);
		if (index > -1) counters->vanished[index] += value;
	}

	else if (sscanf (line, "cerver_http_failed_responses_total %zu", &value) == 1) {
		counters->failed_responses += value;
	}

	else if (sscanf (line, "cerver_http_responses_total{code=\"200\"} %zu", &value) == 1) {
		counters->ok_responses += value;
	}

}

static unsigned int disconnect_json_value (const char *response, const char *key) {

	unsigned int value = 0;

	const char *found = strstr (response, key);
	if (found) value = (unsigned int) strtoul (found + strlen (key), NULL, 10);

	return value;

}

// reads the counters of an instance directly, without nginx
// returns false if the instance did not answer
static bool disconnect_instance_read (const char *instance, DisconnectCounters *counters) {

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };

	bool read = (disconnect_request (
		instance, DISCONNECT_INSTANCE_PORT, "GET", "/instance/cerver/metrics", response, DISCONNECT_RESPONSE_SIZE
	) == DISCONNECT_STATUS_OK) && (strstr (response, "cerver_http_failed_responses_total") != NULL);

	if (read) {
		char *saveptr = NULL;
		for (char *line = strtok_r (response, "\n", &saveptr); line; line = strtok_r (NULL, "\n", &saveptr)) {
			disconnect_metrics_line (line, counters);
		}
	}

	read = read && (disconnect_request (
		instance, DISCONNECT_INSTANCE_PORT, "GET", "/states", response, DISCONNECT_RESPONSE_SIZE
	) == DISCONNECT_STATUS_OK);

	if (read) {
		counters->closed += disconnect_json_value (response, "\"closed\": ");
		counters->reset += disconnect_json_value (response, "\"reset\": ");
	}

	read = read && (disconnect_request (
		instance, DISCONNECT_INSTANCE_PORT, "GET", "/uploads", response, DISCONNECT_RESPONSE_SIZE
	) == DISCONNECT_STATUS_OK);

	if (read) counters->uploads += disconnect_json_value (response, "\"uploads\": ");

	return read;

}

// the counters of every instance added
static void disconnect_counters_read (DisconnectCounters *counters) {

	(void) memset (counters, 0, sizeof (DisconnectCounters));

	for (unsigned int i = 0; i < n_instances; i++) {
		if (!disconnect_instance_read (instances[i], counters)) {
			(void) fprintf (stderr, "the instance %s did not answer\n", instances[i]);
			failures += 1;
		}
	}

}

static size_t disconnect_aborted_total (const DisconnectCounters *counters) {

	size_t total = 0;
	for (unsigned int i = 0; i < DISCONNECT_STAGES; i++) total += counters->aborted[i] + counters->vanished[i];

	return total;

}

// the instance answers directly again, after it was restarted
static bool disconnect_instance_wait (const char *instance) {

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };

	bool ready = false;
	const double deadline = disconnect_now () + (double) DISCONNECT_WAIT_SECONDS;
	while (!ready && (disconnect_now () < deadline)) {
		ready = disconnect_request (
			instance, DISCONNECT_INSTANCE_PORT, "GET", "/ok", response, DISCONNECT_RESPONSE_SIZE
		) == DISCONNECT_STATUS_OK;

		if (!ready) disconnect_milliseconds_sleep (DISCONNECT_POLL_WAIT);
	}

	return ready;

}

#pragma endregion

#pragma region checks

typedef bool (*DisconnectCondition) (const DisconnectCounters *before, const DisconnectCounters *after);

// waits until the counters of the instances meet the condition
static bool disconnect_wait_condition (
	const DisconnectCounters *before, const DisconnectCondition condition, DisconnectCounters *after
) {

	bool met = false;
	const double deadline = disconnect_now () + (double) DISCONNECT_WAIT_SECONDS;
	while (!met && (disconnect_now () < deadline)) {
		disconnect_counters_read (after);
		met = condition (before, after);

		if (!met) disconnect_milliseconds_sleep (DISCONNECT_POLL_WAIT);
	}

	return met;

}

static void disconnect_check (const bool passed, const char *scenario) {

	(void) printf ("\t%s: %s\n", scenario, passed ? "ok" : "failed");

	if (!passed) failures += 1;

}

static bool disconnect_condition_nothing (const DisconnectCounters *before, const DisconnectCounters *after) {

	return (disconnect_aborted_total (after) == disconnect_aborted_total (before))
		&& (after->failed_responses == before->failed_responses)
		&& (after->uploads == 0);

}

static bool disconnect_condition_body (const DisconnectCounters *before, const DisconnectCounters *after) {

	const unsigned int body = 3;

	return ((after->aborted[body] - before->aborted[body]) == 1)
		&& (disconnect_aborted_total (after) == (disconnect_aborted_total (before) + 1));

}

static bool disconnect_condition_upload (const DisconnectCounters *before, const DisconnectCounters *after) {

	const unsigned int upload = 4;

	return ((after->aborted[upload] - before->aborted[upload]) == 1)
		&& (disconnect_aborted_total (after) == (disconnect_aborted_total (before) + 1))
		&& (after->uploads == 0);

}

static bool disconnect_condition_gone (const DisconnectCounters *before, const DisconnectCounters *after) {

	return ((after->closed + after->reset) - (before->closed + before->reset)) == 1;

}

static bool disconnect_condition_failed (const DisconnectCounters *before, const DisconnectCounters *after) {

	return (after->failed_responses - before->failed_responses) == 1;

}

#pragma endregion

#pragma region scenarios

// sends the first bytes of a request through nginx and resets the connection
static void disconnect_nginx_leave (const char *data, const size_t length) {

	const int fd = disconnect_connect (nginx_address, nginx_port);
	if (fd > -1) {
		(void) disconnect_send (fd, data, length);

		// nginx forwards what it does not buffer before the client leaves
		disconnect_milliseconds_sleep (DISCONNECT_FORWARD_WAIT);

		disconnect_reset (fd);
	}

	else {
		failures += 1;
	}

}

// a body that stops in the middle: buffered, the cerver never sees it,
// unbuffered, the cerver sees its client (nginx) leave in the body
static void disconnect_body (const char *prefix, const bool buffered) {

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	int length = snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"POST %s/echo HTTP/1.1\r\nHost: localhost\r\nContent-Type: text/plain\r\nContent-Length: %d\r\n\r\n",
		prefix, DISCONNECT_BODY_LENGTH
	);

	(void) memset (request + length, 'b', DISCONNECT_BODY_SENT);
	length += DISCONNECT_BODY_SENT;

	DisconnectCounters before, after;
	disconnect_counters_read (&before);

	disconnect_nginx_leave (request, (size_t) length);

	if (buffered) {
		disconnect_milliseconds_sleep (DISCONNECT_SETTLE_WAIT);
		disconnect_counters_read (&after);
		disconnect_check (disconnect_condition_nothing (&before, &after), "a buffered body that stops");
	}

	else {
		disconnect_check (
			disconnect_wait_condition (&before, disconnect_condition_body, &after),
			"an unbuffered body that stops"
		);
	}

}

// a multi-part body that stops in the middle of its file
static void disconnect_upload (const char *prefix, const bool buffered) {

	const char *body = {
		"--" DISCONNECT_BOUNDARY "\r\n"
		"Content-Disposition: form-data; name=\"file\"; filename=\"file.txt\"\r\n"
		"Content-Type: text/plain\r\n"
		"\r\n"
		"the first half of the file"
	};

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	const int length = snprintf (
		request, DISCONNECT_REQUEST_SIZE,
		"POST %s/upload HTTP/1.1\r\nHost: localhost\r\n"
		"Content-Type: multipart/form-data; boundary=" DISCONNECT_BOUNDARY "\r\n"
		"Content-Length: %zu\r\n\r\n%s",
		prefix, strlen (body) * 2, body
	);

	DisconnectCounters before, after;
	disconnect_counters_read (&before);

	disconnect_nginx_leave (request, (size_t) length);

	if (buffered) {
		disconnect_milliseconds_sleep (DISCONNECT_SETTLE_WAIT);
		disconnect_counters_read (&after);
		disconnect_check (disconnect_condition_nothing (&before, &after), "a buffered upload that stops");
	}

	else {
		disconnect_check (
			disconnect_wait_condition (&before, disconnect_condition_upload, &after),
			"an unbuffered upload that stops, without files left"
		);
	}

}

// the client leaves while the handler runs, nginx closes its connection to the cerver
// and the handler finds it gone
static void disconnect_handler (const char *prefix) {

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	const int length = snprintf (request, DISCONNECT_REQUEST_SIZE, "GET %s/gone HTTP/1.1\r\nHost: localhost\r\n\r\n", prefix);

	DisconnectCounters before, after;
	disconnect_counters_read (&before);

	const int fd = disconnect_connect (nginx_address, nginx_port);
	if ((fd > -1) && disconnect_send (fd, request, (size_t) length)) {
		disconnect_milliseconds_sleep (DISCONNECT_FORWARD_WAIT);
		(void) close (fd);

		disconnect_check (
			disconnect_wait_condition (&before, disconnect_condition_gone, &after),
			"the handler finds that its client left"
		);
	}

	else {
		if (fd > -1) (void) close (fd);
		disconnect_check (false, "the handler finds that its client left");
	}

}

// the client leaves after the first byte of a large response:
// buffered, nginx may keep reading it, unbuffered, the cerver's response fails
static void disconnect_response (const char *prefix, const bool buffered) {

	char request[DISCONNECT_REQUEST_SIZE] = { 0 };
	const int length = snprintf (request, DISCONNECT_REQUEST_SIZE, "GET %s/large HTTP/1.1\r\nHost: localhost\r\n\r\n", prefix);

	DisconnectCounters before, after;
	disconnect_counters_read (&before);

	const int fd = disconnect_connect (nginx_address, nginx_port);
	char byte = 0;
	if ((fd > -1) && disconnect_send (fd, request, (size_t) length) && (recv (fd, &byte, 1, 0) == 1)) {
		disconnect_reset (fd);

		if (buffered) {
			disconnect_milliseconds_sleep (DISCONNECT_SETTLE_WAIT);
			disconnect_counters_read (&after);

			// nginx may finish reading the response into its buffers, or close the cerver's connection
			disconnect_check (
				(after.failed_responses - before.failed_responses) <= 1,
				"a buffered response that the client leaves"
			);
		}

		else {
			disconnect_check (
				disconnect_wait_condition (&before, disconnect_condition_failed, &after),
				"an unbuffered response that the client leaves"
			);
		}
	}

	else {
		if (fd > -1) (void) close (fd);
		disconnect_check (false, buffered ? "a buffered response that the client leaves" : "an unbuffered response that the client leaves");
	}

	// the cerver still answers through nginx
	disconnect_check (
		disconnect_nginx_request ("GET", DISCONNECT_PREFIX "/ok") == DISCONNECT_STATUS_OK,
		"the cerver answers after a client left its response"
	);

}

// every request through nginx is answered by an instance, and the instances counted them all
static void disconnect_pair_sum (void) {

	DisconnectCounters before, after;
	disconnect_counters_read (&before);

	unsigned int answered = 0;
	for (unsigned int i = 0; i < DISCONNECT_REQUESTS; i++) {
		if (disconnect_nginx_request ("GET", DISCONNECT_PREFIX "/ok") == DISCONNECT_STATUS_OK) answered += 1;
	}

	disconnect_counters_read (&after);

	// the metrics of each instance were read once before, and that response is counted
	disconnect_check (
		(answered == DISCONNECT_REQUESTS)
			&& ((after.ok_responses - before.ok_responses) == (DISCONNECT_REQUESTS + (n_instances * 3))),
		"the instances count every request that nginx sent them"
	);

}

// an instance dies in the middle of a request: nginx sends a GET again to the other one,
// a POST is not sent again (it is not idempotent), so it is answered with 502
static void disconnect_pair_crash (void) {

	unsigned int get_answered = 0;
	for (unsigned int i = 0; i < DISCONNECT_CRASH_REQUESTS; i++) {
		if (disconnect_nginx_request ("GET", DISCONNECT_PREFIX "/crash") == DISCONNECT_STATUS_OK) get_answered += 1;
	}

	disconnect_check (get_answered == DISCONNECT_CRASH_REQUESTS, "a GET is sent again to the other instance");

	unsigned int post_expected = 0;
	for (unsigned int i = 0; i < DISCONNECT_CRASH_REQUESTS; i++) {
		const int status = disconnect_nginx_request ("POST", DISCONNECT_PREFIX "/crash");
		if ((status == DISCONNECT_STATUS_OK) || (status == DISCONNECT_STATUS_BAD_GATEWAY)) post_expected += 1;
	}

	disconnect_check (post_expected == DISCONNECT_CRASH_REQUESTS, "a POST is answered or fails with 502");

	for (unsigned int i = 0; i < n_instances; i++) {
		disconnect_check (disconnect_instance_wait (instances[i]), "the instance that crashed is restarted");
	}

}

// an instance drains: its request in progress is answered,
// and nginx sends the new ones to the other instance
static void disconnect_pair_drain (void) {

	const char *draining = instances[0];

	char response[DISCONNECT_RESPONSE_SIZE] = { 0 };

	const int slow = disconnect_connect (draining, DISCONNECT_INSTANCE_PORT);
	const char *slow_request = { "GET /slow HTTP/1.1\r\nHost: localhost\r\n\r\n" };
	const bool slow_sent = (slow > -1) && disconnect_send (slow, slow_request, strlen (slow_request));

	const int drain = disconnect_request (draining, DISCONNECT_INSTANCE_PORT, "GET", "/drain", response, DISCONNECT_RESPONSE_SIZE);

	unsigned int answered = 0;
	for (unsigned int i = 0; i < DISCONNECT_REQUESTS; i++) {
		if (disconnect_nginx_request ("GET", DISCONNECT_PREFIX "/ok") == DISCONNECT_STATUS_OK) answered += 1;
	}

	int slow_status = 0;
	if (slow_sent) {
		(void) disconnect_read (slow, response, DISCONNECT_RESPONSE_SIZE);
		slow_status = disconnect_status (response);
	}

	if (slow > -1) (void) close (slow);

	disconnect_check (drain == DISCONNECT_STATUS_OK, "an instance starts to drain");
	disconnect_check (slow_status == DISCONNECT_STATUS_OK, "the request in progress of a draining instance is answered");
	disconnect_check (answered == DISCONNECT_REQUESTS, "nginx sends the requests to the instance that is not draining");
	disconnect_check (disconnect_instance_wait (draining), "the instance that drained is restarted");

}

// after fail_timeout, nginx sends requests again to the instance that failed
static void disconnect_pair_returns (void) {

	(void) sleep (DISCONNECT_FAIL_TIMEOUT);

	DisconnectCounters before[DISCONNECT_INSTANCES_MAX];
	for (unsigned int i = 0; i < n_instances; i++) {
		(void) memset (&before[i], 0, sizeof (DisconnectCounters));
		(void) disconnect_instance_read (instances[i], &before[i]);
	}

	for (unsigned int i = 0; i < DISCONNECT_REQUESTS; i++) {
		(void) disconnect_nginx_request ("GET", DISCONNECT_PREFIX "/ok");
	}

	bool every_one = true;
	for (unsigned int i = 0; i < n_instances; i++) {
		DisconnectCounters after;
		(void) memset (&after, 0, sizeof (DisconnectCounters));
		(void) disconnect_instance_read (instances[i], &after);

		// more than the responses of its own counters
		every_one = every_one && ((after.ok_responses - before[i].ok_responses) > 3);
	}

	disconnect_check (every_one, "nginx sends requests again to every instance after fail_timeout");

}

#pragma endregion

#pragma region main

static void disconnect_instances_parse (void) {

	const char *value = getenv (DISCONNECT_INSTANCES_VARIABLE);

	char list[DISCONNECT_HOST_SIZE * DISCONNECT_INSTANCES_MAX] = { 0 };
	(void) snprintf (list, sizeof (list), "%s", (value && value[0]) ? value : DISCONNECT_DEFAULT_INSTANCES);

	char *saveptr = NULL;
	for (
		char *host = strtok_r (list, ",", &saveptr);
		host && (n_instances < DISCONNECT_INSTANCES_MAX);
		host = strtok_r (NULL, ",", &saveptr)
	) {
		(void) snprintf (instances[n_instances], DISCONNECT_HOST_SIZE, "%s", host);
		n_instances += 1;
	}

}

static void disconnect_direct (void) {

	disconnect_body (DISCONNECT_PREFIX, true);
	disconnect_body (DISCONNECT_UNBUFFERED_PREFIX, false);
	disconnect_upload (DISCONNECT_UNBUFFERED_PREFIX, false);
	disconnect_handler (DISCONNECT_PREFIX);
	disconnect_response (DISCONNECT_PREFIX, true);
	disconnect_response (DISCONNECT_UNBUFFERED_PREFIX, false);

}

static void disconnect_services (void) {

	disconnect_body (DISCONNECT_PREFIX, true);
	disconnect_upload (DISCONNECT_PREFIX, true);
	disconnect_handler (DISCONNECT_PREFIX);
	disconnect_response (DISCONNECT_PREFIX, true);

}

static void disconnect_pair (void) {

	disconnect_pair_sum ();
	disconnect_handler (DISCONNECT_PREFIX);

	// the drain needs the other instance available, the crashes take it out for fail_timeout,
	// and the requests refused while an instance drains and restarts take it out too
	disconnect_pair_drain ();
	(void) sleep (DISCONNECT_FAIL_TIMEOUT);

	disconnect_pair_crash ();
	disconnect_pair_returns ();

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	nginx_address = test_address ();

	const char *port = getenv (DISCONNECT_PORT_VARIABLE);
	if (port && port[0]) nginx_port = port;

	disconnect_instances_parse ();

	const char *config = getenv (DISCONNECT_CONFIG_VARIABLE);
	if (!config || !config[0]) config = DISCONNECT_CONFIG_DIRECT;

	(void) printf ("disconnect - %s - nginx %s port %s - %u instances\n", config, nginx_address, nginx_port, n_instances);

	if (!strcmp (config, DISCONNECT_CONFIG_SERVICES)) disconnect_services ();
	else if (!strcmp (config, DISCONNECT_CONFIG_PAIR)) disconnect_pair ();
	else disconnect_direct ();

	(void) printf ("disconnect - %s: %s\n", config, failures ? "failed" : "ok");

	return failures ? 1 : 0;

}

#pragma endregion
