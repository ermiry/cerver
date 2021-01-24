#ifndef _CERVER_SOCKET_H_
#define _CERVER_SOCKET_H_

#include <pthread.h>

#include "cerver/cerver.h"
#include "cerver/config.h"
#include "cerver/receive.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;

struct _Socket {

    int sock_fd;

	char *packet_buffer;
	size_t packet_buffer_size;

	pthread_mutex_t *read_mutex;
	pthread_mutex_t *write_mutex;

};

typedef struct _Socket Socket;

CERVER_PUBLIC Socket *socket_new (void);

CERVER_PUBLIC void socket_delete (void *socket_ptr);

CERVER_PUBLIC void *socket_create_empty (void);

CERVER_PUBLIC Socket *socket_create (int fd);

#ifdef __cplusplus
}
#endif

#endif