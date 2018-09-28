#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <arpa/inet.h>

#include "server.h"

extern bool sock_setNonBlocking (i32);

#endif