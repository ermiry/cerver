#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include <netdb.h>

#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>

#include <sys/socket.h>
#include <sys/time.h>

#include "cerver/network.h"

// the base of a zone given as an interface index (fe80::1%2)
#define NETWORK_ZONE_INDEX_BASE			10

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

static int network_resolve_socket_type (const int protocol) {

	return (protocol == IPPROTO_UDP) ? SOCK_DGRAM : SOCK_STREAM;

}

// copies the addresses of one family from the results, keeping their order
// returns how many addresses were copied
static unsigned int network_resolve_collect (
	const struct addrinfo *results, const int family,
	struct sockaddr_storage *collected
) {

	unsigned int count = 0;

	for (
		const struct addrinfo *result = results;
		result && (count < NETWORK_MAX_RESOLVED_ADDRESSES);
		result = result->ai_next
	) {
		if (
			(result->ai_family == family)
			&& (result->ai_addrlen <= (socklen_t) sizeof (struct sockaddr_storage))
		) {
			(void) memset (&collected[count], 0, sizeof (struct sockaddr_storage));
			(void) memcpy (&collected[count], result->ai_addr, result->ai_addrlen);
			count += 1;
		}
	}

	return count;

}

// one address of each family in turn, starting with the first family,
// so an unreachable family only delays the connection by one attempt
static void network_resolve_interleave (
	const struct sockaddr_storage *first, const unsigned int first_count,
	const struct sockaddr_storage *second, const unsigned int second_count,
	NetworkAddresses *addresses
) {

	unsigned int first_index = 0;
	unsigned int second_index = 0;

	addresses->count = 0;
	while (
		((first_index < first_count) || (second_index < second_count))
		&& (addresses->count < NETWORK_MAX_RESOLVED_ADDRESSES)
	) {
		if (first_index < first_count) {
			addresses->addresses[addresses->count] = first[first_index];
			addresses->count += 1;
			first_index += 1;
		}

		if ((second_index < second_count) && (addresses->count < NETWORK_MAX_RESOLVED_ADDRESSES)) {
			addresses->addresses[addresses->count] = second[second_index];
			addresses->count += 1;
			second_index += 1;
		}
	}

}

static void network_resolve_order (
	const struct addrinfo *results, const bool prefer_ipv6,
	NetworkAddresses *addresses
) {

	struct sockaddr_storage ipv6[NETWORK_MAX_RESOLVED_ADDRESSES];
	struct sockaddr_storage ipv4[NETWORK_MAX_RESOLVED_ADDRESSES];

	const unsigned int ipv6_count = network_resolve_collect (results, AF_INET6, ipv6);
	const unsigned int ipv4_count = network_resolve_collect (results, AF_INET, ipv4);

	if (prefer_ipv6) {
		network_resolve_interleave (ipv6, ipv6_count, ipv4, ipv4_count, addresses);
	}

	else {
		network_resolve_interleave (ipv4, ipv4_count, ipv6, ipv6_count, addresses);
	}

}

// resolves a literal ip or a host name with getaddrinfo (),
// a literal ip is parsed first so its zone is validated the same way as a bind address
// returns 0 on success, 1 if the host could not be resolved
static unsigned int network_resolve_lookup (
	const char *host, const bool prefer_ipv6, const int protocol,
	NetworkAddresses *addresses
) {

	unsigned int retval = 1;

	addresses->count = 0;

	if (!network_parse_address (host, 0, &addresses->addresses[0])) {
		addresses->count = 1;
		retval = 0;
	}

	else {
		struct addrinfo hints;
		(void) memset (&hints, 0, sizeof (struct addrinfo));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = network_resolve_socket_type (protocol);

		struct addrinfo *results = NULL;
		if (!getaddrinfo (host, NULL, &hints, &results) && results) {
			network_resolve_order (results, prefer_ipv6, addresses);
			retval = addresses->count ? 0 : 1;
		}

		if (results) freeaddrinfo (results);
	}

	return retval;

}

