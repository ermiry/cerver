#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/http/headers.h>
#include <cerver/http/http.h>
#include <cerver/http/proxy.h>
#include <cerver/http/request.h>

#include "../test.h"

static const char *test_proxy_nginx = { "172.18.0.5" };
static const char *test_proxy_client = { "203.0.113.7" };
static const char *test_proxy_outside = { "198.51.100.20" };

static bool test_contains (const char *cidr, const char *ip) {

	HttpProxyNetwork network;
	test_check_true (http_proxy_network_parse (cidr, &network));

	return http_proxy_network_contains (&network, ip, strlen (ip));

}

static void test_proxy_network_parse (void) {

	HttpProxyNetwork network;

	test_check_true (http_proxy_network_parse ("10.0.0.0/8", &network));
	test_check_unsigned_eq (network.prefix, 8, NULL);

	// an address without a prefix is a single host
	test_check_true (http_proxy_network_parse ("192.168.1.1", &network));
	test_check_unsigned_eq (network.prefix, 32, NULL);

	test_check_true (http_proxy_network_parse ("fc00::/7", &network));
	test_check_unsigned_eq (network.prefix, 7, NULL);

	test_check_true (http_proxy_network_parse ("::1", &network));
	test_check_unsigned_eq (network.prefix, 128, NULL);

	test_check_true (http_proxy_network_parse ("0.0.0.0/0", &network));
	test_check_unsigned_eq (network.prefix, 0, NULL);

	static const char *invalid[] = {
		"10.0.0.0/33", "fc00::/129", "10.0.0.0/", "10.0.0.0/-1", "10.0.0.0/8x",
		"10.0.0", "256.0.0.1", "nginx", "", "/8",
		"10.0.0.0/8/8", "fc00:::/7"
	};

	for (size_t i = 0; i < (sizeof (invalid) / sizeof (const char *)); i++) {
		test_check_false (http_proxy_network_parse (invalid[i], &network));
	}

	test_check_false (http_proxy_network_parse (NULL, &network));
	test_check_false (http_proxy_network_parse ("10.0.0.0/8", NULL));

}

static void test_proxy_network_contains (void) {

	test_check_true (test_contains ("172.16.0.0/12", test_proxy_nginx));
	test_check_true (test_contains ("172.16.0.0/12", "172.31.255.255"));
	test_check_false (test_contains ("172.16.0.0/12", "172.32.0.0"));
	test_check_false (test_contains ("172.16.0.0/12", "172.15.255.255"));

	test_check_true (test_contains ("10.1.2.3", "10.1.2.3"));
	test_check_false (test_contains ("10.1.2.3", "10.1.2.4"));

	test_check_true (test_contains ("0.0.0.0/0", test_proxy_client));

	test_check_true (test_contains ("fc00::/7", "fd12:3456::1"));
	test_check_false (test_contains ("fc00::/7", "fe80::1"));
	test_check_true (test_contains ("2001:db8::/32", "2001:db8:ffff::1"));

	// an IPv4 inside an IPv6 is an IPv4
	test_check_true (test_contains ("10.0.0.0/8", "::ffff:10.20.30.40"));

	// the families are not mixed
	test_check_false (test_contains ("::/0", test_proxy_client));
	test_check_false (test_contains ("0.0.0.0/0", "2001:db8::1"));

	// invalid addresses
	test_check_false (test_contains ("0.0.0.0/0", "unknown"));
	test_check_false (test_contains ("0.0.0.0/0", ""));

}

static HttpCerver *test_proxy_cerver (const HttpTrustedProxies trusted_proxies) {

	HttpCerver *http_cerver = http_cerver_create (NULL);
	test_check_ptr (http_cerver);

	http_cerver_set_trusted_proxies (http_cerver, trusted_proxies);

	return http_cerver;

}

static bool test_trusted (const HttpCerver *http_cerver, const char *ip) {

	return http_proxy_is_trusted (http_cerver, ip, strlen (ip));

}

