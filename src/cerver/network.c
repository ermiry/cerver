#include <stdlib.h>
#include <string.h>

#include <netdb.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/time.h>

#include "cerver/network.h"

#pragma region resolve

static void network_address_set_port (
	struct sockaddr_storage *address, const in_port_t port
) {

	if (address->ss_family == AF_INET6) {
		((struct sockaddr_in6 *) address)->sin6_port = htons (port);
	}

	else {
		((struct sockaddr_in *) address)->sin_port = htons (port);
	}

}

// the first address of the preferred family, or the first one of any family
static const struct addrinfo *network_resolve_select (
	const struct addrinfo *results, const int preferred_family
) {

	const struct addrinfo *selected = NULL;

	for (const struct addrinfo *result = results; result && !selected; result = result->ai_next) {
		if (result->ai_family == preferred_family) selected = result;
	}

	return selected ? selected : results;

}

static unsigned int network_resolve (
	const char *host, const bool prefer_ipv6,
	struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	struct addrinfo hints;
	(void) memset (&hints, 0, sizeof (struct addrinfo));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;

	struct addrinfo *results = NULL;
	if (host && !getaddrinfo (host, NULL, &hints, &results) && results) {
		const struct addrinfo *selected = network_resolve_select (
			results, prefer_ipv6 ? AF_INET6 : AF_INET
		);

		(void) memset (address, 0, sizeof (struct sockaddr_storage));
		(void) memcpy (address, selected->ai_addr, selected->ai_addrlen);

		retval = 0;
	}

	if (results) freeaddrinfo (results);

	return retval;

}

// gets the ip related to the hostname, IPv4 or IPv6, with getaddrinfo () (thread safe)
// returns a newly allocated c string if found
// returns NULL on error or not found
char *network_hostname_to_ip (
	const char *hostname
) {

	char *retval = NULL;

	struct sockaddr_storage address;
	if (!network_resolve (hostname, false, &address)) {
		retval = sock_ip_to_string ((const struct sockaddr *) &address);
	}

	return retval;

}

// resolves a literal ip or a host name into an address with the port
// returns 0 on success, 1 if the host could not be resolved
unsigned int network_resolve_address (
	const char *host, const in_port_t port, const bool prefer_ipv6,
	struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	if (address && !network_resolve (host, prefer_ipv6, address)) {
		network_address_set_port (address, port);
		retval = 0;
	}

	return retval;

}

// parses a literal IPv4 or IPv6 address, without a port, into an address
// returns 0 on success, 1 if it is not a valid ip
unsigned int network_parse_address (
	const char *ip, const in_port_t port,
	struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	if (ip && address) {
		(void) memset (address, 0, sizeof (struct sockaddr_storage));

		struct sockaddr_in *ipv4 = (struct sockaddr_in *) address;
		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *) address;

		if (inet_pton (AF_INET, ip, &ipv4->sin_addr) == 1) {
			ipv4->sin_family = AF_INET;
			retval = 0;
		}

		else if (inet_pton (AF_INET6, ip, &ipv6->sin6_addr) == 1) {
			ipv6->sin6_family = AF_INET6;
			retval = 0;
		}

		if (!retval) network_address_set_port (address, port);
	}

	return retval;

}

// returns the size of the address of its family
socklen_t sock_address_size (const struct sockaddr_storage *address) {

	return (address->ss_family == AF_INET6) ?
		(socklen_t) sizeof (struct sockaddr_in6) : (socklen_t) sizeof (struct sockaddr_in);

}

// returns true if the address is an IPv4 inside an IPv6 address (::ffff:a.b.c.d)
bool sock_ip_is_mapped (const struct sockaddr *address) {

	return address
		&& (address->sa_family == AF_INET6)
		&& IN6_IS_ADDR_V4MAPPED (&((const struct sockaddr_in6 *) address)->sin6_addr);

}

// sets IPV6_V6ONLY in an IPv6 socket
// returns 0 on success, 1 on error
int sock_set_ipv6_only (int sock_fd, bool ipv6_only) {

	int value = ipv6_only ? 1 : 0;

	return setsockopt (
		sock_fd, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof (int)
	) ? 1 : 0;

}

#pragma endregion

// enable/disable blocking on a socket
// true on success, false if there was an error
bool sock_set_blocking (int32_t fd, bool blocking) {

	bool retval = false;

	if (fd >= 0) {
		int flags = fcntl (fd, F_GETFL, 0);
		if (flags >= 0) {
			flags = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);

			retval = (fcntl (fd, F_SETFL, flags) == 0) ? true : false;
		}
	}

	return retval;

}

