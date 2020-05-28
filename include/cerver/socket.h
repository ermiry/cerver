#ifndef _CERVER_SOCKET_H_
#define _CERVER_SOCKET_H_

#include <pthread.h>

struct _Socket {

    int sock_fd;

	char *packet_buffer;

	pthread_mutex_t *mutex;

};

typedef struct _Socket Socket;

extern Socket *sockt_new (void);

extern void socket_delete (void *socket_ptr);

extern Socket *socket_create (int fd);

#endif