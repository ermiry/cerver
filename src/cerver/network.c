#include <stdlib.h>
#include <string.h>

#include <netdb.h>

#include <arpa/inet.h>

#include <sys/socket.h>
#include <sys/time.h>

#include "cerver/network.h"

// gets the ip related to the hostname
// returns a newly allocated c string if found
// returns NULL on error or not found
char *network_hostname_to_ip (
	const char *hostname
) {

	char *retval = NULL;

	struct hostent *he = gethostbyname (hostname);
	if (he) {
		struct in_addr **addr_list = (struct in_addr **) he->h_addr_list;
		if (addr_list) {
			if (inet_ntoa (*addr_list[0])) {
				retval = strdup (inet_ntoa (*addr_list[0]));
			}
		}
	}

	return retval;

}

// enable/disable blocking on a socket
// true on success, false if there was an error
bool sock_set_blocking (int32_t fd, bool blocking) {

	bool retval = false;

	if (fd >= 0) {
		int flags = fcntl (fd, F_GETFL, 0);
		if (flags >= 0) {
			// flags = isBlocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
			flags = blocking ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);

			retval = (fcntl (fd, F_SETFL, flags) == 0) ? true : false;
		}
	}

	return retval;

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
			(void) inet_ntop (
				AF_INET6,
				&((struct sockaddr_in6 *) address)->sin6_addr,
				ip_buffer, INET6_ADDRSTRLEN
			);

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
						&& memcmp(a_in6->sin6_addr.s6_addr, b_in6->sin6_addr.s6_addr,
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

// sets the socket's reusable options
// this should avoid errors when binding sockets
// returns 0 on success, 1 on any error
int sock_set_reusable (int sock_fd) {

	int errors = 0;

	int reusable = 1;

	errors |= setsockopt (sock_fd, SOL_SOCKET, SO_REUSEADDR, &reusable, sizeof (int));

	#ifdef SO_REUSEPORT
	errors |= setsockopt (sock_fd, SOL_SOCKET, SO_REUSEPORT, &reusable, sizeof (int));
	#endif

	return errors;

}