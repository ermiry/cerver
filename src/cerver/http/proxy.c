#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ctype.h>
#include <pthread.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include "cerver/network.h"

#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/proxy.h"
#include "cerver/http/request.h"

#define HTTP_PROXY_IPV4_SIZE				4
#define HTTP_PROXY_IPV4_BITS				32
#define HTTP_PROXY_IPV6_BITS				128
#define HTTP_PROXY_BYTE_BITS				8
#define HTTP_PROXY_BYTE_MASK				0xffu
#define HTTP_PROXY_DECIMAL_BASE				10

static const char http_proxy_prefix_separator = '/';
static const char http_proxy_list_separator = ',';

// the loopback and the private networks (RFC 1918, RFC 4193) and the link local ones
static const char *http_proxy_private_cidrs[] = {
	"127.0.0.0/8",
	"10.0.0.0/8",
	"172.16.0.0/12",
	"192.168.0.0/16",
	"169.254.0.0/16",
	"::1/128",
	"fc00::/7",
	"fe80::/10"
};

#define HTTP_PROXY_PRIVATE_NETWORKS			8

static HttpProxyNetwork http_proxy_private_networks[HTTP_PROXY_PRIVATE_NETWORKS];
static pthread_once_t http_proxy_private_networks_once = PTHREAD_ONCE_INIT;

const char *http_trusted_proxies_string (
	const HttpTrustedProxies trusted_proxies
) {

	switch (trusted_proxies) {
		#define XX(num, name, string, description) case HTTP_TRUSTED_PROXIES_##name: return #string;
		HTTP_TRUSTED_PROXIES_MAP(XX)
		#undef XX
	}

	return http_trusted_proxies_string (HTTP_TRUSTED_PROXIES_NONE);

}

const char *http_trusted_proxies_description (
	const HttpTrustedProxies trusted_proxies
) {

	switch (trusted_proxies) {
		#define XX(num, name, string, description) case HTTP_TRUSTED_PROXIES_##name: return #description;
		HTTP_TRUSTED_PROXIES_MAP(XX)
		#undef XX
	}

	return http_trusted_proxies_description (HTTP_TRUSTED_PROXIES_NONE);

}

// an IPv4 inside an IPv6 is kept as an IPv4, like the connections of the core do,
// so "::ffff:10.0.0.1" is inside "10.0.0.0/8"
// the bytes are copied to an aligned address before they are checked
static void http_proxy_address_unmap (int *family, unsigned char *address) {

	if (*family == AF_INET6) {
		struct in6_addr ipv6;
		(void) memcpy (&ipv6, address, sizeof (struct in6_addr));

		if (IN6_IS_ADDR_V4MAPPED (&ipv6)) {
			(void) memmove (address, address + NETWORK_IPV4_MAPPED_OFFSET, HTTP_PROXY_IPV4_SIZE);
			(void) memset (address + HTTP_PROXY_IPV4_SIZE, 0, HTTP_PROXY_ADDRESS_SIZE - HTTP_PROXY_IPV4_SIZE);
			*family = AF_INET;
		}
	}

}

// parses an IPv4 or an IPv6 address of length bytes (it does not need a '\0')
// the zone of a link local address (fe80::1%eth0) is not part of a network, it is ignored
// returns true on success
static bool http_proxy_address_parse (
	const char *ip, const size_t ip_length,
	int *family, unsigned char *address
) {

	bool parsed = false;

	if (ip && ip_length && (ip_length < HTTP_CLIENT_IP_SIZE)) {
		char value[HTTP_CLIENT_IP_SIZE] = { 0 };
		(void) memcpy (value, ip, ip_length);

		char *zone = strchr (value, NETWORK_ZONE_SEPARATOR);
		if (zone) *zone = '\0';

		(void) memset (address, 0, HTTP_PROXY_ADDRESS_SIZE);
		if (inet_pton (AF_INET, value, address) == 1) {
			*family = AF_INET;
			parsed = true;
		}

		else if (inet_pton (AF_INET6, value, address) == 1) {
			*family = AF_INET6;
			http_proxy_address_unmap (family, address);
			parsed = true;
		}
	}

	return parsed;

}

// returns the prefix of the CIDR, or the bits of the whole address without one, -1 on error
static int http_proxy_network_parse_prefix (
	const char *prefix, const int family
) {

	const int max_bits = (family == AF_INET) ? HTTP_PROXY_IPV4_BITS : HTTP_PROXY_IPV6_BITS;

	int bits = max_bits;
	if (prefix) {
		char *end = NULL;
		const long value = strtol (prefix, &end, HTTP_PROXY_DECIMAL_BASE);
		bits = (*prefix && end && !*end && (value >= 0) && (value <= max_bits)) ? (int) value : -1;
	}

	return bits;

}

bool http_proxy_network_parse (
	const char *cidr, HttpProxyNetwork *network
) {

	bool parsed = false;

	if (cidr && network && (strlen (cidr) < HTTP_PROXY_CIDR_SIZE)) {
		const char *separator = strchr (cidr, http_proxy_prefix_separator);
		const size_t address_length = separator ? (size_t) (separator - cidr) : strlen (cidr);

		if (http_proxy_address_parse (cidr, address_length, &network->family, network->address)) {
			const int prefix = http_proxy_network_parse_prefix (
				separator ? separator + 1 : NULL, network->family
			);

			if (prefix >= 0) {
				network->prefix = (unsigned int) prefix;
				parsed = true;
			}
		}
	}

	return parsed;

}