// an IPv4 inside an IPv6 address is written as the IPv4,
// so a dual stack cerver logs and compares its IPv4 clients as IPv4
static void sock_ip_to_string_ipv6 (
	const struct sockaddr_in6 *address, char *ip_buffer
) {

	if (IN6_IS_ADDR_V4MAPPED (&address->sin6_addr)) {
		(void) inet_ntop (
			AF_INET,
			&address->sin6_addr.s6_addr[NETWORK_IPV4_MAPPED_OFFSET],
			ip_buffer, INET6_ADDRSTRLEN
		);
	}

	else {
		(void) inet_ntop (AF_INET6, &address->sin6_addr, ip_buffer, INET6_ADDRSTRLEN);
	}

}

unsigned int sock_ip_to_string_actual (
	const struct sockaddr *address,
	char *ip_buffer
) {

	unsigned int retval = 1;

	switch (address->sa_family) {
		case AF_INET:
			(void) inet_ntop (
				AF_INET,
				&((struct sockaddr_in *) address)->sin_addr,
				ip_buffer, INET6_ADDRSTRLEN
			);

			retval = 0;
			break;

		case AF_INET6:
			sock_ip_to_string_ipv6 ((const struct sockaddr_in6 *) address, ip_buffer);

			retval = 0;
			break;

		default: break;
	}

	return retval;

}

char *sock_ip_to_string (const struct sockaddr *address) {

	char *ip_buffer = NULL;

	if (address) {
		ip_buffer = (char *) calloc (INET6_ADDRSTRLEN, sizeof (char));
		if (ip_buffer) {
			if (sock_ip_to_string_actual (address, ip_buffer)) {
				free (ip_buffer);
				ip_buffer = NULL;
			}
		}
	}

	return ip_buffer;

}

bool sock_ip_equal (
	const struct sockaddr *a, const struct sockaddr *b
) {

	bool retval = false;

	if (a && b) {
		if (a->sa_family == b->sa_family) {
			switch (a->sa_family) {
				case AF_INET: {
					struct sockaddr_in *a_in = (struct sockaddr_in *) a;
					struct sockaddr_in *b_in = (struct sockaddr_in *) b;
					retval = a_in->sin_port == b_in->sin_port
						&& a_in->sin_addr.s_addr == b_in->sin_addr.s_addr;
				} break;

				case AF_INET6: {
					struct sockaddr_in6 *a_in6 = (struct sockaddr_in6 *) a;
					struct sockaddr_in6 *b_in6 = (struct sockaddr_in6 *) b;
					retval = a_in6->sin6_port == b_in6->sin6_port
						&& a_in6->sin6_flowinfo == b_in6->sin6_flowinfo
						&& memcmp (a_in6->sin6_addr.s6_addr, b_in6->sin6_addr.s6_addr,
								SOCK_SIZEOF_MEMBER (struct in6_addr, s6_addr)) == 0
						&& a_in6->sin6_scope_id == b_in6->sin6_scope_id;
				} break;

				default: break;
			}
		}
	}

	return retval;

}

in_port_t sock_ip_port (const struct sockaddr *address) {

	in_port_t retval = 0;

	if (address) {
		switch (address->sa_family) {
			case AF_INET:
				retval = ntohs (((struct sockaddr_in *) address)->sin_port);
				break;
			case AF_INET6:
				retval = ntohs (((struct sockaddr_in6 *) address)->sin6_port);
				break;

			default: break;
		}
	}

	return retval;

}

// sets a timeout (in seconds) for a socket
// the socket will still block until the timeout is completed
// if no data was read, a EAGAIN error is returned
// returns 0 on success, 1 on error
int sock_set_timeout (int sock_fd, time_t timeout) {

	struct timeval tv = { 0 };
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	return setsockopt (
		sock_fd,
		SOL_SOCKET, SO_RCVTIMEO,
		(const char *) &tv, sizeof (struct timeval)
	);

}

// sets a timeout (in seconds) for the socket's send operations
// a send that can not make progress in that time fails with EAGAIN
// returns 0 on success, 1 on error
int sock_set_send_timeout (int sock_fd, time_t timeout) {

	struct timeval tv = { 0 };
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	return setsockopt (
		sock_fd,
		SOL_SOCKET, SO_SNDTIMEO,
		(const char *) &tv, sizeof (struct timeval)
	) ? 1 : 0;

}

// sets the socket's reusable address option (SO_REUSEADDR)
// this should avoid errors when binding sockets
// returns 0 on success, 1 on any error
int sock_set_reusable (int sock_fd) {

	int reusable = 1;

	return setsockopt (
		sock_fd, SOL_SOCKET, SO_REUSEADDR, &reusable, sizeof (int)
	) ? 1 : 0;

}

// sets the socket's reusable port option (SO_REUSEPORT)
// other sockets can bind the same port, so the kernel balances them
// returns 0 on success, 1 on any error
int sock_set_reusable_port (int sock_fd) {

	int errors = 1;

	#ifdef SO_REUSEPORT
	int reusable = 1;

	errors = setsockopt (
		sock_fd, SOL_SOCKET, SO_REUSEPORT, &reusable, sizeof (int)
	) ? 1 : 0;
	#else
	(void) sock_fd;
	#endif

	return errors;

}