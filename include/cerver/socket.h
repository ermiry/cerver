#ifndef _CERVER_SOCKET_H_
#define _CERVER_SOCKET_H_

#include <pthread.h>

#include "cerver/cerver.h"

struct _Cerver;

struct _Socket {

    int sock_fd;

	char *packet_buffer;
	size_t packet_buffer_size;

	pthread_mutex_t *read_mutex;
	pthread_mutex_t *write_mutex;

};

typedef struct _Socket Socket;

extern void socket_delete (void *socket_ptr);

extern void *socket_create_empty (void);

extern Socket *socket_create (int fd);

extern Socket *socket_get_by_fd (struct _Cerver *cerver, int sock_fd, bool on_hold);

#endif