static bool http_proxy_network_contains_address (
	const HttpProxyNetwork *network, const int family, const unsigned char *address
) {

	bool contains = false;

	if (network->family == family) {
		const unsigned int whole_bytes = network->prefix / HTTP_PROXY_BYTE_BITS;
		const unsigned int remaining_bits = network->prefix % HTTP_PROXY_BYTE_BITS;

		contains = !memcmp (network->address, address, whole_bytes);
		if (contains && remaining_bits) {
			const unsigned char mask = (unsigned char) (HTTP_PROXY_BYTE_MASK << (HTTP_PROXY_BYTE_BITS - remaining_bits));
			contains = ((network->address[whole_bytes] & mask) == (address[whole_bytes] & mask));
		}
	}

	return contains;

}

bool http_proxy_network_contains (
	const HttpProxyNetwork *network, const char *ip, const size_t ip_length
) {

	int family = 0;
	unsigned char address[HTTP_PROXY_ADDRESS_SIZE] = { 0 };

	return network
		&& http_proxy_address_parse (ip, ip_length, &family, address)
		&& http_proxy_network_contains_address (network, family, address);

}

static void http_proxy_private_networks_init (void) {

	for (unsigned int i = 0; i < HTTP_PROXY_PRIVATE_NETWORKS; i++) {
		(void) http_proxy_network_parse (http_proxy_private_cidrs[i], &http_proxy_private_networks[i]);
	}

}

static bool http_proxy_networks_contain (
	const HttpProxyNetwork *networks, const unsigned int n_networks,
	const int family, const unsigned char *address
) {

	bool contains = false;

	for (unsigned int i = 0; !contains && (i < n_networks); i++) {
		contains = http_proxy_network_contains_address (&networks[i], family, address);
	}

	return contains;

}

bool http_proxy_is_trusted (
	const HttpCerver *http_cerver, const char *ip, const size_t ip_length
) {

	bool trusted = false;

	int family = 0;
	unsigned char address[HTTP_PROXY_ADDRESS_SIZE] = { 0 };
	if (http_cerver && http_proxy_address_parse (ip, ip_length, &family, address)) {
		switch (http_cerver->trusted_proxies) {
			case HTTP_TRUSTED_PROXIES_NONE: break;

			case HTTP_TRUSTED_PROXIES_LIST:
				trusted = http_proxy_networks_contain (
					http_cerver->trusted_proxies_networks, http_cerver->n_trusted_proxies,
					family, address
				);
				break;

			case HTTP_TRUSTED_PROXIES_PRIVATE:
				(void) pthread_once (&http_proxy_private_networks_once, http_proxy_private_networks_init);
				trusted = http_proxy_networks_contain (
					http_proxy_private_networks, HTTP_PROXY_PRIVATE_NETWORKS,
					family, address
				);
				break;

			case HTTP_TRUSTED_PROXIES_ALL:
				trusted = true;
				break;
		}
	}

	return trusted;

}

static bool http_proxy_address_is_valid (const char *ip, const size_t ip_length) {

	int family = 0;
	unsigned char address[HTTP_PROXY_ADDRESS_SIZE] = { 0 };

	return http_proxy_address_parse (ip, ip_length, &family, address);

}

static void http_proxy_client_ip_set (
	char *client_ip, const char *ip, const size_t ip_length
) {

	if (ip_length) (void) memcpy (client_ip, ip, ip_length);
	client_ip[ip_length] = '\0';

}

// the item of the list that ends at end, without its spaces
static const char *http_proxy_list_item (
	const char *start, const char *end, size_t *item_length
) {

	const char *item_start = end;
	while ((item_start > start) && (*(item_start - 1) != http_proxy_list_separator)) item_start -= 1;

	const char *first = item_start;
	const char *last = end;
	while ((first < last) && isspace ((unsigned char) *first)) first += 1;
	while ((last > first) && isspace ((unsigned char) *(last - 1))) last -= 1;

	*item_length = (size_t) (last - first);

	return first;

}

// walks the addresses from right to left: each proxy appends the address that it received from,
// so the client is the first one that is not a trusted proxy
static void http_proxy_resolve_forwarded_for (
	const HttpCerver *http_cerver, const String *forwarded_for, char *client_ip
) {

	const char *start = forwarded_for->str;
	const char *end = forwarded_for->str + forwarded_for->len;
	while (end > start) {
		size_t ip_length = 0;
		const char *ip = http_proxy_list_item (start, end, &ip_length);
		if (!http_proxy_address_is_valid (ip, ip_length)) break;

		http_proxy_client_ip_set (client_ip, ip, ip_length);
		if (!http_proxy_is_trusted (http_cerver, ip, ip_length)) break;

		// the separator before the item
		const char *separator = ip;
		while ((separator > start) && (*separator != http_proxy_list_separator)) separator -= 1;
		end = separator;
	}

}

void http_proxy_resolve_client_ip (
	const HttpCerver *http_cerver, const char *peer_ip,
	const HttpRequest *request, char *client_ip
) {

	if (client_ip) {
		const size_t peer_length = peer_ip ? strnlen (peer_ip, HTTP_CLIENT_IP_SIZE - 1) : 0;
		http_proxy_client_ip_set (client_ip, peer_ip, peer_length);

		if (http_cerver && request && http_proxy_is_trusted (http_cerver, peer_ip, peer_length)) {
			const String *forwarded_for = request->headers[HTTP_HEADER_FORWARDED_FOR];
			const String *real_ip = request->headers[HTTP_HEADER_REAL_IP];

			if (forwarded_for && forwarded_for->len) {
				http_proxy_resolve_forwarded_for (http_cerver, forwarded_for, client_ip);
			}

			else if (real_ip && http_proxy_address_is_valid (real_ip->str, real_ip->len)) {
				http_proxy_client_ip_set (client_ip, real_ip->str, real_ip->len);
			}
		}
	}

}
