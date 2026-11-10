#ifndef _TEST_CLIENT_LOAD_BALANCER_H_
#define _TEST_CLIENT_LOAD_BALANCER_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <cerver/client.h>
#include <cerver/connection.h>

#include "../../cerver/upstream.h"

// the clients of the balancer process pairs:
// each body carries the client that sent it and a pattern,
// so a response that reaches another client or comes back changed is counted

#define BALANCER_MAX_CLIENTS			32
#define BALANCER_BODY_SIZE				64
#define BALANCER_WAIT_SECONDS			32
#define BALANCER_PROBE_NANOSECONDS		(1L << 27)

typedef struct BalancerBody {

	uint32_t owner;
	uint32_t sequence;

} BalancerBody;

typedef struct BalancerClient {

	unsigned int owner;

	Client *client;
	Connection *connection;

	unsigned int sent;

} BalancerClient;

// the counters that the handlers of every client update
typedef struct BalancerCounters {

	unsigned int echoes[BALANCER_MAX_CLIENTS];
	unsigned int errors[BALANCER_MAX_CLIENTS];

	unsigned int identities[UPSTREAM_SERVICES];
	uint32_t pids[UPSTREAM_SERVICES];

	unsigned int wrong_owner;
	unsigned int bad_bodies;

} BalancerCounters;

extern BalancerCounters balancer_counters;

// connects a client to the balancer, owner is its index in [0, BALANCER_MAX_CLIENTS)
extern BalancerClient balancer_client_connect (const unsigned int owner);

// sends a request with a body of body_size bytes (at least a BalancerBody)
extern void balancer_client_send (
	BalancerClient *client, const uint32_t request_type, const size_t body_size
);

// sends a request without a body, like UPSTREAM_REQUEST_IDENTITY or UPSTREAM_REQUEST_EXIT
extern void balancer_client_send_empty (
	BalancerClient *client, const uint32_t request_type
);

extern void balancer_client_end (BalancerClient *client);

// true when the balancer closed the connection of the client
extern bool balancer_client_stopped (const BalancerClient *client);

// the echoes and the errors that the client got until now
extern unsigned int balancer_echoes (const unsigned int owner);

extern unsigned int balancer_errors (const unsigned int owner);

// waits until the client got expected echoes, returns how many it got
extern unsigned int balancer_wait_echoes (const unsigned int owner, const unsigned int expected);

// waits until the client got expected errors, returns how many it got
extern unsigned int balancer_wait_errors (const unsigned int owner, const unsigned int expected);

// the last pid that a service answered with, 0 if it never answered
extern uint32_t balancer_service_pid (const unsigned int service);

// fails if a response reached another client or came back changed
extern void balancer_check_bodies (void);

extern void balancer_sleep (const long nanoseconds);

extern void balancer_init (void);

extern void balancer_end (void);

#endif
