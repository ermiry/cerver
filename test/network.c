#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>

#include <net/if.h>
#include <netinet/in.h>

#include <cerver/network.h>

#include "test.h"

#define NETWORK_BAD_FD			-1

// sock_set_blocking () had its flags inverted,
// the poll cerver called it with true to get a non blocking socket
static void test_network_blocking (void) {

	int fd = socket (AF_INET, SOCK_STREAM, 0);
	test_check (fd > NETWORK_BAD_FD, NULL);

	test_check_true (sock_set_blocking (fd, false));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) != 0, "the socket is not non blocking");

	test_check_true (sock_set_blocking (fd, true));
	test_check ((fcntl (fd, F_GETFL, 0) & O_NONBLOCK) == 0, "the socket is not blocking");

	test_check_false (sock_set_blocking (NETWORK_BAD_FD, true));

	(void) close (fd);

}

#define NETWORK_TEST_PORT				7777
#define NETWORK_TEST_IPV4				"10.0.0.5"
#define NETWORK_TEST_IPV6				"fd00::5"
#define NETWORK_TEST_MAPPED				"::ffff:10.0.0.5"
#define NETWORK_TEST_LOOPBACK			"127.0.0.1"
#define NETWORK_TEST_LOOPBACK_IPV6		"::1"
#define NETWORK_TEST_LOCALHOST			"localhost"
#define NETWORK_TEST_INVALID			"10.0.0.256"
#define NETWORK_TEST_UNKNOWN_HOST		"cerver-unknown.invalid"

static void test_network_parse_address (void) {

	struct sockaddr_storage address;

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV4, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_int_eq (address.ss_family, AF_INET, NULL);
	test_check_unsigned_eq (sock_ip_port ((struct sockaddr *) &address), NETWORK_TEST_PORT, NULL);
	test_check_unsigned_eq (sock_address_size (&address), sizeof (struct sockaddr_in), NULL);

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV6, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_int_eq (address.ss_family, AF_INET6, NULL);
	test_check_unsigned_eq (sock_ip_port ((struct sockaddr *) &address), NETWORK_TEST_PORT, NULL);
	test_check_unsigned_eq (sock_address_size (&address), sizeof (struct sockaddr_in6), NULL);

	// a host name is not a literal ip
	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_INVALID, NETWORK_TEST_PORT, &address), 1, NULL);
	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_LOCALHOST, NETWORK_TEST_PORT, &address), 1, NULL);
	test_check_unsigned_eq (network_parse_address (NULL, NETWORK_TEST_PORT, &address), 1, NULL);

}

