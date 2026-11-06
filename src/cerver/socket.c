#include <stdlib.h>

#include <pthread.h>

#include "cerver/socket.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/handler.h"

Socket *socket_new (void) {

	Socket *socket = (Socket *) malloc (sizeof (Socket));
	if (socket) {
		socket->sock_fd = -1;

		socket->packet_buffer = NULL;
		socket->packet_buffer_size = 0;

		socket->read_mutex = NULL;
		socket->write_mutex = NULL;

		socket->references = 1;
	}

	return socket;

}

// takes a new reference to the socket
Socket *socket_reference (Socket *socket) {

	if (socket) {
		(void) __atomic_add_fetch (&socket->references, 1, __ATOMIC_ACQ_REL);
	}

	return socket;

}

// releases a reference to the socket, it is deleted with the last one
void socket_delete (void *socket_ptr) {

	if (socket_ptr) {
		Socket *socket = (Socket *) socket_ptr;

		if (__atomic_sub_fetch (&socket->references, 1, __ATOMIC_ACQ_REL)) return;

		if (socket->packet_buffer) free (socket->packet_buffer);

		// the mutexes live inside the socket,
		// a send that another thread started finishes before they are destroyed
		if (socket->read_mutex) (void) pthread_mutex_destroy (socket->read_mutex);
		if (socket->write_mutex) {
			(void) pthread_mutex_lock (socket->write_mutex);
			(void) pthread_mutex_unlock (socket->write_mutex);
			(void) pthread_mutex_destroy (socket->write_mutex);
		}

		free (socket_ptr);
	}

}

void *socket_create_empty (void) {

	Socket *socket = socket_new ();
	if (socket) {
		socket->read_mutex = &socket->read_mutex_storage;
		(void) pthread_mutex_init (socket->read_mutex, NULL);

		socket->write_mutex = &socket->write_mutex_storage;
		(void) pthread_mutex_init (socket->write_mutex, NULL);
	}

	return socket;

}

Socket *socket_create (int fd) {

	Socket *socket = (Socket *) socket_create_empty ();
	if (socket) {
		socket->sock_fd = fd;
	}

	return socket;

}
