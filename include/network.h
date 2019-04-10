#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#define IP_TO_STR_LEN       16
#define IPV6_TO_STR_LEN     46

#define SOCK_SIZEOF_MEMBER(type, member) (sizeof(((type *) NULL)->member))

extern bool sock_setBlocking (int32_t fd, bool blocking);
// extern bool sock_setNonBlocking (int32_t);

extern char *sock_ip_to_string ( const struct sockaddr *address) ;
extern bool sock_ip_equal (const struct sockaddr *a, const struct sockaddr *b);

extern in_port_t sock_ip_port (const struct sockaddr *address);

#endif