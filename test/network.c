#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <fcntl.h>
#include <unistd.h>

#include <sys/socket.h>

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

	char ip[INET6_ADDRSTRLEN] = { 0 };
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

	test_network_ipv6_only ();

	(void) printf ("\nDone with NETWORK tests!\n\n");

	return 0;

}
