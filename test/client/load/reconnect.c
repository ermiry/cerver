#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "balancer.h"

#include "../../test.h"

// a service stops in the middle of the pair and test/supervise.sh starts it again:
// the balancer sends everything to the other one, connects to the new one,
// and the client gets answers from both services again, from the new process too

#define RECONNECT_OWNER				0
#define RECONNECT_PACKETS			64

// sends identity requests until both services answered with a pid that is not 0,
// or until one of them answers with another pid than the one in before
static bool reconnect_identities (
	BalancerClient *client, const uint32_t before[UPSTREAM_SERVICES], const bool changed
) {

	bool done = false;

	const double deadline = test_wait_now () + BALANCER_WAIT_SECONDS;
	while (!done && (test_wait_now () < deadline)) {
		balancer_client_send_empty (client, UPSTREAM_REQUEST_IDENTITY);
		balancer_sleep (BALANCER_PROBE_NANOSECONDS);

		bool all = true;
		bool other = false;
		for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
			const uint32_t pid = balancer_service_pid (service);
			all = all && pid;
			other = other || (pid && (pid != before[service]));
		}

		done = changed ? (all && other) : all;
	}

	return done;

}

static void reconnect_echoes (BalancerClient *client, const unsigned int expected) {

	for (unsigned int index = 0; index < RECONNECT_PACKETS; index++) {
		balancer_client_send (client, UPSTREAM_REQUEST_ECHO, BALANCER_BODY_SIZE);
	}

	test_check_unsigned_eq (
		balancer_wait_echoes (RECONNECT_OWNER, expected), expected,
		"the echoes did not arrive"
	);

}

int main (void) {

	balancer_init ();

	(void) printf ("Testing BALANCER reconnect...\n");

	BalancerClient client = balancer_client_connect (RECONNECT_OWNER);

	const uint32_t none[UPSTREAM_SERVICES] = { 0 };
	test_check (reconnect_identities (&client, none, false), "both services did not answer");

	uint32_t before[UPSTREAM_SERVICES];
	for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
		before[service] = balancer_service_pid (service);
	}

	reconnect_echoes (&client, RECONNECT_PACKETS);

	// the next service in the round stops, the requests that it had are lost
	balancer_client_send_empty (&client, UPSTREAM_REQUEST_EXIT);

	test_check (
		reconnect_identities (&client, before, true),
		"the service that stopped did not come back"
	);

	// after the reconnection every request is answered
	const unsigned int answered = balancer_echoes (RECONNECT_OWNER);
	reconnect_echoes (&client, answered + RECONNECT_PACKETS);

	balancer_client_end (&client);

	balancer_end ();

	(void) printf ("Done!\n");

	return 0;

}
