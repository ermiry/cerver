#ifndef _CERVER_NETWORK_H_
#define _CERVER_NETWORK_H_

#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "cerver/config.h"

#define IP_TO_STR_LEN       16
#define IPV6_TO_STR_LEN     46

typedef enum Protocol {

	PROTOCOL_TCP = IPPROTO_TCP,
	PROTOCOL_UDP = IPPROTO_UDP

} Protocol;

#define SOCK_SIZEOF_MEMBER(type, member) (sizeof(((type *) NULL)->member))

// gets the ip related to the hostname
// returns a newly allocated c string if found
// returns NULL on error or not found
CERVER_PUBLIC char *network_hostname_to_ip (
	const char *hostname
);

// enable/disable blocking on a socket
// true on success, false if there was an error
CERVER_PUBLIC bool sock_set_blocking (
	int32_t fd, bool blocking
);

CERVER_PUBLIC char *sock_ip_to_string (
	const struct sockaddr *address
);

CERVER_PUBLIC bool sock_ip_equal (
	const struct sockaddr *a, const struct sockaddr *b
);

CERVER_PUBLIC in_port_t sock_ip_port (
	const struct sockaddr *address
);

// sets a timeout (in seconds) for a socket
// the socket will still block until the timeout is completed
// if no data was read, a EAGAIN error is returned
// returns 0 on success, 1 on error
CERVER_PUBLIC int sock_set_timeout (
	int sock_fd, time_t timeout
);

#endif