// an IPv4 inside an IPv6 address is written as the IPv4
static void test_network_mapped (void) {

	char ip[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	struct sockaddr_storage address;

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_MAPPED, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_true (sock_ip_is_mapped ((struct sockaddr *) &address));
	test_check_unsigned_eq (sock_ip_to_string_actual ((struct sockaddr *) &address, ip), 0, NULL);
	test_check_str_eq (ip, NETWORK_TEST_IPV4, NULL);

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV6, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_false (sock_ip_is_mapped ((struct sockaddr *) &address));
	test_check_unsigned_eq (sock_ip_to_string_actual ((struct sockaddr *) &address, ip), 0, NULL);
	test_check_str_eq (ip, NETWORK_TEST_IPV6, NULL);

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV4, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_false (sock_ip_is_mapped ((struct sockaddr *) &address));
	test_check_false (sock_ip_is_mapped (NULL));

}

static void test_network_resolve (void) {

	struct sockaddr_storage address;

	// a literal ip resolves to itself
	test_check_unsigned_eq (
		network_resolve_address (NETWORK_TEST_LOOPBACK, NETWORK_TEST_PORT, false, &address), 0, NULL
	);

	test_check_int_eq (address.ss_family, AF_INET, NULL);
	test_check_unsigned_eq (sock_ip_port ((struct sockaddr *) &address), NETWORK_TEST_PORT, NULL);

	// the family comes from the address, not from the preference
	test_check_unsigned_eq (
		network_resolve_address (NETWORK_TEST_LOOPBACK_IPV6, NETWORK_TEST_PORT, false, &address), 0, NULL
	);

	test_check_int_eq (address.ss_family, AF_INET6, NULL);

	// a host name
	test_check_unsigned_eq (
		network_resolve_address (NETWORK_TEST_LOCALHOST, NETWORK_TEST_PORT, false, &address), 0, NULL
	);

	test_check_unsigned_eq (sock_ip_port ((struct sockaddr *) &address), NETWORK_TEST_PORT, NULL);

	test_check_unsigned_eq (
		network_resolve_address (NETWORK_TEST_UNKNOWN_HOST, NETWORK_TEST_PORT, false, &address), 1, NULL
	);

	test_check_unsigned_eq (
		network_resolve_address (NETWORK_TEST_INVALID, NETWORK_TEST_PORT, false, &address), 1, NULL
	);

	char *ip = network_hostname_to_ip (NETWORK_TEST_LOOPBACK);
	test_check_ptr (ip);
	test_check_str_eq (ip, NETWORK_TEST_LOOPBACK, NULL);
	free (ip);

	test_check_null_ptr (network_hostname_to_ip (NETWORK_TEST_UNKNOWN_HOST));

}

#define NETWORK_TEST_LINK_LOCAL				"fe80::1"
#define NETWORK_TEST_LOOPBACK_INTERFACE		"lo"
#define NETWORK_TEST_UNKNOWN_INTERFACE		"cerver0"
#define NETWORK_TEST_ZONE_FORMAT			"%s%c%s"
#define NETWORK_TEST_ZONE_INDEX_FORMAT		"%s%c%u"

static void network_test_zone (
	char *buffer, const char *ip, const char *zone
) {

	(void) snprintf (
		buffer, NETWORK_ADDRESS_STRING_SIZE, NETWORK_TEST_ZONE_FORMAT,
		ip, NETWORK_ZONE_SEPARATOR, zone
	);

}

// a link local address with its zone, as an interface name or as its index
static void test_network_zone (void) {

	const unsigned int loopback_index = if_nametoindex (NETWORK_TEST_LOOPBACK_INTERFACE);
	test_check (loopback_index > 0, "the loopback interface has no index");

	char text[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	char ip[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	struct sockaddr_storage address;
	const struct sockaddr_in6 *ipv6 = (const struct sockaddr_in6 *) &address;

	network_test_zone (text, NETWORK_TEST_LINK_LOCAL, NETWORK_TEST_LOOPBACK_INTERFACE);
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_int_eq (address.ss_family, AF_INET6, NULL);
	test_check_unsigned_eq (ipv6->sin6_scope_id, loopback_index, NULL);
	test_check_unsigned_eq (sock_ip_port ((struct sockaddr *) &address), NETWORK_TEST_PORT, NULL);

	// the zone is written back with the interface name
	test_check_unsigned_eq (sock_ip_to_string_actual ((struct sockaddr *) &address, ip), 0, NULL);
	test_check_str_eq (ip, text, NULL);

	(void) snprintf (
		text, NETWORK_ADDRESS_STRING_SIZE, NETWORK_TEST_ZONE_INDEX_FORMAT,
		NETWORK_TEST_LINK_LOCAL, NETWORK_ZONE_SEPARATOR, loopback_index
	);

	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 0, NULL);
	test_check_unsigned_eq (ipv6->sin6_scope_id, loopback_index, NULL);

	// a literal with a zone resolves to itself
	NetworkAddresses addresses;
	test_check_unsigned_eq (
		network_resolve_addresses (text, NETWORK_TEST_PORT, false, PROTOCOL_TCP, &addresses), 0, NULL
	);

	test_check_unsigned_eq (addresses.count, 1, NULL);
	test_check_unsigned_eq (
		((const struct sockaddr_in6 *) &addresses.addresses[0])->sin6_scope_id, loopback_index, NULL
	);

	// a zone only belongs to a link local IPv6 address
	network_test_zone (text, NETWORK_TEST_IPV6, NETWORK_TEST_LOOPBACK_INTERFACE);
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 1, NULL);

	network_test_zone (text, NETWORK_TEST_IPV4, NETWORK_TEST_LOOPBACK_INTERFACE);
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 1, NULL);

	// an interface that does not exist, an empty zone and an index of 0
	network_test_zone (text, NETWORK_TEST_LINK_LOCAL, NETWORK_TEST_UNKNOWN_INTERFACE);
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 1, NULL);

	network_test_zone (text, NETWORK_TEST_LINK_LOCAL, "");
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 1, NULL);

	network_test_zone (text, NETWORK_TEST_LINK_LOCAL, "0");
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &address), 1, NULL);

}

