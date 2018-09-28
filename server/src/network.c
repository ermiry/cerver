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