// resolves a literal ip (with its zone) or a host name into every address of the protocol,
// alternating the families and starting with the one of prefer_ipv6,
// each family keeps the order of getaddrinfo () (RFC 6724)
// returns 0 on success, 1 if the host could not be resolved
unsigned int network_resolve_addresses (
	const char *host, const in_port_t port,
	const bool prefer_ipv6, const int protocol,
	NetworkAddresses *addresses
) {

	unsigned int retval = 1;

	if (host && addresses) {
		retval = network_resolve_lookup (host, prefer_ipv6, protocol, addresses);

		for (unsigned int index = 0; index < addresses->count; index++) {
			network_address_set_port (&addresses->addresses[index], port);
		}
	}

	return retval;

}

// the first address of the preferred family, or the first one of any family
static unsigned int network_resolve (
	const char *host, const bool prefer_ipv6,
	struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	NetworkAddresses addresses;
	if (!network_resolve_addresses (host, 0, prefer_ipv6, IPPROTO_TCP, &addresses)) {
		*address = addresses.addresses[0];
		retval = 0;
	}

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

// a zone only means something for a link local address
static bool network_address_is_link_local (const struct in6_addr *address) {

	return IN6_IS_ADDR_LINKLOCAL (address) || IN6_IS_ADDR_MC_LINKLOCAL (address);

}

static bool network_zone_is_index (const char *zone) {

	bool is_index = (zone[0] != '\0');

	for (const char *current = zone; *current && is_index; current++) {
		is_index = isdigit ((unsigned char) *current) ? true : false;
	}

	return is_index;

}

// a zone is an interface name (eth0) or its index (2)
// returns 0 on success, 1 if there is not an interface with that zone
static unsigned int network_parse_zone (const char *zone, uint32_t *scope_id) {

	unsigned int retval = 1;

	if (network_zone_is_index (zone)) {
		char *end = NULL;
		const unsigned long index = strtoul (zone, &end, NETWORK_ZONE_INDEX_BASE);
		if (end && !*end && index && (index <= UINT32_MAX)) {
			*scope_id = (uint32_t) index;
			retval = 0;
		}
	}

	else if (zone[0] && (strlen (zone) < IF_NAMESIZE)) {
		const unsigned int index = if_nametoindex (zone);
		if (index) {
			*scope_id = (uint32_t) index;
			retval = 0;
		}
	}

	return retval;

}

// a link local IPv6 address with its zone (fe80::1%eth0)
// returns 0 on success, 1 if it is not a valid address with a zone
static unsigned int network_parse_address_zone (
	const char *ip, const char *separator,
	struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	const size_t length = (size_t) (separator - ip);
	if (length < INET6_ADDRSTRLEN) {
		char literal[INET6_ADDRSTRLEN] = { 0 };
		(void) memcpy (literal, ip, length);

		struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *) address;
		uint32_t scope_id = 0;
		if (
			(inet_pton (AF_INET6, literal, &ipv6->sin6_addr) == 1)
			&& network_address_is_link_local (&ipv6->sin6_addr)
			&& !network_parse_zone (separator + 1, &scope_id)
		) {
			ipv6->sin6_family = AF_INET6;
			ipv6->sin6_scope_id = scope_id;
			retval = 0;
		}
	}

	if (retval) (void) memset (address, 0, sizeof (struct sockaddr_storage));

	return retval;

}

// a literal IPv4 or IPv6 address without a zone
// returns 0 on success, 1 if it is not a valid ip
static unsigned int network_parse_address_literal (
	const char *ip, struct sockaddr_storage *address
) {

	unsigned int retval = 1;

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

	return retval;

}

