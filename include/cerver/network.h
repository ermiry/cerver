#ifndef _CERVER_NETWORK_H_
#define _CERVER_NETWORK_H_

#include <stdbool.h>

#include <unistd.h>
#include <fcntl.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "cerver/config.h"

#define IP_TO_STR_LEN					16
#define IPV6_TO_STR_LEN					46

#define MAX_PORT_NUM					65535
#define MAX_UDP_PACKET_SIZE				65515

// an IPv4 inside an IPv6 address (::ffff:a.b.c.d) starts after these bytes
#define NETWORK_IPV4_MAPPED_OFFSET		12

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Protocol {

	PROTOCOL_TCP = IPPROTO_TCP,
	PROTOCOL_UDP = IPPROTO_UDP

} Protocol;

#define SOCK_SIZEOF_MEMBER(type, member) (sizeof (((type *) NULL)->member))

// gets the ip related to the hostname, IPv4 or IPv6, with getaddrinfo () (thread safe)
// returns a newly allocated c string if found
// returns NULL on error or not found
CERVER_PUBLIC char *network_hostname_to_ip (
	const char *hostname
);

// resolves a literal ip or a host name into an address with the port
// an address of the family of prefer_ipv6 is taken first, and any other one if there is not one
// returns 0 on success, 1 if the host could not be resolved
CERVER_PUBLIC unsigned int network_resolve_address (
	const char *host, const in_port_t port, const bool prefer_ipv6,
	struct sockaddr_storage *address
);

// parses a literal IPv4 or IPv6 address, without a port, into an address
// returns 0 on success, 1 if it is not a valid ip
CERVER_PUBLIC unsigned int network_parse_address (
	const char *ip, const in_port_t port,
	struct sockaddr_storage *address
);

// returns the size of the address of its family, the size that bind () and connect () expect
CERVER_PUBLIC socklen_t sock_address_size (const struct sockaddr_storage *address);

// returns true if the address is an IPv4 inside an IPv6 address (::ffff:a.b.c.d)
CERVER_PUBLIC bool sock_ip_is_mapped (const struct sockaddr *address);

// sets IPV6_V6ONLY in an IPv6 socket, false accepts IPv4 connections too (dual stack)
// returns 0 on success, 1 on error
CERVER_PUBLIC int sock_set_ipv6_only (int sock_fd, bool ipv6_only);

// enable/disable blocking on a socket
// blocking = false sets O_NONBLOCK
// true on success, false if there was an error
CERVER_PUBLIC bool sock_set_blocking (
	int32_t fd, bool blocking
);

// writes the ip of the address in a buffer of at least INET6_ADDRSTRLEN bytes
// an IPv4 inside an IPv6 address is written as the IPv4 (a.b.c.d)
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int sock_ip_to_string_actual (
	const struct sockaddr *address,
	char *ip_buffer
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

// sets a timeout (in seconds) for the socket's send operations
// a send that can not make progress in that time fails with EAGAIN
// returns 0 on success, 1 on error
CERVER_PUBLIC int sock_set_send_timeout (
	int sock_fd, time_t timeout
);

// sets the socket's reusable address option (SO_REUSEADDR)
// this should avoid errors when binding sockets
// returns 0 on success, 1 on any error
CERVER_PUBLIC int sock_set_reusable (int sock_fd);

// sets the socket's reusable port option (SO_REUSEPORT)
// other sockets can bind the same port, so the kernel balances them
// returns 0 on success, 1 on any error
CERVER_PUBLIC int sock_set_reusable_port (int sock_fd);

#ifdef __cplusplus
}
#endif

#endif