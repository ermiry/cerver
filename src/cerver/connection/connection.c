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
#include "cerver/client/handler.h"
#include "cerver/connection/data.h"
#include "cerver/connection/end.h"
#include "cerver/connection/internal.h"
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

#pragma region states

const char *connection_state_string (
	const ConnectionState state
) {

	switch (state) {
		#define XX(num, name, string, description) case CONNECTION_STATE_##name: return #string;
		CONNECTION_STATE_MAP(XX)
		#undef XX
	}

	return connection_state_string (CONNECTION_STATE_NONE);

}

const char *connection_state_description (
	const ConnectionState state
) {

	switch (state) {
		#define XX(num, name, string, description) case CONNECTION_STATE_##name: return #description;
		CONNECTION_STATE_MAP(XX)
		#undef XX
	}

	return connection_state_description (CONNECTION_STATE_NONE);

}

#pragma endregion

#pragma region main

Connection *connection_new (void) {

	Connection *connection = connection_allocate ();
	if (connection) {
		connection->client = NULL;

		(void) memset (connection->name, 0, CONNECTION_NAME_SIZE);

		connection->socket = NULL;
		connection->port = 0;
		connection->protocol = CONNECTION_DEFAULT_PROTOCOL;
		connection->use_ipv6 = CONNECTION_DEFAULT_USE_IPV6;

		(void) memset (connection->ip, 0, CONNECTION_IP_SIZE);
		(void) memset (&connection->address, 0, sizeof (struct sockaddr_storage));

		connection->state = CONNECTION_STATE_NONE;
		connection->state_mutex = NULL;

		connection->connected_timestamp = 0;

		connection->cerver_report = NULL;

		connection->max_sleep = CONNECTION_DEFAULT_MAX_SLEEP;
		connection->connect_timeout = CONNECTION_DEFAULT_CONNECT_TIMEOUT;
		connection->active = false;
		connection->updating = false;
		connection->ending = false;
		connection->end_deferred = false;
		connection->shut_down = false;

		connection->auth_tries = CONNECTION_DEFAULT_MAX_AUTH_TRIES;
		connection->bad_packets = 0;

		connection->receive_flags = CONNECTION_DEFAULT_RECEIVE_FLAGS;
		connection->receive_packet_buffer_size = CONNECTION_DEFAULT_RECEIVE_BUFFER_SIZE;

		connection->receive_handle = (ReceiveHandle) {
			.type = RECEIVE_TYPE_NONE,

			.cerver = NULL,

			.socket = NULL,
			.connection = NULL,
			.client = NULL,
			.admin = NULL,

			.lobby = NULL,

			.buffer = NULL,
			.buffer_size = 0,
			.received_size = 0,

			.state = RECEIVE_HANDLE_STATE_NONE,

			.header = { 0 },

			.header_end = NULL,
			.remaining_header = 0,

			.spare_packet = NULL,

			.trailing = NULL,
			.trailing_size = 0,
			.trailing_taken = 0
		};

		connection->update_thread_id = 0;
		connection->update_timeout = CONNECTION_DEFAULT_UPDATE_TIMEOUT;

		connection->received_data = NULL;
		connection->received_data_size = 0;
		connection->received_data_delete = NULL;

		connection->receive_packets = CONNECTION_DEFAULT_RECEIVE_PACKETS;

		connection->custom_receive = NULL;
		connection->custom_receive_args = NULL;
		connection->custom_receive_args_delete = NULL;

		connection->use_send_queue = CONNECTION_DEFAULT_USE_SEND_QUEUE;
		connection->send_flags = CONNECTION_DEFAULT_SEND_FLAGS;
		connection->send_timeout = CERVER_DEFAULT_SEND_TIMEOUT;
		connection->send_thread_running = false;
		connection->send_thread_id = 0;
		connection->send_queue = NULL;

		connection->authenticated = false;
		connection->auth_data = NULL;
		connection->auth_data_size = 0;
		connection->delete_auth_data = NULL;
		connection->admin_auth = false;
		connection->auth_serialized = false;
		connection->auth_token = false;
		connection->auth_packet = NULL;

		(void) memset (connection->datagram_token, 0, CONNECTION_DATAGRAM_TOKEN_SIZE);
		connection->datagram_token_set = false;

		connection->route_id = 0;

		connection->lobby = NULL;

		connection->attempt_reconnect = CONNECTION_DEFAULT_ATTEMPT_RECONNECT;
		connection->reconnect_wait_time = CONNECTION_DEFAULT_RECONNECT_WAIT_TIME;
		connection->reconnect_attempts = 0;

		connection->stats = NULL;

		connection->cond = NULL;
		connection->mutex = NULL;

		connection->recycle_next = NULL;
	}

	return connection;

}

