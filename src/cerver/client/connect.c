#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/avl.h"
#include "cerver/collections/dlist.h"

#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/client/connect.h"
#include "cerver/client/events.h"
#include "cerver/client/internal.h"
#include "cerver/client/requests.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/data.h"
#include "cerver/connection/end.h"
#include "cerver/connection/lifecycle.h"
#include "cerver/connection/update.h"
#include "cerver/core/cerver.h"
#include "cerver/core/serialization.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/udp.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region connect

// the request of the cerver info has the biggest size of the info,
// a cerver does not answer an unproven address with more bytes than it sent (udp.c)
static void client_connect_datagram_info (Client *client, Connection *connection) {

	u8 padding[CERVER_SUMMARY_MAX_ENCODED_SIZE] = { 0 };

	Packet *request = packet_generate_request (
		PACKET_TYPE_CERVER, CERVER_PACKET_TYPE_INFO,
		padding, CERVER_SUMMARY_MAX_ENCODED_SIZE
	);

	if (request) {
		packet_set_network_values (request, NULL, client, connection, NULL);
		(void) packet_send (request, 0, NULL, false);
		packet_delete (request);
	}

}

static void client_connect_datagram_auth (Client *client, Connection *connection) {

	if (!connection->auth_packet) {
		(void) connection_generate_auth_packet (connection);
	}

	if (connection->auth_packet) {
		packet_set_network_values (connection->auth_packet, NULL, client, connection, NULL);

		if (!packet_send (connection->auth_packet, 0, NULL, false)) {
			client_event_trigger (CLIENT_EVENT_AUTH_SENT, client, connection);
		}
	}

}

// an UDP cerver only knows a connection when it sends a datagram:
// the connection asks for the cerver info and sends its auth packet (if it has auth data)
void client_connect_datagram (Client *client, Connection *connection) {

	if (client && connection && (connection->protocol == PROTOCOL_UDP)) {
		client_connect_datagram_info (client, connection);

		if (connection->auth_data) {
			client_connect_datagram_auth (client, connection);
		}
	}

}

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is a blocking method, as it will wait until the connection has been successfull or a timeout
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 when the connection has been established, 1 on error or failed to connect
unsigned int client_connect (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		if (!connection_connect (connection)) {
			connection->active = true;
			(void) time (&connection->connected_timestamp);

			// the event sees the connection already active
			client_event_trigger (CLIENT_EVENT_CONNECTED, client, connection);

			client_connect_datagram (client, connection);

			retval = 0;     // success - connected to cerver
		}

		else {
			client_event_trigger (
				CLIENT_EVENT_CONNECTION_FAILED,
				client, connection
			);
		}
	}

	return retval;

}

// connects a client to the host with the specified values in the connection
// performs a first read to get the cerver info packet
// this is a blocking method, and works exactly the same as if only calling client_connect ()
// returns 0 when the connection has been established, 1 on error or failed to connect
unsigned int client_connect_to_cerver (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (!client_connect (client, connection)) {
		// we expect to handle a packet with the cerver's information
		client_connection_get_next_packet (
			client, connection
		);

		retval = 0;
	}

	return retval;

}

// counts an async thread that is about to use the client (1),
// or one that does not use it anymore (-1)
void client_async_thread_count (Client *client, const int delta) {

	if (!client->lock) return;

	(void) pthread_mutex_lock (client->lock);

	if (delta > 0) client->n_async_threads += 1;
	else client->n_async_threads -= 1;

	(void) pthread_cond_broadcast (&client->updates_cond);
	(void) pthread_mutex_unlock (client->lock);

}

// creates a detached thread that uses the client,
// and releases its data if the thread could not be created
// returns 0 on success, 1 on error
unsigned int client_async_thread_create (
	ClientConnection *cc, void *(*work) (void *)
) {

	Client *client = cc->client;

	client_async_thread_count (client, 1);

	pthread_t thread_id = 0;
	const unsigned int retval = thread_create_detachable (&thread_id, work, cc);
	if (retval) {
		client_connection_aux_delete (cc);
		client_async_thread_count (client, -1);
	}

	return retval;

}