// parses a literal IPv4 or IPv6 address, without a port, into an address
// a link local IPv6 address can have its zone, as an interface name or index (fe80::1%eth0)
// returns 0 on success, 1 if it is not a valid ip
unsigned int network_parse_address (
	const char *ip, const in_port_t port,
	struct sockaddr_storage *address
) {

	unsigned int retval = 1;

	if (ip && address) {
		(void) memset (address, 0, sizeof (struct sockaddr_storage));

		const char *separator = strchr (ip, NETWORK_ZONE_SEPARATOR);
		retval = separator ?
			network_parse_address_zone (ip, separator, address) :
			network_parse_address_literal (ip, address);

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

// the zone of an address with a scope id: its interface name, or its index
static void sock_ip_to_string_zone (
	const struct sockaddr_in6 *address, char *ip_buffer
) {

	const size_t length = strlen (ip_buffer);
	char interface[IF_NAMESIZE] = { 0 };

	if (if_indextoname (address->sin6_scope_id, interface)) {
		(void) snprintf (
			ip_buffer + length, NETWORK_ADDRESS_STRING_SIZE - length,
			"%c%s", NETWORK_ZONE_SEPARATOR, interface
		);
	}

	else {
		(void) snprintf (
			ip_buffer + length, NETWORK_ADDRESS_STRING_SIZE - length,
			"%c%u", NETWORK_ZONE_SEPARATOR, (unsigned int) address->sin6_scope_id
		);
	}

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

		if (address->sin6_scope_id) sock_ip_to_string_zone (address, ip_buffer);
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
		ip_buffer = (char *) calloc (NETWORK_ADDRESS_STRING_SIZE, sizeof (char));
		if (ip_buffer) {
			if (sock_ip_to_string_actual (address, ip_buffer)) {
				free (ip_buffer);
				ip_buffer = NULL;
			}
		}
	}

	return ip_buffer;

}

// an IPv4 inside an IPv6 address becomes the IPv4, so both forms compare equal
static void sock_ip_normalize (
	const struct sockaddr *address, struct sockaddr_storage *normalized
) {

	(void) memset (normalized, 0, sizeof (struct sockaddr_storage));

	if (sock_ip_is_mapped (address)) {
		const struct sockaddr_in6 *mapped = (const struct sockaddr_in6 *) address;
		struct sockaddr_in *ipv4 = (struct sockaddr_in *) normalized;

		ipv4->sin_family = AF_INET;
		ipv4->sin_port = mapped->sin6_port;
		(void) memcpy (
			&ipv4->sin_addr,
			&mapped->sin6_addr.s6_addr[NETWORK_IPV4_MAPPED_OFFSET],
			sizeof (struct in_addr)
		);
	}

	else if (address->sa_family == AF_INET6) {
		(void) memcpy (normalized, address, sizeof (struct sockaddr_in6));
	}

	else if (address->sa_family == AF_INET) {
		(void) memcpy (normalized, address, sizeof (struct sockaddr_in));
	}

}

// the flow info is not part of the identity of an address
static bool sock_ip_equal_normalized (
	const struct sockaddr_storage *a, const struct sockaddr_storage *b
) {

	bool retval = false;

	if (a->ss_family == b->ss_family) {
		switch (a->ss_family) {
			case AF_INET: {
				const struct sockaddr_in *a_in = (const struct sockaddr_in *) a;
				const struct sockaddr_in *b_in = (const struct sockaddr_in *) b;
				retval = (a_in->sin_port == b_in->sin_port)
					&& (a_in->sin_addr.s_addr == b_in->sin_addr.s_addr);
			} break;

			case AF_INET6: {
				const struct sockaddr_in6 *a_in6 = (const struct sockaddr_in6 *) a;
				const struct sockaddr_in6 *b_in6 = (const struct sockaddr_in6 *) b;
				retval = (a_in6->sin6_port == b_in6->sin6_port)
					&& !memcmp (
						a_in6->sin6_addr.s6_addr, b_in6->sin6_addr.s6_addr,
						SOCK_SIZEOF_MEMBER (struct in6_addr, s6_addr)
					)
					&& (a_in6->sin6_scope_id == b_in6->sin6_scope_id);
			} break;

			default: break;
		}
	}

	return retval;

}

// true if both addresses have the same ip, port and zone,
// an IPv4 inside an IPv6 address (::ffff:a.b.c.d) is equal to the IPv4
bool sock_ip_equal (
	const struct sockaddr *a, const struct sockaddr *b
) {

	bool retval = false;

	if (a && b) {
		struct sockaddr_storage a_normalized;
		struct sockaddr_storage b_normalized;

		sock_ip_normalize (a, &a_normalized);
		sock_ip_normalize (b, &b_normalized);

		retval = sock_ip_equal_normalized (&a_normalized, &b_normalized);
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
