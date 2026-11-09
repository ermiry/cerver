#ifndef _CERVER_SOCKET_H_
#define _CERVER_SOCKET_H_

#include <pthread.h>

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/receive.h"

#include "cerver/collections/freelist.h"

// the deleted sockets that are kept to be reused
#define SOCKET_RECYCLE_LIMIT			FREELIST_RECYCLE_LIMIT (FREELIST_DEFAULT_LIMIT)

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Socket {

    int sock_fd;

	char *packet_buffer;
	size_t packet_buffer_size;

	// point to the storage below, so a socket is a single allocation
	pthread_mutex_t *read_mutex;
	pthread_mutex_t *write_mutex;

	pthread_mutex_t read_mutex_storage;
	pthread_mutex_t write_mutex_storage;

	// the connection owns one reference, and every CerverReceive another one,
	// so a receive thread can still read the socket after a handler deleted its connection
	unsigned int references;

	// the next deleted socket that waits to be reused
	struct _Socket *recycle_next;

};

typedef struct _Socket Socket;

CERVER_PUBLIC Socket *socket_new (void);

// releases a reference to the socket, it is deleted with the last one
CERVER_PUBLIC void socket_delete (void *socket_ptr);

// takes a new reference to the socket, that must be released with socket_delete ()
// returns the same socket
CERVER_PUBLIC Socket *socket_reference (Socket *socket);

CERVER_PUBLIC void *socket_create_empty (void);

CERVER_PUBLIC Socket *socket_create (int fd);

#ifdef __cplusplus
}
#endif

#endif