// localhost resolves to 127.0.0.1 and ::1,
// one of each family in turn, starting with the preferred one
static void test_network_resolve_addresses (void) {

	NetworkAddresses addresses;

	test_check_unsigned_eq (
		network_resolve_addresses (NETWORK_TEST_LOCALHOST, NETWORK_TEST_PORT, false, PROTOCOL_TCP, &addresses),
		0, NULL
	);

	test_check (addresses.count >= 2, "localhost did not resolve to both families");
	test_check_int_eq (addresses.addresses[0].ss_family, AF_INET, NULL);
	test_check_int_eq (addresses.addresses[1].ss_family, AF_INET6, NULL);

	for (unsigned int index = 0; index < addresses.count; index++) {
		test_check_unsigned_eq (
			sock_ip_port ((struct sockaddr *) &addresses.addresses[index]), NETWORK_TEST_PORT, NULL
		);
	}

	test_check_unsigned_eq (
		network_resolve_addresses (NETWORK_TEST_LOCALHOST, NETWORK_TEST_PORT, true, PROTOCOL_UDP, &addresses),
		0, NULL
	);

	test_check (addresses.count >= 2, "localhost did not resolve to both families");
	test_check_int_eq (addresses.addresses[0].ss_family, AF_INET6, NULL);
	test_check_int_eq (addresses.addresses[1].ss_family, AF_INET, NULL);

	test_check_unsigned_eq (
		network_resolve_addresses (NETWORK_TEST_LOOPBACK, NETWORK_TEST_PORT, true, PROTOCOL_TCP, &addresses),
		0, NULL
	);

	test_check_unsigned_eq (addresses.count, 1, NULL);
	test_check_int_eq (addresses.addresses[0].ss_family, AF_INET, NULL);

	test_check_unsigned_eq (
		network_resolve_addresses (NETWORK_TEST_UNKNOWN_HOST, NETWORK_TEST_PORT, false, PROTOCOL_TCP, &addresses),
		1, NULL
	);

	test_check_unsigned_eq (
		network_resolve_addresses (NULL, NETWORK_TEST_PORT, false, PROTOCOL_TCP, &addresses), 1, NULL
	);

}

#define NETWORK_TEST_OTHER_PORT			7778
#define NETWORK_TEST_FLOW_INFO			1

// sock_ip_equal () required the same family, so an IPv4 client of a dual stack cerver
// was never equal to its own IPv4 address, and it compared the flow info
static void test_network_ip_equal (void) {

	struct sockaddr_storage ipv4;
	struct sockaddr_storage mapped;
	struct sockaddr_storage other;

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV4, NETWORK_TEST_PORT, &ipv4), 0, NULL);
	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_MAPPED, NETWORK_TEST_PORT, &mapped), 0, NULL);

	test_check_true (sock_ip_equal ((struct sockaddr *) &ipv4, (struct sockaddr *) &mapped));
	test_check_true (sock_ip_equal ((struct sockaddr *) &mapped, (struct sockaddr *) &ipv4));

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_MAPPED, NETWORK_TEST_OTHER_PORT, &other), 0, NULL);
	test_check_false (sock_ip_equal ((struct sockaddr *) &ipv4, (struct sockaddr *) &other));

	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV6, NETWORK_TEST_PORT, &mapped), 0, NULL);
	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_IPV6, NETWORK_TEST_PORT, &other), 0, NULL);
	((struct sockaddr_in6 *) &other)->sin6_flowinfo = NETWORK_TEST_FLOW_INFO;
	test_check_true (sock_ip_equal ((struct sockaddr *) &mapped, (struct sockaddr *) &other));

	test_check_false (sock_ip_equal ((struct sockaddr *) &ipv4, (struct sockaddr *) &mapped));

	// the same link local address in two zones
	char text[NETWORK_ADDRESS_STRING_SIZE] = { 0 };
	network_test_zone (text, NETWORK_TEST_LINK_LOCAL, NETWORK_TEST_LOOPBACK_INTERFACE);
	test_check_unsigned_eq (network_parse_address (text, NETWORK_TEST_PORT, &mapped), 0, NULL);
	test_check_unsigned_eq (network_parse_address (NETWORK_TEST_LINK_LOCAL, NETWORK_TEST_PORT, &other), 0, NULL);
	test_check_false (sock_ip_equal ((struct sockaddr *) &mapped, (struct sockaddr *) &other));

	test_check_false (sock_ip_equal (NULL, (struct sockaddr *) &other));

}

static void test_network_ipv6_only (void) {

	int fd = socket (AF_INET6, SOCK_STREAM, 0);
	test_check (fd > NETWORK_BAD_FD, NULL);

	int value = 0;
	socklen_t size = sizeof (int);

	test_check_int_eq (sock_set_ipv6_only (fd, true), 0, NULL);
	test_check_int_eq (getsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &value, &size), 0, NULL);
	test_check_int_eq (value, 1, NULL);

	test_check_int_eq (sock_set_ipv6_only (fd, false), 0, NULL);
	test_check_int_eq (getsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &value, &size), 0, NULL);
	test_check_int_eq (value, 0, NULL);

	(void) close (fd);

	test_check_int_eq (sock_set_ipv6_only (NETWORK_BAD_FD, true), 1, NULL);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) printf ("Testing NETWORK...\n");

	test_network_blocking ();

	test_network_parse_address ();

	test_network_mapped ();

	test_network_resolve ();

	test_network_zone ();

	test_network_resolve_addresses ();

	test_network_ip_equal ();

	test_network_ipv6_only ();

	(void) printf ("\nDone with NETWORK tests!\n\n");

	return 0;

}
