#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include <cerver/packets/packets.h>

#include "../../test.h"
#include "balancer.h"

// big packets go through the balancer to a service and back, with their content checked,
// up to the biggest one that the balancer takes,
// and a packet that is bigger closes only the connection that sent it

#define LARGE_OWNER					0
#define LARGE_OTHER_OWNER			1

#define LARGE_SMALLEST				(1 << 20)
#define LARGE_SHIFT					2
#define LARGE_PACKETS_PER_SIZE		2
#define LARGE_AFTER_PACKETS			16

// the header and the body of the biggest packet that the balancer takes
#define LARGE_BIGGEST_BODY			(UPSTREAM_MAX_PACKET_SIZE - PACKET_HEADER_SIZE)

static unsigned int large_send_sizes (BalancerClient *client) {

	unsigned int sent = 0;

	for (size_t size = LARGE_SMALLEST; size < LARGE_BIGGEST_BODY; size <<= LARGE_SHIFT) {
		for (unsigned int index = 0; index < LARGE_PACKETS_PER_SIZE; index++) {
			balancer_client_send (client, UPSTREAM_REQUEST_ECHO, size);
			sent += 1;
		}

		test_check_unsigned_eq (
			balancer_wait_echoes (client->owner, sent), sent,
			"a big packet did not come back"
		);
	}

	balancer_client_send (client, UPSTREAM_REQUEST_ECHO, LARGE_BIGGEST_BODY);
	sent += 1;

	test_check_unsigned_eq (
		balancer_wait_echoes (client->owner, sent), sent,
		"the biggest packet did not come back"
	);

	return sent;

}

// the balancer can close the connection before the whole packet was sent
static void large_send_too_big (BalancerClient *client) {

	const size_t size = LARGE_BIGGEST_BODY + 1;

	char *body = (char *) calloc (1, size);
	test_check_ptr (body);

	Packet *packet = packet_generate_request (PACKET_TYPE_APP, UPSTREAM_REQUEST_ECHO, body, size);
	test_check_ptr (packet);

	packet_set_network_values (packet, NULL, client->client, client->connection, NULL);
	(void) packet_send (packet, 0, NULL, false);

	packet_delete (packet);
	free (body);

}

int main (void) {

	balancer_init ();

	(void) printf ("Testing BALANCER large packets...\n");

	BalancerClient client = balancer_client_connect (LARGE_OWNER);
	BalancerClient other = balancer_client_connect (LARGE_OTHER_OWNER);

	const unsigned int sent = large_send_sizes (&client);

	// the stream goes on after the big packets
	for (unsigned int index = 0; index < LARGE_AFTER_PACKETS; index++) {
		balancer_client_send (&client, UPSTREAM_REQUEST_ECHO, BALANCER_BODY_SIZE);
	}

	test_check_unsigned_eq (
		balancer_wait_echoes (LARGE_OWNER, sent + LARGE_AFTER_PACKETS), sent + LARGE_AFTER_PACKETS,
		"the stream did not go on after the big packets"
	);

	large_send_too_big (&client);
	test_check (balancer_client_stopped (&client), "the balancer took a packet that is too big");

	// the other clients are not affected
	for (unsigned int index = 0; index < LARGE_AFTER_PACKETS; index++) {
		balancer_client_send (&other, UPSTREAM_REQUEST_ECHO, BALANCER_BODY_SIZE);
	}

	test_check_unsigned_eq (
		balancer_wait_echoes (LARGE_OTHER_OWNER, LARGE_AFTER_PACKETS), LARGE_AFTER_PACKETS,
		"the other client stopped working"
	);

	balancer_client_end (&client);
	balancer_client_end (&other);

	balancer_end ();

	(void) printf ("Done!\n");

	return 0;

}