static void *client_connect_thread (void *client_connection_ptr) {

	if (client_connection_ptr) {
		ClientConnection *cc = (ClientConnection *) client_connection_ptr;
		Client *client = cc->client;

		// triggers CLIENT_EVENT_CONNECTED or CLIENT_EVENT_CONNECTION_FAILED
		if (!client_connect (cc->client, cc->connection)) {
			client_start (cc->client);
		}

		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// connects a client to the host with the specified values in the connection
// it can be a cerver or not
// this is NOT a blocking method, a new thread will be created to wait for a connection to be established
// open a success connection, EVENT_CONNECTED will be triggered, otherwise, EVENT_CONNECTION_FAILED will be triggered
// user must manually handle how he wants to receive / handle incomming packets and also send requests
// returns 0 on success connection thread creation, 1 on error
unsigned int client_connect_async (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		ClientConnection *cc = client_connection_aux_new (client, connection);
		if (cc) {
			retval = client_async_thread_create (cc, client_connect_thread);

			#ifdef CLIENT_DEBUG
			if (retval) cerver_log_error ("Failed to create client_connect_thread () detachable thread!");
			#endif
		}
	}

	return retval;

}

static void *client_connect_to_cerver_thread (void *client_connection_ptr) {

	if (client_connection_ptr) {
		ClientConnection *cc = (ClientConnection *) client_connection_ptr;
		Client *client = cc->client;

		// handles the packet with the cerver's information
		(void) client_connect_to_cerver (cc->client, cc->connection);

		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// works like client_connect_to_cerver ()
// this is NOT a blocking method, a new thread will be created,
// client_requests_wait () and client_teardown () wait for it
// returns 0 on success creating connection thread, 1 on error
unsigned int client_connect_to_cerver_async (
	Client *client, Connection *connection
) {

	unsigned int retval = 1;

	if (client && connection) {
		ClientConnection *cc = client_connection_aux_new (client, connection);
		if (cc) {
			retval = client_async_thread_create (cc, client_connect_to_cerver_thread);

			#ifdef CLIENT_DEBUG
			if (retval) cerver_log_error ("Failed to create client_connect_to_cerver_thread () detachable thread!");
			#endif
		}
	}

	return retval;

}

#pragma endregion

#pragma region start

// after a client connection successfully connects to a server,
// it will start the connection's update thread to enable the connection to
// receive & handle packets in a dedicated thread
// returns 0 on success, 1 on error
int client_connection_start (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		if (connection->active) {
			if (!client_start (client) && !connection_start (connection)) {
				// set before the thread exists, so an end that arrives first waits for it
				client_update_thread_started (client, connection, true);

				if (!thread_create_detachable (
					&connection->update_thread_id,
					connection_update,
					client_connection_aux_new (client, connection)
				)) {
					retval = 0;         // success
				}

				else {
					client_update_thread_started (client, connection, false);

					cerver_log_error (
						"client_connection_start () - Failed to create update thread for client %s",
						client->name
					);
				}
			}

			else {
				cerver_log_error (
					"client_connection_start () - Failed to start client %s",
					client->name
				);
			}
		}
	}

	return retval;

}

// connects a client connection to a server
// and after a success connection, it will start the connection (create update thread for receiving messages)
// this is a blocking method, returns only after a success or failed connection
// returns 0 on success, 1 on error
int client_connect_and_start (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		if (!client_connect (client, connection)) {
			if (!client_connection_start (client, connection)) {
				retval = 0;
			}
		}

		else {
			cerver_log_error (
				"client_connect_and_start () - Client %s failed to connect",
				client->name
			);
		}
	}

	return retval;

}

static void *client_connection_start_wrapper (void *data_ptr) {

	if (data_ptr) {
		ClientConnection *cc = (ClientConnection *) data_ptr;
		Client *client = cc->client;

		(void) client_connect_and_start (cc->client, cc->connection);
		client_connection_aux_delete (cc);

		client_async_thread_count (client, -1);
	}

	return NULL;

}

// connects a client connection to a server in a new thread to avoid blocking the calling thread,
// and after a success connection, it will start the connection (create update thread for receiving messages)
// returns 0 on success creating connection thread, 1 on error
u8 client_connect_and_start_async (Client *client, Connection *connection) {

	u8 retval = 1;

	ClientConnection *cc = (client && connection) ?
		client_connection_aux_new (client, connection) : NULL;

	if (cc) {
		retval = (u8) client_async_thread_create (cc, client_connection_start_wrapper);
	}

	return retval;

}

#pragma endregion
#pragma region end

// ends a connection with a cerver
// by sending a disconnect packet and the closing the connection
static void client_connection_terminate (
	Client *client, Connection *connection
) {

	if (connection) {
		if (connection->active) {
			if (__atomic_load_n (&connection->cerver_report, __ATOMIC_ACQUIRE)) {
				// send a close connection packet
				Packet *packet = packet_generate_request (
					PACKET_TYPE_CLIENT, CLIENT_PACKET_TYPE_CLOSE_CONNECTION, NULL, 0
				);

				if (packet) {
					packet_set_network_values (packet, NULL, client, connection, NULL);
					if (packet_send (packet, 0, NULL, false)) {
						cerver_log_error ("Failed to send CLIENT_CLOSE_CONNECTION!");
					}
					packet_delete (packet);
				}
			}
		}
	}

}

// closes the connection's socket & set it to be inactive
// does not send a close connection packet to the cerver
// returns 0 on success, 1 on error
int client_connection_stop (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		client_event_trigger (CLIENT_EVENT_CONNECTION_CLOSE, client, connection);
		connection_end (connection);

		retval = 0;
	}

	return retval;

}

