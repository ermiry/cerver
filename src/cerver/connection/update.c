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
#include "cerver/client/connect.h"
#include "cerver/client/events.h"
#include "cerver/client/handler.h"
#include "cerver/connection/data.h"
#include "cerver/connection/end.h"
#include "cerver/connection/internal.h"
#include "cerver/connection/lifecycle.h"
#include "cerver/connection/update.h"
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

#pragma region receive

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"

static ConnectionCustomReceiveData *connection_custom_receive_data_new (
	Client *client, Connection *connection, 
	void *args
) {

	ConnectionCustomReceiveData *custom_data =
		(ConnectionCustomReceiveData *) malloc (sizeof (ConnectionCustomReceiveData));
	if (custom_data) {
		custom_data->client = client;
		custom_data->connection = connection;
		custom_data->args = args;
	}

	return custom_data;

}

static inline void connection_custom_receive_data_delete (void *custom_data_ptr) {

	if (custom_data_ptr) free (custom_data_ptr);

}

#pragma GCC diagnostic pop

// the connection that the current thread updates, NULL in any other thread
static __thread const Connection *connection_update_current = NULL;

// returns true if the current thread is the update thread of the connection,
// like a packet handler with direct handle that runs in it
bool connection_is_update_thread (const Connection *connection) {

	return connection && (connection_update_current == connection);

}

// wakes up the update thread of the connection that is blocked in recv (),
// the socket is shut down but it is only closed by connection_end (),
// so its fd can not be reused while the update thread uses it
// the connection's mutex must be held
void connection_wake_update (Connection *connection) {

	if (connection->socket) {
		__atomic_store_n (&connection->shut_down, true, __ATOMIC_RELEASE);
		(void) socket_shutdown (connection->socket);
	}

	// an update thread that waits to reconnect
	if (connection->cond) (void) pthread_cond_broadcast (connection->cond);

}

// the receive loop has ended: the thread that waits for it is woken up,
// and a deferred end (the cerver closed the connection) is completed here
static void connection_update_end (Client *client, Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);
	connection->updating = false;
	const bool deferred = connection->end_deferred;
	(void) pthread_cond_broadcast (connection->cond);
	(void) pthread_mutex_unlock (connection->mutex);

	// any other thread that ends the connection waits for updating, so it must not be used after it,
	// unless this thread owns its end
	if (deferred) client_connection_end_deferred (client, connection);

	connection_update_current = NULL;

	client_update_thread_ended (client);

}

// receives from the connection until it ends or fails
static void connection_update_receive (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size
) {

	connection->receive_handle.client = client;
	connection->receive_handle.connection = connection;

	connection->receive_handle.state = RECEIVE_HANDLE_STATE_NORMAL;

	(void) sock_set_timeout (
		connection->socket->sock_fd,
		connection->update_timeout
	);

	// check if we have a custom receive method
	if (connection->custom_receive) {
		ConnectionCustomReceiveData custom_data = {
			.client = client,
			.connection = connection,
			.args = connection->custom_receive_args
		};

		while (
			client_is_running (client)
			&& connection->active
			&& !connection->custom_receive (
				&custom_data,
				buffer, buffer_size
			)
		);
	}

	// use the default receive method
	// that handles cerver type packages
	else {
		while (
			client_is_running (client)
			&& connection->active
			&& !client_receive_internal (
				client, connection,
				buffer, buffer_size
			)
		);
	}

}

// returns true if the connection must connect again,
// only when the cerver ended it and the client did not ask for its end
static bool connection_update_should_reconnect (
	Client *client, Connection *connection
) {

	bool reconnect = false;

	if (connection->attempt_reconnect && client_is_running (client)) {
		(void) pthread_mutex_lock (connection->mutex);
		reconnect = !connection->ending;
		(void) pthread_mutex_unlock (connection->mutex);
	}

	return reconnect;

}

// waits the reconnect time, the end of the connection wakes it up
// returns true if the connection can attempt to connect
static bool connection_update_reconnect_wait (
	Client *client, Connection *connection
) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) connection->reconnect_wait_time;

	(void) pthread_mutex_lock (connection->mutex);

	int result = 0;
	while (
		!connection->ending
		&& client_is_running (client)
		&& (result != ETIMEDOUT)
	) {
		result = pthread_cond_timedwait (connection->cond, connection->mutex, &deadline);
	}

	const bool proceed = !connection->ending && client_is_running (client);

	(void) pthread_mutex_unlock (connection->mutex);

	return proceed;

}

