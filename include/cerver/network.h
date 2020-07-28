#ifndef _CERVER_NETWORK_H_
#define _CERVER_NETWORK_H_

#include <stdbool.h>

#include <unistd.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define IP_TO_STR_LEN       16
#define IPV6_TO_STR_LEN     46

typedef enum Protocol {

	PROTOCOL_TCP = IPPROTO_TCP,
	PROTOCOL_UDP = IPPROTO_UDP

} Protocol;

#define SOCK_SIZEOF_MEMBER(type, member) (sizeof(((type *) NULL)->member))

// enable/disable blocking on a socket
// true on success, false if there was an error
extern bool sock_set_blocking (int32_t fd, bool blocking);

extern char *sock_ip_to_string ( const struct sockaddr *address);

extern bool sock_ip_equal (const struct sockaddr *a, const struct sockaddr *b);

extern in_port_t sock_ip_port (const struct sockaddr *address);

#endif