// terminates the connection & closes the socket
// but does NOT destroy the current connection
// returns 0 on success, 1 on error
int client_connection_close (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		client_connection_terminate (client, connection);
		retval = client_connection_stop (client, connection);
	}

	return retval;

}

// returns true while the client is running, it is read by its update threads
bool client_is_running (const Client *client) {

	return client ? __atomic_load_n (&client->running, __ATOMIC_SEQ_CST) : false;

}

// the connections are removed by their address, the comparator of the list uses their fd
static int client_connection_same (const void *a, const void *b) {

	return (a == b) ? 0 : 1;

}

// returns true if the connection is in the client's list, the client's lock must be held
static bool client_connection_is_registered (
	const Client *client, const Connection *connection
) {

	bool registered = false;

	for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
		if (le->data == connection) {
			registered = true;
			break;
		}
	}

	return registered;

}

// an update thread is about to be created (started is true) or it failed to be created
void client_update_thread_started (
	Client *client, Connection *connection, const bool started
) {

	(void) pthread_mutex_lock (connection->mutex);
	connection->updating = started;
	(void) pthread_mutex_unlock (connection->mutex);

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);
		if (started) client->n_update_threads += 1;
		else client->n_update_threads -= 1;
		(void) pthread_mutex_unlock (client->lock);
	}

}

// an update thread of the client has ended and it does not use the client anymore
void client_update_thread_ended (Client *client) {

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);
		client->n_update_threads -= 1;
		(void) pthread_cond_broadcast (&client->updates_cond);
		(void) pthread_mutex_unlock (client->lock);
	}

}

// closes the connection, removes it from the client and deletes it
// it runs only in the thread that owns the end of the connection,
// when its update thread (if any) does not use it anymore
// a client that the cerver left without connections (remote) stops running, like when it was disconnected
static void client_connection_destroy (
	Client *client, Connection *connection, const bool remote
) {

	(void) client_connection_stop (client, connection);

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	(void) dlist_remove (client->connections, connection, client_connection_same);

	if (remote && !dlist_size (client->connections)) {
		__atomic_store_n (&client->running, false, __ATOMIC_SEQ_CST);
	}

	(void) pthread_cond_broadcast (&client->updates_cond);

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

	// the socket of a connection that never connected, or whose connect failed,
	// is still open: only connection_end () closes it, and only for an active connection
	(void) socket_close (connection->socket);

	connection_delete (connection);

}

// the ownership of the end of a connection
typedef enum ClientConnectionEnd {

	CLIENT_CONNECTION_END_NONE			= 0,
	CLIENT_CONNECTION_END_OWNER			= 1,	// this thread ends the connection
	CLIENT_CONNECTION_END_TAKEN			= 2,	// the end deferred to the update thread is taken by this thread
	CLIENT_CONNECTION_END_OTHER			= 3,	// another thread ends the connection

	CLIENT_CONNECTION_END_MAX			= 4

} ClientConnectionEnd;

// claims the end of the connection, the connection's mutex must be held
static ClientConnectionEnd client_connection_claim_end (
	Connection *connection, const bool deferred
) {

	ClientConnectionEnd end = CLIENT_CONNECTION_END_OTHER;

	if (!connection->ending) {
		connection->ending = true;
		connection->end_deferred = deferred;
		end = CLIENT_CONNECTION_END_OWNER;

		connection_set_state (connection, CONNECTION_STATE_DISCONNECTING);
	}

	// the update thread has not finished its loop yet, so its deferred end can be taken
	else if (!deferred && connection->end_deferred && connection->updating) {
		connection->end_deferred = false;
		end = CLIENT_CONNECTION_END_TAKEN;
	}

	return end;

}

