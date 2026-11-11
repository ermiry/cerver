#ifndef _TEST_UPSTREAM_H_
#define _TEST_UPSTREAM_H_

#include <stdint.h>

// the requests of the upstream services behind the balancer in the process pairs

// answers with the same body
#define UPSTREAM_REQUEST_ECHO			1

// answers with an UpstreamIdentity: a service that was started again has another pid
#define UPSTREAM_REQUEST_IDENTITY		2

// the service stops, test/supervise.sh starts it again
#define UPSTREAM_REQUEST_EXIT			3

// answers with the same body after UPSTREAM_SLOW_NANOSECONDS
#define UPSTREAM_REQUEST_SLOW			4

#define UPSTREAM_SLOW_NANOSECONDS		(1L << 28)

// the biggest packet that the services, the balancer and its clients take
#define UPSTREAM_MAX_PACKET_SIZE		(1 << 25)

#define UPSTREAM_SERVICES				2
#define UPSTREAM_BALANCER_PORT			7000
#define UPSTREAM_FIRST_SERVICE_PORT		7001

typedef struct UpstreamIdentity {

	uint32_t id;
	uint32_t pid;

} UpstreamIdentity;

#endif
