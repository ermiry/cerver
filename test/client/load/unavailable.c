#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "../../test.h"
#include "balancer.h"

// both services stop, and test/supervise.sh starts them again some seconds later:
// meanwhile the balancer answers each request with an error instead of hanging,
// and it works again with both services when they are back

#define UNAVAILABLE_OWNER			0
#define UNAVAILABLE_PACKETS			64

static bool unavailable_identities (BalancerClient *client, const uint32_t before[UPSTREAM_SERVICES]) {

	bool done = false;

	const double deadline = test_wait_now () + BALANCER_WAIT_SECONDS;
	while (!done && (test_wait_now () < deadline)) {
		balancer_client_send_empty (client, UPSTREAM_REQUEST_IDENTITY);
		balancer_sleep (BALANCER_PROBE_NANOSECONDS);

		done = true;
		for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
			const uint32_t pid = balancer_service_pid (service);
			done = done && pid && (pid != before[service]);
		}
	}

	return done;

}

// a probe that is answered means that a service is still up, so it is asked to stop too
static bool unavailable_wait_error (BalancerClient *client) {

	unsigned int answered = balancer_echoes (UNAVAILABLE_OWNER);

	const double deadline = test_wait_now () + BALANCER_WAIT_SECONDS;
	while (!balancer_errors (UNAVAILABLE_OWNER) && (test_wait_now () < deadline)) {
		balancer_client_send (client, UPSTREAM_REQUEST_ECHO, BALANCER_BODY_SIZE);
		balancer_sleep (BALANCER_PROBE_NANOSECONDS);

		const unsigned int now = balancer_echoes (UNAVAILABLE_OWNER);
		if (now > answered) {
			balancer_client_send_empty (client, UPSTREAM_REQUEST_EXIT);
			answered = now;
		}
	}

	return balancer_errors (UNAVAILABLE_OWNER) > 0;

}

int main (void) {

	balancer_init ();

	(void) printf ("Testing BALANCER unavailable services...\n");

	BalancerClient client = balancer_client_connect (UNAVAILABLE_OWNER);

	const uint32_t none[UPSTREAM_SERVICES] = { 0 };
	test_check (unavailable_identities (&client, none), "both services did not answer");

	uint32_t before[UPSTREAM_SERVICES];
	for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
		before[service] = balancer_service_pid (service);
	}

	// one stop for each service in the round
	for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
		balancer_client_send_empty (&client, UPSTREAM_REQUEST_EXIT);
	}

	test_check (unavailable_wait_error (&client), "the balancer did not answer with an error");
	test_check (client_is_running (client.client), "the balancer closed the client without services");

	test_check (unavailable_identities (&client, before), "the services did not come back");

	const unsigned int answered = balancer_echoes (UNAVAILABLE_OWNER);
	for (unsigned int index = 0; index < UNAVAILABLE_PACKETS; index++) {
		balancer_client_send (&client, UPSTREAM_REQUEST_ECHO, BALANCER_BODY_SIZE);
	}

	test_check_unsigned_eq (
		balancer_wait_echoes (UNAVAILABLE_OWNER, answered + UNAVAILABLE_PACKETS), answered + UNAVAILABLE_PACKETS,
		"the balancer did not work again with its services"
	);

	balancer_client_end (&client);

	balancer_end ();

	(void) printf ("Done!\n");

	return 0;

}