void connection_delete (void *connection_ptr) {

	if (connection_ptr) {
		Connection *connection = (Connection *) connection_ptr;

		// connection_end () closes the socket, so it must run before deleting it
		if (connection->active) connection_end (connection);

		// a packet that was only partially received
		receive_handle_end (&connection->receive_handle);

		socket_delete (connection->socket);

		cerver_report_delete (connection->cerver_report);

		if (connection->received_data && connection->received_data_delete)
			connection->received_data_delete (connection->received_data);

		if (connection->custom_receive_args) {
			if (connection->custom_receive_args_delete) {
				connection->custom_receive_args_delete (connection->custom_receive_args);
			}
		}

		connection_remove_auth_data (connection);

		// the send thread was already joined by the owner of the connection's end
		connection_stop_send (connection);
		job_queue_delete (connection->send_queue);

		connection_stats_delete (connection, connection->stats);

		connection_synchronization_delete (connection);

		connection_recycle (connection);
	}

}

Connection *connection_create_empty (void) {

	Connection *connection = connection_new ();
	if (connection) {
		(void) strncpy (
			connection->name,
			CONNECTION_DEFAULT_NAME,
			CONNECTION_NAME_SIZE - 1
		);

		connection->socket = (Socket *) socket_create_empty ();

		connection->state_mutex = &connection->state_mutex_storage;

		connection_stats_init (&connection->stats_storage);
		connection->stats = &connection->stats_storage;
	}

	return connection;

}

// sets the cond and the mutex that a connection uses to connect to a cerver
void connection_set_synchronization (Connection *connection) {

	if (connection) {
		connection->cond = &connection->cond_storage;
		connection->mutex = &connection->mutex_storage;
	}

}

// creates an empty connection with its synchronization values,
// ready to connect to a cerver
Connection *connection_create_complete (void) {

	Connection *connection = connection_create_empty ();
	connection_set_synchronization (connection);

	return connection;

}

Connection *connection_create (
	const i32 sock_fd, const struct sockaddr_storage *address,
	const Protocol protocol
) {

	Connection *connection = connection_create_empty ();
	if (connection) {
		connection->socket->sock_fd = sock_fd;
		(void) memcpy (&connection->address, address, sizeof (struct sockaddr_storage));
		connection->protocol = protocol;

		connection_get_values (connection);
	}

	return connection;

}

// compare two connections by their socket fds
int connection_comparator (const void *a, const void *b) {

	if (a && b) {
		Connection *con_a = (Connection *) a;
		Connection *con_b = (Connection *) b;

		if (con_a->socket && con_b->socket) {
			if (con_a->socket->sock_fd < con_b->socket->sock_fd) return -1;
			else if (con_a->socket->sock_fd == con_b->socket->sock_fd) return 0;
			else return 1;
		}
	}

	return -1;

}

// sets the connection's name
void connection_set_name (
	Connection *connection, const char *name
) {

	if (connection) {
		(void) strncpy (connection->name, name, CONNECTION_NAME_SIZE - 1);
	}

}

// get from where the client is connecting
void connection_get_values (Connection *connection) {

	if (connection) {
		(void) sock_ip_to_string_actual (
			(const struct sockaddr *) &connection->address,
			connection->ip
		);

		connection->port = sock_ip_port (
			(const struct sockaddr *) &connection->address
		);
	}

}

// sets the connection's newtwork values
void connection_set_values (
	Connection *connection,
	const char *ip_address, u16 port, Protocol protocol, bool use_ipv6
) {

	if (connection) {
		(void) strncpy (connection->ip, ip_address, CONNECTION_IP_SIZE - 1);

		connection->port = port;
		connection->protocol = protocol;
		connection->use_ipv6 = use_ipv6;

		connection->active = false;
	}

}

