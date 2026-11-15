#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/admin/admin.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/internal.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region recycle

static FreeList connection_recycled = FREELIST_INITIALIZER (
	Connection, recycle_next, CONNECTION_RECYCLE_LIMIT
);

// a new connection initializes its synchronization values once,
// a recycled one keeps them
Connection *connection_allocate (void) {

	Connection *connection = (Connection *) freelist_pop (&connection_recycled);
	if (!connection) {
		connection = (Connection *) malloc (sizeof (Connection));
		if (connection) {
			(void) pthread_mutex_init (&connection->state_mutex_storage, NULL);
			(void) pthread_cond_init (&connection->cond_storage, NULL);
			(void) pthread_mutex_init (&connection->mutex_storage, NULL);
		}
	}

	return connection;

}

static void connection_destroy (void *connection_ptr) {

	Connection *connection = (Connection *) connection_ptr;

	(void) pthread_mutex_destroy (&connection->state_mutex_storage);
	(void) pthread_cond_destroy (&connection->cond_storage);
	(void) pthread_mutex_destroy (&connection->mutex_storage);

	free (connection);

}

// keeps the connection to be reused, or releases it if there are enough
void connection_recycle (Connection *connection) {

	if (freelist_push (&connection_recycled, connection)) {
		connection_destroy (connection);
	}

}

// the recycled connections are released when the library is unloaded
__attribute__ ((destructor)) static void connection_recycled_drain (void) {

	freelist_drain (&connection_recycled, connection_destroy);

}

// only the values that were not taken from the connection's storage are released
void connection_synchronization_delete (Connection *connection) {

	if (connection->state_mutex != &connection->state_mutex_storage) {
		thread_mutex_delete (connection->state_mutex);
	}

	if (connection->cond != &connection->cond_storage) {
		thread_cond_delete (connection->cond);
	}

	if (connection->mutex != &connection->mutex_storage) {
		thread_mutex_delete (connection->mutex);
	}

}

#pragma endregion
