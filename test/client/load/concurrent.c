#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#include <pthread.h>

#include "balancer.h"

#include "../../test.h"

// many clients use the balancer at the same time, each with its own bodies:
// every response reaches the client that sent its request (the routes by fd),
// none comes back changed, and the round robin gives work to both services

#define CONCURRENT_CLIENTS			BALANCER_MAX_CLIENTS
#define CONCURRENT_PACKETS			64
#define CONCURRENT_IDENTITIES		4
#define CONCURRENT_BIGGEST_BODY		4096
#define CONCURRENT_BODY_STEP		61

// each service answers at least this part of the identities (a quarter)
#define CONCURRENT_MINIMUM_SHIFT	2

static unsigned int concurrent_identities (void) {

	unsigned int total = 0;
	for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
		total += __atomic_load_n (&balancer_counters.identities[service], __ATOMIC_SEQ_CST);
	}

	return total;

}

// the identities of every client, that can come after its last echo from the other service
static bool concurrent_wait_identities (void) {

	const unsigned int expected = CONCURRENT_CLIENTS * CONCURRENT_IDENTITIES;

	const double deadline = test_wait_now () + BALANCER_WAIT_SECONDS;
	while ((concurrent_identities () < expected) && (test_wait_now () < deadline)) {
		balancer_sleep (TEST_WAIT_POLL_NANOSECONDS);
	}

	return concurrent_identities () == expected;

}

static void *concurrent_client (void *owner_ptr) {

	const unsigned int owner = *(const unsigned int *) owner_ptr;

	BalancerClient client = balancer_client_connect (owner);

	for (unsigned int index = 0; index < CONCURRENT_PACKETS; index++) {
		const size_t size = BALANCER_BODY_SIZE + ((index * CONCURRENT_BODY_STEP) % CONCURRENT_BIGGEST_BODY);
		balancer_client_send (&client, UPSTREAM_REQUEST_ECHO, size);
	}

	for (unsigned int index = 0; index < CONCURRENT_IDENTITIES; index++) {
		balancer_client_send_empty (&client, UPSTREAM_REQUEST_IDENTITY);
	}

	test_check_unsigned_eq (
		balancer_wait_echoes (owner, CONCURRENT_PACKETS), CONCURRENT_PACKETS,
		"a client did not get all of its echoes"
	);

	test_check (concurrent_wait_identities (), "the identities did not arrive");

	balancer_client_end (&client);

	return NULL;

}

int main (void) {

	balancer_init ();

	(void) printf ("Testing BALANCER concurrent clients...\n");

	unsigned int owners[CONCURRENT_CLIENTS];
	pthread_t threads[CONCURRENT_CLIENTS];

	for (unsigned int index = 0; index < CONCURRENT_CLIENTS; index++) {
		owners[index] = index;
		test_check_int_eq (pthread_create (&threads[index], NULL, concurrent_client, &owners[index]), 0, NULL);
	}

	for (unsigned int index = 0; index < CONCURRENT_CLIENTS; index++) {
		test_check_int_eq (pthread_join (threads[index], NULL), 0, NULL);
	}

	// every client waited for all the identities before it ended
	const unsigned int total = concurrent_identities ();
	(void) printf ("Identities: %u of %u\n", total, CONCURRENT_CLIENTS * CONCURRENT_IDENTITIES);

	const unsigned int minimum = total >> CONCURRENT_MINIMUM_SHIFT;
	for (unsigned int service = 0; service < UPSTREAM_SERVICES; service++) {
		const unsigned int answered = __atomic_load_n (&balancer_counters.identities[service], __ATOMIC_SEQ_CST);
		(void) printf ("Service %u answered %u identities\n", service, answered);

		test_check (answered >= minimum, "the round robin did not give work to both services");
	}

	test_check (total > 0, "no identity was answered");

	balancer_end ();

	(void) printf ("Done!\n");

	return 0;

}