ConnectionState connection_get_state (Connection *connection) {

	ConnectionState state = CONNECTION_STATE_NONE;

	if (connection && connection->state_mutex) {
		(void) pthread_mutex_lock (connection->state_mutex);

		state = connection->state;

		(void) pthread_mutex_unlock (connection->state_mutex);
	}

	return state;

}

void connection_set_state (
	Connection *connection, const ConnectionState state
) {

	if (connection && connection->state_mutex) {
		(void) pthread_mutex_lock (connection->state_mutex);

		connection->state = state;

		(void) pthread_mutex_unlock (connection->state_mutex);
	}

}

// sets the connection max sleep (wait time) to try to connect to the cerver
void connection_set_max_sleep (Connection *connection, u32 max_sleep) {

	if (connection) connection->max_sleep = max_sleep;

}

// sets how long a connect () to each address of the cerver can take, in milliseconds,
// after it the next resolved address is tried (default CONNECTION_DEFAULT_CONNECT_TIMEOUT)
void connection_set_connect_timeout (Connection *connection, u32 timeout) {

	if (connection) connection->connect_timeout = timeout;

}

// sets if the connection will receive packets or not (default true)
// if true, a new thread is created that handled incoming packets
void connection_set_receive (Connection *connection, bool receive) {

	if (connection) connection->receive_packets = receive;

}

// sets the flags to be passed to recv () when handling packets
void connection_set_receive_flags (Connection *connection, int flags) {

	if (connection) connection->receive_flags = flags;

}

// read packets into a buffer of this size in client_receive ()
// by default the value RECEIVE_PACKET_BUFFER_SIZE is used
void connection_set_receive_buffer_size (Connection *connection, u32 size) {

	if (connection && size) connection->receive_packet_buffer_size = size;

}

// sets the timeout (in secs) the connection's socket will have
// this refers to the time the socket will block waiting for new data to araive
// note that this only has effect in connection_update ()
void connection_set_update_timeout (Connection *connection, u32 timeout) {

	if (connection) connection->update_timeout = timeout;

}

// sets how many secs a send waits for space in a full non blocking socket
void connection_set_send_timeout (Connection *connection, u32 timeout) {

	if (connection) connection->send_timeout = timeout;

}

// sets the connection received data
// 01/01/2020 - a place to safely store the request response, like when using client_connection_request_to_cerver ()
void connection_set_received_data (
	Connection *connection,
	void *data, size_t data_size, Action data_delete
) {

	if (connection) {
		connection->received_data = data;
		connection->received_data_size = data_size;
		connection->received_data_delete = data_delete;
	}

}

// sets a custom receive method to handle incomming packets in the connection
// a reference to the client and connection will be passed to the action as a ConnectionCustomReceiveData structure
// alongside the arguments passed to this method
// the method must return 0 on success & 1 on error
void connection_set_custom_receive (
	Connection *connection, 
	u8 (*custom_receive) (
		void *custom_data_ptr,
		char *buffer, const size_t buffer_size
	),
	void *args, void (*args_delete)(void *)
) {

	if (connection) {
		connection->custom_receive = custom_receive;
		connection->custom_receive_args = args;
		connection->custom_receive_args_delete = args_delete;
		if (connection->custom_receive) connection->receive_packets = true;
	}

}

// enables the ability to send packets using the connection's queue
// a dedicated thread will be created to send queued packets
// when the connection starts, and it is joined when it is destroyed
void connection_set_send_queue (
	Connection *connection, int flags
) {

	if (connection && !connection->send_queue) {
		connection->send_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
		if (connection->send_queue) {
			connection->use_send_queue = true;
			connection->send_flags = flags;
		}
	}

}

// sets the ability to attempt a reconnection when the cerver ends the connection
// the update thread waits wait_time secs before each attempt,
// until it connects again or the connection is ended by the client
void connection_set_attempt_reconnect (
	Connection *connection, unsigned int wait_time
) {

	if (connection) {
		connection->attempt_reconnect = true;
		connection->reconnect_wait_time = wait_time;
	}

}

#pragma endregion