static void test_proxy_is_trusted (void) {

	HttpCerver *http_cerver = test_proxy_cerver (HTTP_TRUSTED_PROXIES_NONE);
	test_check_false (test_trusted (http_cerver, test_proxy_nginx));
	test_check_false (test_trusted (http_cerver, "127.0.0.1"));

	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);
	test_check_true (test_trusted (http_cerver, test_proxy_nginx));
	test_check_true (test_trusted (http_cerver, "127.0.0.1"));
	test_check_true (test_trusted (http_cerver, "10.8.0.1"));
	test_check_true (test_trusted (http_cerver, "192.168.0.10"));
	test_check_true (test_trusted (http_cerver, "::1"));
	test_check_true (test_trusted (http_cerver, "fd00::5"));
	test_check_true (test_trusted (http_cerver, "::ffff:192.168.0.10"));
	test_check_false (test_trusted (http_cerver, test_proxy_client));
	test_check_false (test_trusted (http_cerver, "2001:db8::1"));

	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_LIST);
	test_check_false (test_trusted (http_cerver, test_proxy_nginx));
	test_check_unsigned_eq (http_cerver_add_trusted_proxy (http_cerver, "172.18.0.0/16"), 0, NULL);
	test_check_unsigned_eq (http_cerver_add_trusted_proxy (http_cerver, test_proxy_outside), 0, NULL);
	test_check_true (test_trusted (http_cerver, test_proxy_nginx));
	test_check_true (test_trusted (http_cerver, test_proxy_outside));
	test_check_false (test_trusted (http_cerver, "172.19.0.1"));
	test_check_false (test_trusted (http_cerver, "127.0.0.1"));

	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_ALL);
	test_check_true (test_trusted (http_cerver, test_proxy_client));
	test_check_false (test_trusted (http_cerver, "not an address"));

	http_cerver_delete (http_cerver);

}

// an invalid network is a configuration error, like a route error, so the cerver does not start
static void test_proxy_add_errors (void) {

	HttpCerver *http_cerver = test_proxy_cerver (HTTP_TRUSTED_PROXIES_LIST);

	test_check_unsigned_eq (http_cerver_add_trusted_proxy (http_cerver, "10.0.0.0/40"), 1, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 1, NULL);

	for (unsigned int i = 0; i < HTTP_TRUSTED_PROXIES_SIZE; i++) {
		test_check_unsigned_eq (http_cerver_add_trusted_proxy (http_cerver, "10.0.0.0/8"), 0, NULL);
	}

	test_check_unsigned_eq (http_cerver_add_trusted_proxy (http_cerver, "10.0.0.0/8"), 1, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 2, NULL);
	test_check_unsigned_eq (http_cerver->n_trusted_proxies, HTTP_TRUSTED_PROXIES_SIZE, NULL);

	http_cerver_delete (http_cerver);

}

// resolves the client of a request from the peer and its forwarded headers
static void test_proxy_expect_client (
	const HttpCerver *http_cerver, const char *peer,
	const char *forwarded_for, const char *real_ip, const char *expected
) {

	HttpRequest *request = http_request_new ();
	test_check_ptr (request);

	if (forwarded_for) request->headers[HTTP_HEADER_FORWARDED_FOR] = str_new (forwarded_for);
	if (real_ip) request->headers[HTTP_HEADER_REAL_IP] = str_new (real_ip);

	http_proxy_resolve_client_ip (http_cerver, peer, request, request->client_ip);
	test_check_str_eq (http_request_get_client_ip (request), expected, NULL);

	http_request_delete (request);

}

