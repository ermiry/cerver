/**
 * @file cerver/network/network.h
 * @brief The addresses, the resolution of the hosts and the options of the sockets.
 */

#ifndef _CERVER_NETWORK_H_
#define _CERVER_NETWORK_H_

#include <stdbool.h>

#include <arpa/inet.h>
#include <fcntl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/config.h"

/**
 * @defgroup network Network
 * @brief The addresses and the sockets of cerver: the network values, the sockets of the connections and the UDP
 * peers.
 */

/**
 * @defgroup network_values Values
 * @ingroup network
 * @brief The sizes and the limits of the addresses and the packets.
 * @{
 */

/** @brief The size of the text of an IPv4 address, with its terminator. */
#define IP_TO_STR_LEN					16

/** @brief The size of the text of an IPv6 address, with its terminator. */
#define IPV6_TO_STR_LEN					46

/** @brief The biggest port. */
#define MAX_PORT_NUM					65535

/** @brief The payload of an IPv4 UDP datagram: 65535 - 20 (IP header) - 8 (UDP header). */
#define MAX_UDP_PACKET_SIZE				65507

/** @brief The bytes after which an IPv4 starts inside an IPv4-mapped IPv6 address. */
#define NETWORK_IPV4_MAPPED_OFFSET		12

/** @brief Separates a link local IPv6 address from its zone, like in `fe80::1%eth0`. */
#define NETWORK_ZONE_SEPARATOR			'%'

/** @brief The size of an ip with its zone: the IPv6 text, the separator and the interface name. */
#define NETWORK_ADDRESS_STRING_SIZE		(INET6_ADDRSTRLEN + IF_NAMESIZE)

