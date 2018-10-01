#include <string.h>

#include "network.h"

/*** SOCKETS ***/   

bool sock_setNonBlocking (i32 server) {

    int non_blocking = 1;
	return fcntl (server, F_SETFL, O_NONBLOCK, non_blocking) != -1;

}

const char *sock_ip_to_string (const struct sockaddr *address, char *string, size_t string_size) {

	switch(address->sa_family) {
        case AF_INET:
            return inet_ntop(AF_INET,
                            &((struct sockaddr_in *) address)->sin_addr,
                            string, string_size);
            break;
        case AF_INET6:
            return inet_ntop(AF_INET6,
                            &((struct sockaddr_in6 *) address)->sin6_addr,
                            string, string_size);
            break;
        default:
            strncpy(string, "[Unknown AF!]", string_size);
            return NULL;
	}

}

bool sock_ip_equal (const struct sockaddr *a, const struct sockaddr *b) {

	if (a->sa_family != b->sa_family) return false;

	switch(a->sa_family) {
        case AF_INET: {
            struct sockaddr_in *a_in = (struct sockaddr_in *) a;
            struct sockaddr_in *b_in = (struct sockaddr_in *) b;
            return a_in->sin_port == b_in->sin_port
                && a_in->sin_addr.s_addr == b_in->sin_addr.s_addr;
        }

        case AF_INET6: {
            struct sockaddr_in6 *a_in6 = (struct sockaddr_in6 *) a;
            struct sockaddr_in6 *b_in6 = (struct sockaddr_in6 *) b;
            return a_in6->sin6_port == b_in6->sin6_port
                && a_in6->sin6_flowinfo == b_in6->sin6_flowinfo
                && memcmp(a_in6->sin6_addr.s6_addr, b_in6->sin6_addr.s6_addr,
                        SOCK_SIZEOF_MEMBER (struct in6_addr, s6_addr)) == 0
                && a_in6->sin6_scope_id == b_in6->sin6_scope_id;
        }

        default: return false;
    }

}

in_port_t sock_ip_port (const struct sockaddr *address) {

	switch(address->sa_family) {
        case AF_INET: return ntohs (((struct sockaddr_in *) address)->sin_port);
        case AF_INET6: return ntohs (((struct sockaddr_in6 *) address)->sin6_port);
        default: return 0;
	}

}