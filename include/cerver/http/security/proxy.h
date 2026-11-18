/**
 * @file cerver/http/security/proxy.h
 * @brief The trusted proxies of a CerverHttp and the address of the client of a request.
 */

#ifndef _CERVER_HTTP_PROXY_H_
#define _CERVER_HTTP_PROXY_H_

#include <stdbool.h>
#include <stddef.h>

#include <arpa/inet.h>

#include "cerver/config.h"

#include "cerver/network/network.h"

/**
 * @defgroup http_proxies Proxies
 * @ingroup http_security
 * @brief Which peers can tell the address of the client with X-Forwarded-For and X-Real-IP.
 * @{
 */

/** @brief The size of the longest IP address (an IPv6 with its zone) and its terminator. */
#define HTTP_CLIENT_IP_SIZE				NETWORK_ADDRESS_STRING_SIZE

/** @brief The most networks of trusted proxies of a cerver. */
#define HTTP_TRUSTED_PROXIES_SIZE		32

/** @brief The bytes of an IPv6 address; an IPv4 one uses the first 4. */
#define HTTP_PROXY_ADDRESS_SIZE			16

/** @brief The size of the longest CIDR, an IPv6 with its prefix ("/128"), and its terminator. */
#define HTTP_PROXY_CIDR_SIZE			(INET6_ADDRSTRLEN + 4)

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpRequest;

/**
 * @brief The X-macro table of the peers from which cerver takes the address of the client of X-Forwarded-For or
 * X-Real-IP: the number, the name, the string and the description of each one.
 *
 * With any other peer, the client is the peer itself.
 */
#define HTTP_TRUSTED_PROXIES_MAP(XX)																	\
	XX(0,	NONE,		None,		(The forwarded headers are never used))							\
	XX(1,	LIST,		List,		(Only from the networks added with cerver_http_add_trusted_proxy))	\
	XX(2,	PRIVATE,	Private,	(Only from the loopback and the private networks))				\
	XX(3,	ALL,		All,		(From any peer))

/**
 * @brief Which peers are trusted proxies, one `HTTP_TRUSTED_PROXIES_<name>` for each row of
 * #HTTP_TRUSTED_PROXIES_MAP.
 */
typedef enum CerverHttpTrustedProxies {

	#define XX(num, name, string, description) HTTP_TRUSTED_PROXIES_##name = num,
	HTTP_TRUSTED_PROXIES_MAP(XX)
	#undef XX

} CerverHttpTrustedProxies;

/**
 * @brief Returns the string of a trusted proxies setting.
 *
 * @param[in] trusted_proxies the setting
 * @return the string of its row in #HTTP_TRUSTED_PROXIES_MAP, or the one of HTTP_TRUSTED_PROXIES_NONE for a value
 * out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_trusted_proxies_string (
	const CerverHttpTrustedProxies trusted_proxies
);

/**
 * @brief Returns the description of a trusted proxies setting.
 *
 * @param[in] trusted_proxies the setting
 * @return the description of its row in #HTTP_TRUSTED_PROXIES_MAP, or the one of HTTP_TRUSTED_PROXIES_NONE for a
 * value out of the table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_http_trusted_proxies_description (
	const CerverHttpTrustedProxies trusted_proxies
);

/** @brief By default the forwarded headers are never used. */
#define CERVER_HTTP_DEFAULT_TRUSTED_PROXIES		HTTP_TRUSTED_PROXIES_NONE

/**
 * @brief An IPv4 or IPv6 network, like "172.16.0.0/12" or "fc00::/7".
 */
typedef struct HttpProxyNetwork {

	/** @brief AF_INET or AF_INET6. */
	int family;
	/** @brief The bytes of the address of the network. */
	unsigned char address[HTTP_PROXY_ADDRESS_SIZE];
	/** @brief The bits of its prefix. */
	unsigned int prefix;

} HttpProxyNetwork;

/**
 * @brief Parses a network in CIDR notation; an address without a prefix is a single host.
 *
 * @param[in] cidr the network
 * @param[out] network the parsed network
 * @return true when it was parsed, false for a NULL argument, a CIDR that is too long or an invalid one
 */
CERVER_PUBLIC bool cerver_http_proxy_network_parse (
	const char *cidr, HttpProxyNetwork *network
);

/**
 * @brief Returns whether an address is inside a network.
 *
 * @param[in] network the network
 * @param[in] ip the IPv4 or IPv6 address, it does not need to end with '\0'
 * @param[in] ip_length its length
 * @return true if the address is inside, false for a NULL network or an invalid address
 */
CERVER_PUBLIC bool cerver_http_proxy_network_contains (
	const HttpProxyNetwork *network, const char *ip, const size_t ip_length
);

/**
 * @brief Returns whether the cerver trusts the forwarded headers sent by an address.
 *
 * @param[in] http_cerver the CerverHttp, with its trusted proxies setting
 * @param[in] ip the IPv4 or IPv6 address of the peer
 * @param[in] ip_length its length
 * @return true for a trusted proxy, false for a NULL CerverHttp or an invalid address
 */
CERVER_PUBLIC bool cerver_http_proxy_is_trusted (
	const struct _HttpCerver *http_cerver, const char *ip, const size_t ip_length
);

/**
 * @brief Sets the address of the client of a request.
 *
 * It is the peer or, when the peer is a trusted proxy, the first address of X-Forwarded-For, from right to left,
 * that is not a trusted proxy (X-Real-IP if there is no X-Forwarded-For); an invalid forwarded address leaves the
 * peer.
 *
 * @param[in] http_cerver the CerverHttp
 * @param[in] peer_ip the address of the peer
 * @param[in] request the request, with its headers
 * @param[out] client_ip the buffer of #HTTP_CLIENT_IP_SIZE bytes, NULL is ignored
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_proxy_resolve_client_ip (
	const struct _HttpCerver *http_cerver, const char *peer_ip,
	const struct _HttpRequest *request, char *client_ip
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