// claims the end of a connection of the client, the client's lock is held
// so the connection can not be destroyed by its update thread meanwhile
static ClientConnectionEnd client_connection_claim_registered (
	Client *client, Connection *connection
) {

	ClientConnectionEnd end = CLIENT_CONNECTION_END_NONE;

	if (client_connection_is_registered (client, connection)) {
		(void) pthread_mutex_lock (connection->mutex);
		end = client_connection_claim_end (connection, false);
		(void) pthread_mutex_unlock (connection->mutex);
	}

	return end;

}

// waits until the update thread of the connection that ends in another thread removes it,
// the client's lock is held
static void client_connection_wait_removed (Client *client, Connection *connection) {

	while (client_connection_is_registered (client, connection)) {
		(void) pthread_cond_wait (&client->updates_cond, client->lock);
	}

}

// wakes up the update thread of the connection and waits until its receive loop ends
static void client_connection_wait_update (Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);

	if (connection->updating) {
		connection_wake_update (connection);

		while (connection->updating) {
			(void) pthread_cond_wait (connection->cond, connection->mutex);
		}
	}

	(void) pthread_mutex_unlock (connection->mutex);

}

// a connection that ends from its own update thread (a packet handler with direct handle):
// its loop is woken up and it completes the end when it finishes
static int client_connection_end_from_update (Client *client, Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);
	const ClientConnectionEnd end = client_connection_claim_end (connection, true);
	(void) pthread_mutex_unlock (connection->mutex);

	if (end == CLIENT_CONNECTION_END_OWNER) {
		client_connection_terminate (client, connection);

		(void) pthread_mutex_lock (connection->mutex);
		connection_wake_update (connection);
		(void) pthread_mutex_unlock (connection->mutex);
	}

	return 0;

}

// a connection that ends from any other thread: its update thread is woken up and joined
static int client_connection_end_actual (Client *client, Connection *connection) {

	int retval = 0;

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	const ClientConnectionEnd end = client->lock ?
		client_connection_claim_registered (client, connection) : CLIENT_CONNECTION_END_OWNER;

	// its update thread is ending it, it returns once the connection was removed
	if (end == CLIENT_CONNECTION_END_OTHER) client_connection_wait_removed (client, connection);

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

	switch (end) {
		case CLIENT_CONNECTION_END_OWNER:
			client_connection_terminate (client, connection);
			client_connection_wait_update (connection);
			client_connection_destroy (client, connection, false);
			break;

		// the cerver already closed it, so the close packet is not sent
		case CLIENT_CONNECTION_END_TAKEN:
			client_connection_wait_update (connection);
			client_connection_destroy (client, connection, false);
			break;

		case CLIENT_CONNECTION_END_OTHER: break;

		default: retval = 1; break;
	}

	return retval;

}

// terminates and destroy a connection registered to a client
// that is connected to a cerver
// the first thread that ends a connection owns its end, any other one waits for it
// a packet handler that runs in the connection's update thread only requests the end,
// the update thread completes it when its receive loop ends
// returns 0 on success, 1 on error
int client_connection_end (Client *client, Connection *connection) {

	int retval = 1;

	if (client && connection) {
		retval = connection_is_update_thread (connection) ?
			client_connection_end_from_update (client, connection) :
			client_connection_end_actual (client, connection);
	}

	return retval;

}

// completes the end of a connection that its update thread owns,
// called by the update thread when its receive loop has ended
void client_connection_end_deferred (Client *client, Connection *connection) {

	if (client && connection) {
		client_connection_destroy (client, connection, true);
	}

}

// requests the end of a connection without waiting for it:
// a connection with an update thread is ended by it when its receive loop ends
// returns true if its update thread ends it, false if it has no update thread
bool client_connection_end_request (Connection *connection) {

	(void) pthread_mutex_lock (connection->mutex);

	const bool updating = connection->updating;
	if (updating) {
		(void) client_connection_claim_end (connection, true);
		connection_wake_update (connection);
	}

	(void) pthread_mutex_unlock (connection->mutex);

	return updating;

}

