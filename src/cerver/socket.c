#include <stdlib.h>

#include <pthread.h>

#include "cerver/socket.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/handler.h"

#include "cerver/collections/freelist.h"

#pragma region recycle

static FreeList socket_recycled = FREELIST_INITIALIZER (
	Socket, recycle_next, SOCKET_RECYCLE_LIMIT
);

// a new socket initializes its mutexes once, a recycled one keeps them
static Socket *socket_allocate (void) {

	Socket *socket = (Socket *) freelist_pop (&socket_recycled);
	if (!socket) {
		socket = (Socket *) malloc (sizeof (Socket));
		if (socket) {
			(void) pthread_mutex_init (&socket->read_mutex_storage, NULL);
			(void) pthread_mutex_init (&socket->write_mutex_storage, NULL);
		}
	}

	return socket;

}

static void socket_destroy (void *socket_ptr) {

	Socket *socket = (Socket *) socket_ptr;

	(void) pthread_mutex_destroy (&socket->read_mutex_storage);
	(void) pthread_mutex_destroy (&socket->write_mutex_storage);

	free (socket);

}

// keeps the socket to be reused, or releases it if there are enough
static void socket_recycle (Socket *socket) {

	if (freelist_push (&socket_recycled, socket)) {
		socket_destroy (socket);
	}

}

// the recycled sockets are released when the library is unloaded
__attribute__ ((destructor)) static void socket_recycled_drain (void) {

	freelist_drain (&socket_recycled, socket_destroy);

}

#pragma endregion

#pragma region main

Socket *socket_new (void) {

	Socket *socket = socket_allocate ();
	if (socket) {
		socket->sock_fd = -1;

		socket->packet_buffer = NULL;
		socket->packet_buffer_size = 0;

		socket->read_mutex = NULL;
		socket->write_mutex = NULL;

		socket->references = 1;

		socket->recycle_next = NULL;
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

		// a send that another thread started finishes before the socket is reused
		(void) pthread_mutex_lock (&socket->write_mutex_storage);
		(void) pthread_mutex_unlock (&socket->write_mutex_storage);

		socket_recycle (socket);
	}

}

void *socket_create_empty (void) {

	Socket *socket = socket_new ();
	if (socket) {
		socket->read_mutex = &socket->read_mutex_storage;
		socket->write_mutex = &socket->write_mutex_storage;
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

#pragma endregion
