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
#include "cerver/connection/data.h"
#include "cerver/connection/end.h"
#include "cerver/connection/internal.h"
#include "cerver/connection/lifecycle.h"
#include "cerver/core/cerver.h"
#include "cerver/core/report.h"
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

#pragma region end

// the packets that were not sent are deleted
static void connection_send_queue_clear (Connection *connection) {

	Job *job = NULL;
	while ((job = (Job *) job_queue_pull (connection->send_queue))) {
		packet_delete (job->args);
		job_return (connection->send_queue, job);
	}

}

// waits until the send thread ends, the connection must not be active
void connection_stop_send (Connection *connection) {

	if (connection && connection->send_queue) {
		if (connection->send_thread_id) {
			__atomic_store_n (&connection->send_thread_running, false, __ATOMIC_SEQ_CST);
			job_queue_signal (connection->send_queue);

			(void) pthread_join (connection->send_thread_id, NULL);
			connection->send_thread_id = 0;
		}

		connection_send_queue_clear (connection);
	}

}

static void connection_reset_received_data (Connection *connection) {

	if (connection->received_data && connection->received_data_delete) {
		connection->received_data_delete (connection->received_data);
	}

	connection->received_data = NULL;
	connection->received_data_size = 0;

}

// resets connection values to be able to connect it again,
// the connection must not have an update thread
void connection_reset (Connection *connection) {

	if (connection) {
		if (connection->active) connection_end (connection);

		connection_stop_send (connection);

		// the socket of an init that never connected is closed too
		(void) socket_close (connection->socket);

		(void) memset (&connection->address, 0, sizeof (struct sockaddr_storage));

		connection_set_state (connection, CONNECTION_STATE_NONE);

		connection->connected_timestamp = 0;

		cerver_report_delete (
			__atomic_exchange_n (&connection->cerver_report, NULL, __ATOMIC_ACQ_REL)
		);

		if (connection->mutex) (void) pthread_mutex_lock (connection->mutex);
		connection->updating = false;
		connection->ending = false;
		connection->end_deferred = false;
		if (connection->mutex) (void) pthread_mutex_unlock (connection->mutex);

		connection->auth_tries = CONNECTION_DEFAULT_MAX_AUTH_TRIES;
		connection->bad_packets = 0;

		// a packet that was only partially received
		receive_handle_end (&connection->receive_handle);

		connection->update_thread_id = 0;

		connection_reset_received_data (connection);

		connection->authenticated = false;

		// a connection that connects again authenticates again
		connection_clear_datagram_token (connection);

		connection->reconnect_attempts = 0;

		// a new socket and the address of the cerver
		(void) connection_init (connection);
	}

}

// ends a connection
void connection_end (Connection *connection) {

	if (connection) {
		if (connection->active) {
			#ifdef CONNECTION_DEBUG
			cerver_log_with_date (
				LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
				"connection_end () - close fd %d - %s:%u",
				connection->socket->sock_fd,
				connection->ip, (unsigned int) connection->port
			);
			#endif

			// with the close mutex, so a shutdown from another thread
			// never reaches the fd after it is closed and reused
			const int sock_fd = connection->socket->sock_fd;
			if (socket_close (connection->socket)) {
				cerver_log_with_date (
					LOG_TYPE_WARNING, LOG_TYPE_CONNECTION,
					"connection_end () - close () failed - fd %d - errno %d",
					sock_fd, errno
				);
			}

			connection_set_state (connection, CONNECTION_STATE_DISCONNECTED);
			connection->active = false;
		}
	}

}

void connection_drop (Cerver *cerver, Connection *connection) {

	(void) cerver;

	if (connection) {
		// close the socket
		connection_end (connection);

		// every connection owns its socket, so it is deleted with it
		connection_delete (connection);
	}

}

// only shuts down the socket, the fd is closed by the receive loop when it removes the connection,
// so it can not be reused while the handler or the loop still use it
u8 connection_drop_request (Connection *connection) {

	u8 retval = 1;

	if (connection && connection->socket) {
		__atomic_store_n (&connection->shut_down, true, __ATOMIC_RELEASE);
		retval = socket_shutdown (connection->socket);
	}

	return retval;

}

#pragma endregion