// each handler thread is woken and joined, a handler that never started is skipped
static void client_handlers_destroy (Client *client) {

	if (client) {
		#ifdef CLIENT_DEBUG
		cerver_log_debug (
			"Client %s num_handlers_alive: %u",
			client->name, client->num_handlers_alive
		);
		#endif

		handler_stop (client->app_packet_handler);

		handler_stop (client->app_error_packet_handler);

		handler_stop (client->custom_packet_handler);
	}

}

// returns the first connection of the client, NULL if it has none
static Connection *client_connections_first (Client *client) {

	if (client->lock) (void) pthread_mutex_lock (client->lock);

	ListElement *le = dlist_start (client->connections);
	Connection *connection = le ? (Connection *) le->data : NULL;

	if (client->lock) (void) pthread_mutex_unlock (client->lock);

	return connection;

}

// ends every connection, each end returns once its connection was removed
// returns 0 on success, 1 if it was called from an update thread of the client
static u8 client_teardown_connections (Client *client) {

	u8 retval = 0;

	Connection *connection = NULL;
	while ((connection = client_connections_first (client))) {
		// an update thread can not wait for itself
		if (connection_is_update_thread (connection)) {
			cerver_log_error (
				"client_teardown () - client %s can not be destroyed from the update thread of one of its connections",
				client->name
			);

			retval = 1;
			break;
		}

		(void) client_connection_end (client, connection);
	}

	return retval;

}

// waits until every update thread has stopped using the client
static void client_teardown_wait_updates (Client *client) {

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);

		while (client->n_update_threads) {
			(void) pthread_cond_wait (&client->updates_cond, client->lock);
		}

		(void) pthread_mutex_unlock (client->lock);
	}

}

// waits until the async connect and request threads do not use the client,
// an async connect can still add and start a connection
static void client_teardown_wait_async (Client *client) {

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);

		while (client->n_async_threads) {
			(void) pthread_cond_wait (&client->updates_cond, client->lock);
		}

		(void) pthread_mutex_unlock (client->lock);
	}

}

void client_requests_wait (Client *client) {

	if (client) client_teardown_wait_async (client);

}

// marks the teardown and wakes the connections that wait in the connect backoff,
// a connection that starts its backoff later sees the mark first
static void client_teardown_cancel_connects (Client *client) {

	__atomic_store_n (&client->teardown_requested, true, __ATOMIC_SEQ_CST);

	if (client->lock) {
		(void) pthread_mutex_lock (client->lock);

		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			connection_wake_connect ((Connection *) le->data);
		}

		(void) pthread_mutex_unlock (client->lock);
	}

}

// true when client_teardown () was called, the connections stop their connect backoff
bool client_teardown_is_requested (const Client *client) {

	return client ? __atomic_load_n (&client->teardown_requested, __ATOMIC_SEQ_CST) : false;

}

// returns 0 on success, 1 on error
static u8 client_teardown_actual (Client *client) {

	u8 retval = 1;

	client_teardown_wait_async (client);

	if (!client_teardown_connections (client)) {
		client_teardown_wait_updates (client);

		client_handlers_destroy (client);

		// no connection or handler can trigger an event after this
		(void) thread_group_wait (&client->event_threads, THREAD_GROUP_WAIT_FOREVER);

		// the connections were already deleted
		dlist_delete (client->connections);
		client->connections = NULL;

		client_delete (client);

		retval = 0;
	}

	return retval;

}

static void *client_teardown_internal (void *client_ptr) {

	if (client_ptr) {
		(void) client_teardown_actual ((Client *) client_ptr);
	}

	return NULL;

}

// stop any on going connection and process and destroys the client
// every connection is ended (their update threads are woken up and joined)
// returns 0 on success, 1 on error
u8 client_teardown (Client *client) {

	u8 retval = 1;

	if (client) {
		__atomic_store_n (&client->running, false, __ATOMIC_SEQ_CST);

		client_teardown_cancel_connects (client);

		retval = client_teardown_actual (client);
	}

	return retval;

}

// calls client_teardown () in a new thread as handlers might need time to stop
// that will cause the calling thread to wait at least a second
// returns 0 on success creating thread, 1 on error
u8 client_teardown_async (Client *client) {

	pthread_t thread_id = 0;
	return client ? thread_create_detachable (
		&thread_id,
		client_teardown_internal,
		client
	) : 1;

}

#pragma endregion
