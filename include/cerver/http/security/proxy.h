#ifndef _CERVER_HTTP_PROXY_H_
#define _CERVER_HTTP_PROXY_H_

#include <stdbool.h>
#include <stddef.h>

#include <arpa/inet.h>

#include "cerver/config.h"

#include "cerver/network/network.h"

// the longest IP address (an IPv6 with its zone) and its '\0'
#define HTTP_CLIENT_IP_SIZE				NETWORK_ADDRESS_STRING_SIZE

// the most networks of trusted proxies of a cerver
#define HTTP_TRUSTED_PROXIES_SIZE		32

// the bytes of an IPv6 address, an IPv4 one uses the first 4
#define HTTP_PROXY_ADDRESS_SIZE			16

// the longest CIDR, an IPv6 with its prefix ("/128"), and its '\0'
#define HTTP_PROXY_CIDR_SIZE			(INET6_ADDRSTRLEN + 4)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpRequest;

// from which peers cerver takes the client's address of X-Forwarded-For or X-Real-IP,
// with any other peer, the client is the peer itself
#define HTTP_TRUSTED_PROXIES_MAP(XX)																	\
	XX(0,	NONE,		None,		(The forwarded headers are never used))							\
	XX(1,	LIST,		List,		(Only from the networks added with http_cerver_add_trusted_proxy))	\
	XX(2,	PRIVATE,	Private,	(Only from the loopback and the private networks))				\
	XX(3,	ALL,		All,		(From any peer))

typedef enum HttpTrustedProxies {

	#define XX(num, name, string, description) HTTP_TRUSTED_PROXIES_##name = num,
	HTTP_TRUSTED_PROXIES_MAP(XX)
	#undef XX

} HttpTrustedProxies;

CERVER_PUBLIC const char *http_trusted_proxies_string (
	const HttpTrustedProxies trusted_proxies
);

CERVER_PUBLIC const char *http_trusted_proxies_description (
	const HttpTrustedProxies trusted_proxies
);

#define HTTP_CERVER_DEFAULT_TRUSTED_PROXIES		HTTP_TRUSTED_PROXIES_NONE

// an IPv4 or IPv6 network, "172.16.0.0/12", "fc00::/7"
typedef struct HttpProxyNetwork {

	int family;
	unsigned char address[HTTP_PROXY_ADDRESS_SIZE];
	unsigned int prefix;

} HttpProxyNetwork;

// parses a network in CIDR notation, an address without a prefix is a single host
// returns true on success
CERVER_PUBLIC bool http_proxy_network_parse (
	const char *cidr, HttpProxyNetwork *network
);

// returns true if the address (an IPv4 or IPv6 string) is inside the network
CERVER_PUBLIC bool http_proxy_network_contains (
	const HttpProxyNetwork *network, const char *ip, const size_t ip_length
);

// returns true if the cerver trusts the forwarded headers sent by the address
CERVER_PUBLIC bool http_proxy_is_trusted (
	const struct _HttpCerver *http_cerver, const char *ip, const size_t ip_length
);

// sets the address of the client of the request in client_ip (HTTP_CLIENT_IP_SIZE bytes):
// the peer, or, when the peer is a trusted proxy, the first address of X-Forwarded-For,
// from right to left, that is not a trusted proxy (X-Real-IP if there is no X-Forwarded-For)
// an invalid forwarded address leaves the peer
CERVER_PRIVATE void http_proxy_resolve_client_ip (
	const struct _HttpCerver *http_cerver, const char *peer_ip,
	const struct _HttpRequest *request, char *client_ip
);

#ifdef __cplusplus
}
#endif

#endif