// a single attempt with a new socket, so the end of the connection
// never waits for an exponential backoff
static bool connection_update_reconnect_attempt (
	Client *client, Connection *connection
) {

	bool connected = false;

	if (!connection_init (connection)) {
		connection_set_state (connection, CONNECTION_STATE_CONNECTING);

		// the host is resolved again, so a cerver that moved to another address is found
		if (!connection_connect_round (connection, true)) {
			connection->active = true;
			(void) time (&connection->connected_timestamp);

			connection_set_state (connection, CONNECTION_STATE_WORKING);

			client_event_trigger (CLIENT_EVENT_CONNECTED, client, connection);

			// an UDP cerver only knows the connection when it sends a datagram
			client_connect_datagram (client, connection);

			connected = true;
		}

		else {
			// the socket of the failed attempt
			(void) socket_close (connection->socket);

			connection_set_state (connection, CONNECTION_STATE_UNAVAILABLE);
		}
	}

	if (!connected) {
		client_event_trigger (CLIENT_EVENT_CONNECTION_FAILED, client, connection);
	}

	return connected;

}

// closes the socket that the cerver ended
static void connection_update_reconnect_close (
	Client *client, Connection *connection
) {

	if (connection->active) {
		client_event_trigger (CLIENT_EVENT_CONNECTION_CLOSE, client, connection);
		connection_end (connection);
	}

	// the cerver sends its info again
	cerver_report_delete (
		__atomic_exchange_n (&connection->cerver_report, NULL, __ATOMIC_ACQ_REL)
	);

	// an UDP connection authenticates again with a new token
	connection_clear_datagram_token (connection);

	// a packet that was only partially received
	receive_handle_end (&connection->receive_handle);

}

// connects again a connection that the cerver ended
// returns true if the connection is connected again
static bool connection_update_reconnect (
	Client *client, Connection *connection
) {

	bool reconnected = false;

	if (connection_update_should_reconnect (client, connection)) {
		connection_update_reconnect_close (client, connection);

		while (
			!reconnected
			&& connection_update_reconnect_wait (client, connection)
		) {
			connection->reconnect_attempts += 1;
			reconnected = connection_update_reconnect_attempt (client, connection);
		}

		if (reconnected) connection->reconnect_attempts = 0;
	}

	return reconnected;

}

// starts listening and receiving data in the connection sock
void *connection_update (void *client_connection_ptr) {

	if (client_connection_ptr) {
		ClientConnection *cc = (ClientConnection *) client_connection_ptr;

		connection_update_current = cc->connection;

		char client_name[THREAD_NAME_BUFFER_SIZE] = { 0 };
		char connection_name[THREAD_NAME_BUFFER_SIZE] = { 0 };

		#ifdef CONNECTION_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
			"Client %s - connection %s connection_update () thread has started",
			cc->client->name, cc->connection->name
		);
		#endif

		(void) snprintf (client_name, THREAD_NAME_BUFFER_SIZE, "%s", cc->client->name);

		if (strcmp (CONNECTION_DEFAULT_NAME, cc->connection->name)) {
			(void) snprintf (
				connection_name, THREAD_NAME_BUFFER_SIZE,
				"%s", cc->connection->name
			);

			(void) thread_set_name ("%s", connection_name);
		}

		const size_t buffer_size = cc->connection->receive_packet_buffer_size;
		char *buffer = (char *) calloc (buffer_size, sizeof (char));
		if (buffer) {
			// a connection with attempt reconnect receives again after it connects again
			do {
				connection_update_receive (
					cc->client, cc->connection,
					buffer, buffer_size
				);
			} while (connection_update_reconnect (cc->client, cc->connection));

			free (buffer);
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CONNECTION,
				"connection_update () - "
				"Failed to allocate buffer for client %s - connection %s!",
				client_name, connection_name
			);
		}

		Client *client = cc->client;
		Connection *connection = cc->connection;

		client_connection_aux_delete (cc);

		connection_update_end (client, connection);

		#ifdef CONNECTION_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
			"Client %s - connection %s connection_update () thread has ended",
			client_name, connection_name
		);
		#endif
	}

	return NULL;

}

#pragma endregion