/** @brief The most addresses of a host name that are kept. */
#define NETWORK_MAX_RESOLVED_ADDRESSES	16

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup network_addresses Addresses
 * @ingroup network
 * @brief The protocols, the resolution of the hosts and the addresses.
 * @{
 */

/**
 * @brief The protocol of a cerver or a connection.
 */
typedef enum Protocol {

	/** @brief TCP. */
	PROTOCOL_TCP = IPPROTO_TCP,
	/** @brief UDP. */
	PROTOCOL_UDP = IPPROTO_UDP

} Protocol;

/**
 * @brief The size of a member of a struct, without an instance.
 *
 * @param type the struct
 * @param member the member
 */
#define SOCK_SIZEOF_MEMBER(type, member) (sizeof (((type *) NULL)->member))

/**
 * @brief The addresses that a host resolves to, in the order to try them.
 */
typedef struct NetworkAddresses {

	/** @brief The addresses in the array. */
	unsigned int count;
	/** @brief The addresses, at most #NETWORK_MAX_RESOLVED_ADDRESSES. */
	struct sockaddr_storage addresses[NETWORK_MAX_RESOLVED_ADDRESSES];

} NetworkAddresses;

/**
 * @brief Returns the ip of a host name, IPv4 or IPv6, resolved with getaddrinfo ().
 *
 * @param[in] hostname the host name or a literal ip
 * @return the ip as a string allocated with malloc (), NULL on error or if it was not found
 * @threadsafety Safe from any thread.
 */
CERVER_PUBLIC char *network_hostname_to_ip (
	const char *hostname
);

/**
 * @brief Resolves a literal ip or a host name into an address with the port.
 *
 * An address of the family of prefer_ipv6 is taken first, and any other one if there is not one.
 *
 * @param[in] host the host name or a literal ip
 * @param[in] port the port, in the order of the host
 * @param[in] prefer_ipv6 true to take an IPv6 address first
 * @param[out] address the address
 * @retval 0 the address was resolved
 * @retval 1 a NULL argument or the host could not be resolved
 */
CERVER_PUBLIC unsigned int network_resolve_address (
	const char *host, const in_port_t port, const bool prefer_ipv6,
	struct sockaddr_storage *address
);

/**
 * @brief Resolves a literal ip (with its zone) or a host name into every address of the protocol.
 *
 * The families alternate, starting with the one of prefer_ipv6, and each family keeps the order of
 * getaddrinfo () (RFC 6724).
 *
 * @param[in] host the host name or a literal ip
 * @param[in] port the port, in the order of the host
 * @param[in] prefer_ipv6 true to start with an IPv6 address
 * @param[in] protocol IPPROTO_TCP or IPPROTO_UDP
 * @param[out] addresses the addresses, at most #NETWORK_MAX_RESOLVED_ADDRESSES
 * @retval 0 the host was resolved
 * @retval 1 a NULL argument or the host could not be resolved
 */
CERVER_PUBLIC unsigned int network_resolve_addresses (
	const char *host, const in_port_t port,
	const bool prefer_ipv6, const int protocol,
	NetworkAddresses *addresses
);

/**
 * @brief Parses a literal IPv4 or IPv6 address, without a port, into an address.
 *
 * A link local IPv6 address can have its zone, as an interface name or index, like `fe80::1%eth0`.
 *
 * @param[in] ip the literal address
 * @param[in] port the port, in the order of the host
 * @param[out] address the address
 * @retval 0 the address was parsed
 * @retval 1 a NULL argument or it is not a valid ip
 */
CERVER_PUBLIC unsigned int network_parse_address (
	const char *ip, const in_port_t port,
	struct sockaddr_storage *address
);

/**
 * @brief Returns the size of the address of its family, the size that bind () and connect () expect.
 *
 * @param[in] address the address, it must not be NULL
 * @return the size of a sockaddr_in6 for AF_INET6, the one of a sockaddr_in for any other family
 */
CERVER_PUBLIC socklen_t sock_address_size (const struct sockaddr_storage *address);

/**
 * @brief Returns whether the address is an IPv4 inside an IPv6 address (an IPv4-mapped IPv6 address).
 *
 * @param[in] address the address
 * @return true for an IPv4-mapped IPv6 address, false otherwise or for NULL
 */
CERVER_PUBLIC bool sock_ip_is_mapped (const struct sockaddr *address);

/**
 * @brief Sets IPV6_V6ONLY in an IPv6 socket.
 *
 * @param[in] sock_fd the socket
 * @param[in] ipv6_only false also accepts IPv4 connections (dual stack)
 * @retval 0 the option was set
 * @retval 1 on error
 */
CERVER_PUBLIC int sock_set_ipv6_only (int sock_fd, bool ipv6_only);

/**
 * @brief Enables or disables the blocking of a socket.
 *
 * @param[in] fd the socket
 * @param[in] blocking false sets O_NONBLOCK, true clears it
 * @return true on success, false for a negative fd or on error
 */
CERVER_PUBLIC bool sock_set_blocking (
	int32_t fd, bool blocking
);

/**
 * @brief Writes the ip of an address.
 *
 * An IPv4-mapped IPv6 address is written as the IPv4 (a.b.c.d), an IPv6 address with a scope id is written with its
 * zone (`fe80::1%eth0`).
 *
 * @param[in] address the address, it must not be NULL
 * @param[out] ip_buffer a buffer of at least #NETWORK_ADDRESS_STRING_SIZE bytes
 * @retval 0 the ip was written
 * @retval 1 the address is not AF_INET nor AF_INET6
 */
CERVER_PUBLIC unsigned int sock_ip_to_string_actual (
	const struct sockaddr *address,
	char *ip_buffer
);

/**
 * @brief Returns the ip of an address, like sock_ip_to_string_actual ().
 *
 * @param[in] address the address
 * @return the ip as a string allocated with malloc (), NULL for NULL, another family or on error
 */
CERVER_PUBLIC char *sock_ip_to_string (
	const struct sockaddr *address
);

/**
 * @brief Returns whether two addresses have the same ip, port and zone.
 *
 * An IPv4-mapped IPv6 address is equal to its IPv4.
 *
 * @param[in] a an address
 * @param[in] b the other address
 * @return true if they are equal, false otherwise or if one is NULL
 */
CERVER_PUBLIC bool sock_ip_equal (
	const struct sockaddr *a, const struct sockaddr *b
);

/**
 * @brief Returns the port of an address.
 *
 * @param[in] address the address
 * @return the port in the order of the host, 0 for NULL or another family
 */
CERVER_PUBLIC in_port_t sock_ip_port (
	const struct sockaddr *address
);

/**
 * @brief Sets the timeout of the receives of a socket (SO_RCVTIMEO).
 *
 * A recv () still blocks until the timeout, and fails with EAGAIN if no data was read.
 *
 * @param[in] sock_fd the socket
 * @param[in] timeout the seconds, 0 disables it
 * @retval 0 the timeout was set
 * @retval 1 on error
 */
CERVER_PUBLIC int sock_set_timeout (
	int sock_fd, time_t timeout
);

/**
 * @brief Sets the timeout of the sends of a socket (SO_SNDTIMEO).
 *
 * A send that can not make progress in that time fails with EAGAIN.
 *
 * @param[in] sock_fd the socket
 * @param[in] timeout the seconds, 0 disables it
 * @retval 0 the timeout was set
 * @retval 1 on error
 */
CERVER_PUBLIC int sock_set_send_timeout (
	int sock_fd, time_t timeout
);

/**
 * @brief Sets SO_REUSEADDR in a socket, to avoid errors when it binds an address that was just used.
 *
 * @param[in] sock_fd the socket
 * @retval 0 the option was set
 * @retval 1 on error
 */
CERVER_PUBLIC int sock_set_reusable (int sock_fd);

/**
 * @brief Sets SO_REUSEPORT in a socket: other sockets can bind the same port, and the kernel balances them.
 *
 * @param[in] sock_fd the socket
 * @retval 0 the option was set
 * @retval 1 on error, or if the system does not have SO_REUSEPORT
 */
CERVER_PUBLIC int sock_set_reusable_port (int sock_fd);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