static void test_proxy_resolve (void) {

	// without trusted proxies the headers are never used, a client can not spoof its address
	HttpCerver *http_cerver = test_proxy_cerver (HTTP_TRUSTED_PROXIES_NONE);
	test_proxy_expect_client (http_cerver, test_proxy_nginx, test_proxy_client, NULL, test_proxy_nginx);
	test_proxy_expect_client (http_cerver, test_proxy_nginx, NULL, test_proxy_client, test_proxy_nginx);
	test_proxy_expect_client (http_cerver, test_proxy_nginx, NULL, NULL, test_proxy_nginx);

	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_PRIVATE);

	// nginx in a private network forwards the client
	test_proxy_expect_client (http_cerver, test_proxy_nginx, test_proxy_client, NULL, test_proxy_client);
	test_proxy_expect_client (http_cerver, test_proxy_nginx, NULL, test_proxy_client, test_proxy_client);

	// X-Forwarded-For is used before X-Real-IP
	test_proxy_expect_client (http_cerver, test_proxy_nginx, test_proxy_client, test_proxy_outside, test_proxy_client);

	// a chain of proxies: the first address from the right that is not a trusted proxy
	test_proxy_expect_client (
		http_cerver, test_proxy_nginx, "1.2.3.4, 203.0.113.7, 10.0.0.2", NULL, test_proxy_client
	);

	// the client can not spoof the addresses that the trusted proxies append
	test_proxy_expect_client (
		http_cerver, test_proxy_nginx, "10.0.0.9, 203.0.113.7", NULL, test_proxy_client
	);

	// every address is a trusted proxy: the leftmost one
	test_proxy_expect_client (http_cerver, test_proxy_nginx, "10.0.0.1,10.0.0.2", NULL, "10.0.0.1");

	// an invalid address stops the walk at the last valid one
	test_proxy_expect_client (http_cerver, test_proxy_nginx, "unknown, 10.0.0.2", NULL, "10.0.0.2");
	test_proxy_expect_client (http_cerver, test_proxy_nginx, "unknown", NULL, test_proxy_nginx);
	test_proxy_expect_client (http_cerver, test_proxy_nginx, " , ", NULL, test_proxy_nginx);
	test_proxy_expect_client (http_cerver, test_proxy_nginx, NULL, "unknown", test_proxy_nginx);

	// IPv6 clients
	test_proxy_expect_client (http_cerver, "::1", "2001:db8::7", NULL, "2001:db8::7");

	// a peer that is not a trusted proxy is the client
	test_proxy_expect_client (http_cerver, test_proxy_outside, test_proxy_client, test_proxy_client, test_proxy_outside);

	// any peer is trusted
	http_cerver_set_trusted_proxies (http_cerver, HTTP_TRUSTED_PROXIES_ALL);
	test_proxy_expect_client (http_cerver, test_proxy_outside, "1.2.3.4, 203.0.113.7", NULL, "1.2.3.4");

	http_cerver_delete (http_cerver);

	// without a peer
	char client_ip[HTTP_CLIENT_IP_SIZE] = { 0 };
	http_proxy_resolve_client_ip (NULL, NULL, NULL, client_ip);
	test_check_str_eq (client_ip, "", NULL);

}

static void test_proxy_strings (void) {

	test_check_str_eq (http_trusted_proxies_string (HTTP_TRUSTED_PROXIES_NONE), "None", NULL);
	test_check_str_eq (http_trusted_proxies_string (HTTP_TRUSTED_PROXIES_LIST), "List", NULL);
	test_check_str_eq (http_trusted_proxies_string (HTTP_TRUSTED_PROXIES_PRIVATE), "Private", NULL);
	test_check_str_eq (http_trusted_proxies_string (HTTP_TRUSTED_PROXIES_ALL), "All", NULL);

	test_check_ptr (http_trusted_proxies_description (HTTP_TRUSTED_PROXIES_ALL));

}

void http_tests_proxy (void) {

	(void) printf ("Testing HTTP trusted proxies...\n");

	test_proxy_network_parse ();

	test_proxy_network_contains ();

	test_proxy_is_trusted ();

	test_proxy_add_errors ();

	test_proxy_resolve ();

	test_proxy_strings ();

	(void) printf ("Done!\n");

}
