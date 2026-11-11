#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include "balancer.h"

#include "../../test.h"

// clients leave while their slow requests are still in the services:
// the balancer drops the responses of the connections that closed and keeps working,
// and the clients that come after them only get their own responses
// a new connection that takes the fd of one with a request in flight is the limit
// documented in the balancer (B9), so the new client waits until the slow requests are done

#define ABANDON_ROUNDS				2
#define ABANDON_CLIENTS				4
#define ABANDON_SLOW_REQUESTS		4
#define ABANDON_OWNER				(BALANCER_MAX_CLIENTS - 1)
#define ABANDON_PACKETS				64

// each service answers its slow requests one after the other, twice that time is waited
#define ABANDON_DRAIN_FACTOR		2

static void abandon_round (const unsigned int round) {

	for (unsigned int index = 0; index < ABANDON_CLIENTS; index++) {
		BalancerClient client = balancer_client_connect (index);

		for (unsigned int request = 0; request < ABANDON_SLOW_REQUESTS; request++) {
			balancer_client_send (&client, UPSTREAM_REQUEST_SLOW, BALANCER_BODY_SIZE);
		}

		// it leaves without waiting for its responses
		balancer_client_end (&client);
	}

	const long drain = (UPSTREAM_SLOW_NANOSECONDS * ABANDON_CLIENTS * ABANDON_SLOW_REQUESTS * ABANDON_DRAIN_FACTOR)
		/ UPSTREAM_SERVICES;
	balancer_sleep (drain);

	BalancerClient client = balancer_client_connect (ABANDON_OWNER);

	for (unsigned int index = 0; index < ABANDON_PACKETS; index++) {
		balancer_client_send (&client, UPSTREAM_REQUEST_ECHO, BALANCER_BODY_SIZE);
	}

	const unsigned int expected = (round + 1) * ABANDON_PACKETS;
	test_check_unsigned_eq (
		balancer_wait_echoes (ABANDON_OWNER, expected), expected,
		"the balancer stopped working after the clients left"
	);

	balancer_client_end (&client);

	balancer_check_bodies ();

}

int main (void) {

	balancer_init ();

	(void) printf ("Testing BALANCER abandoned requests...\n");

	for (unsigned int round = 0; round < ABANDON_ROUNDS; round++) {
		abandon_round (round);
	}

	// no response of a client that left reached anyone
	for (unsigned int index = 0; index < ABANDON_CLIENTS; index++) {
		test_check_unsigned_eq (balancer_echoes (index), 0, "a client that left got a response");
	}

	balancer_end ();

	(void) printf ("Done!\n");

	return 0;

}
