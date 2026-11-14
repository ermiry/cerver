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
#include "cerver/connection/lifecycle.h"
#include "cerver/connection/update.h"
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

#pragma region connect

// the ip of the connection can be a literal IPv4 or IPv6 address or a host name,
// the family of the socket is the one of the first resolved address,
// use_ipv6 stays as the preferred family for every new resolution
// returns 0 on success, 1 on error
static u8 connection_init_address (Connection *connection) {

	u8 retval = 1;

	if (!network_resolve_address (
		connection->ip, connection->port, connection->use_ipv6, &connection->address
	)) {
		retval = 0;
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_NONE,
			"Connection %s failed to resolve %s!",
			connection->name, connection->ip
		);
	}

	return retval;

}

static int connection_init_socket_type (const Protocol protocol) {

	int type = -1;

	switch (protocol) {
		case IPPROTO_TCP: type = SOCK_STREAM; break;
		case IPPROTO_UDP: type = SOCK_DGRAM; break;

		default:
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_NONE,
				"Unkonw protocol type!"
			);
			break;
	}

	return type;

}

// a datagram is received whole in a single recv (), a smaller buffer would truncate it
static void connection_init_datagram_buffer (Connection *connection) {

	if (
		(connection->protocol == PROTOCOL_UDP)
		&& (connection->receive_packet_buffer_size < MAX_UDP_PACKET_SIZE)
	) {
		connection->receive_packet_buffer_size = MAX_UDP_PACKET_SIZE;
	}

}

// sets up the new connection values
u8 connection_init (Connection *connection) {

	u8 retval = 1;

	if (connection) {
		if (!connection->active) {
			const int type = connection_init_socket_type (connection->protocol);
			if ((type < 0) || connection_init_address (connection)) return 1;

			// init the new connection socket, a socket from an earlier init is closed
			socket_replace (connection->socket, socket (connection->address.ss_family, type, 0));

			if (connection->socket->sock_fd > -1) {
				// a new socket that was not shut down
				__atomic_store_n (&connection->shut_down, false, __ATOMIC_RELEASE);

				connection_init_datagram_buffer (connection);

				retval = 0;     // connection setup was successfull
			}

			else {
				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_NONE,
					"Failed to create new socket!"
				);
			}
		}

		else {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_NONE,
				"Failed to init connection -- it is already active!"
			);
		}

	}

	return retval;

}

// true when the connection must stop its connect backoff:
// it is ending, or its client is being torn down
static bool connection_connect_cancelled (const Connection *connection) {

	return connection->ending || client_teardown_is_requested (connection->client);

}

// waits the seconds of a backoff step in the connection's condition,
// connection_wake_connect () and the end of the connection interrupt it
// returns true if the connection can attempt to connect again
// the connection always has its synchronization, see connection_connect_synchronization ()
static bool connection_try_wait (Connection *connection, const u32 seconds) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);
	deadline.tv_sec += (time_t) seconds;

	(void) pthread_mutex_lock (connection->mutex);

	int result = 0;
	while (!connection_connect_cancelled (connection) && (result != ETIMEDOUT)) {
		result = pthread_cond_timedwait (connection->cond, connection->mutex, &deadline);
	}

	const bool proceed = !connection_connect_cancelled (connection);

	(void) pthread_mutex_unlock (connection->mutex);

	return proceed;

}

// wakes up the connection that waits in the backoff of connection_connect (),
// it stops waiting if its client was torn down
void connection_wake_connect (Connection *connection) {

	if (connection && connection->mutex && connection->cond) {
		(void) pthread_mutex_lock (connection->mutex);
		(void) pthread_cond_broadcast (connection->cond);
		(void) pthread_mutex_unlock (connection->mutex);
	}

}

// a new socket of the family of the address, in place of the current one,
// a socket whose connect () failed can not be used again
// returns 0 on success, 1 on error
static u8 connection_socket_replace (
	Connection *connection, const struct sockaddr_storage *address
) {

	const int type = connection_init_socket_type (connection->protocol);
	socket_replace (connection->socket, (type < 0) ? -1 : socket (address->ss_family, type, 0));

	return (connection->socket->sock_fd >= 0) ? 0 : 1;

}

// the result of a non blocking connect () that finished
// returns 0 if the socket is connected, 1 on error
static u8 connection_connect_result (const int sock_fd) {

	int error = 0;
	socklen_t length = (socklen_t) sizeof (int);

	return (!getsockopt (sock_fd, SOL_SOCKET, SO_ERROR, &error, &length) && !error) ? 0 : 1;

}

// waits up to the connect timeout for a non blocking connect (),
// in steps of CONNECTION_CONNECT_POLL_INTERVAL ms, so the end of the connection
// or a client teardown stops the wait
// returns 0 if the socket connected, 1 on error, timeout or cancel
static u8 connection_connect_wait (Connection *connection) {

	u8 retval = 1;

	struct pollfd connecting;
	connecting.fd = connection->socket->sock_fd;
	connecting.events = POLLOUT;
	connecting.revents = 0;

	u32 waited = 0;
	bool waiting = true;
	while (waiting) {
		const u32 remaining = (connection->connect_timeout > waited) ?
			(connection->connect_timeout - waited) : 0;
		const u32 step = (remaining < CONNECTION_CONNECT_POLL_INTERVAL) ?
			remaining : CONNECTION_CONNECT_POLL_INTERVAL;

		const int ready = poll (&connecting, 1, (int) step);
		if (ready > 0) {
			retval = connection_connect_result (connecting.fd);
			waiting = false;
		}

		else if ((ready < 0) && (errno != EINTR)) {
			waiting = false;
		}

		else if (ready == 0) {
			waited += step;
			waiting = (waited < connection->connect_timeout)
				&& !connection_connect_cancelled (connection);
		}
	}

	return retval;

}

// one connect () to the address that can take up to the connect timeout,
// the socket is back in blocking mode after it
// returns 0 on success, 1 on error
static u8 connection_connect_address (
	Connection *connection, const struct sockaddr_storage *address
) {

	u8 retval = 1;

	const int sock_fd = connection->socket->sock_fd;
	if (sock_set_blocking (sock_fd, false)) {
		// the real size of the address family, sizeof (struct sockaddr) breaks IPv6
		if (!connect (sock_fd, (const struct sockaddr *) address, sock_address_size (address))) {
			retval = 0;
		}

		else if (errno == EINPROGRESS) {
			retval = connection_connect_wait (connection);
		}

		(void) sock_set_blocking (sock_fd, true);
	}

	return retval;

}

// the addresses of the host are resolved again in every round,
// if the host can not be resolved the last address is tried
static void connection_connect_addresses (
	const Connection *connection, NetworkAddresses *addresses
) {

	if (network_resolve_addresses (
		connection->ip, connection->port,
		connection->use_ipv6, connection->protocol,
		addresses
	)) {
		addresses->addresses[0] = connection->address;
		addresses->count = 1;
	}

}

// one connect () to each resolved address in turn, the first one that connects
// is kept as the address of the connection
// a fresh socket is used for the first address if it has the same family,
// every other attempt gets a new socket
// returns 0 on success, 1 if none of them connected
u8 connection_connect_round (Connection *connection, const bool fresh) {

	u8 retval = 1;

	NetworkAddresses addresses;
	connection_connect_addresses (connection, &addresses);

	bool reuse = fresh && (connection->socket->sock_fd >= 0);
	for (
		unsigned int index = 0;
		retval && (index < addresses.count) && !connection_connect_cancelled (connection);
		index++
	) {
		const struct sockaddr_storage *address = &addresses.addresses[index];
		const bool same_family = (connection->address.ss_family == address->ss_family);

		if ((reuse && same_family) || !connection_socket_replace (connection, address)) {
			if (!connection_connect_address (connection, address)) {
				connection->address = *address;
				retval = 0;
			}
		}

		reuse = false;
	}

	return retval;

}

// try to connect a client to the addresses of the cerver with exponential backoff
// between each round, each wait can be interrupted by the end of the connection or a client teardown
static u8 connection_try (Connection *connection) {

	// the first attempt is always done, a max sleep smaller than the first wait
	// never attempted to connect
	u32 numsec = CONNECTION_FIRST_RETRY_SECONDS;
	bool fresh = true;
	while (connection_connect_round (connection, fresh)) {
		fresh = false;

		if (numsec > connection->max_sleep) return 1;

		if (!connection_try_wait (connection, numsec)) return 1;

		numsec <<= 1;
	}

	return 0;

}

// a connection without its mutex or its condition takes the ones that it has embedded,
// so its backoff can always be interrupted by connection_wake_connect () and its end
// a mutex or a condition that was set before is kept
static void connection_connect_synchronization (Connection *connection) {

	if (!connection->mutex) connection->mutex = &connection->mutex_storage;
	if (!connection->cond) connection->cond = &connection->cond_storage;

}

// starts a connection -> connects to the specified ip and port
// every address that the ip resolves to is tried in turn, see connection_set_connect_timeout ()
// a connection without synchronization gets it before it starts, see connection_set_synchronization ()
// sets connection's state based on result
// returns 0 on success, 1 on error
int connection_connect (Connection *connection) {

	int retval = 1;

	if (connection) {
		connection_connect_synchronization (connection);

		connection_set_state (connection, CONNECTION_STATE_CONNECTING);

		retval = connection_try (connection);

		// a socket whose connect () failed can not be used again,
		// the next connect starts with a new one
		if (retval) (void) socket_close (connection->socket);

		connection_set_state (
			connection,
			retval ? CONNECTION_STATE_UNAVAILABLE : CONNECTION_STATE_READY
		);
	}

	return retval;

}

// asks the update thread of a connection with attempt reconnect
// to close its socket and to connect again
// returns 0 on success, 1 on error
unsigned int connection_reconnect (Connection *connection) {

	unsigned int retval = 1;

	if (connection && connection->attempt_reconnect && connection->mutex) {
		(void) pthread_mutex_lock (connection->mutex);

		if (connection->updating && !connection->ending) {
			connection_wake_update (connection);
			retval = 0;
		}

		(void) pthread_mutex_unlock (connection->mutex);
	}

	return retval;

}

static unsigned int connection_start_send (Connection *connection) {

	unsigned int retval = 1;

	__atomic_store_n (&connection->send_thread_running, true, __ATOMIC_SEQ_CST);

	if (!thread_create_joinable (
		&connection->send_thread_id,
		connection_send_thread,
		connection
	)) {
		retval = 0;
	}

	else {
		__atomic_store_n (&connection->send_thread_running, false, __ATOMIC_SEQ_CST);

		cerver_log_error (
			"connection_start () - "
			"Failed to create send thread for connection %s",
			connection->name
		);
	}

	return retval;

}

// starts the threads of a connection that is connected,
// like the send queue thread
// returns 0 on success, 1 on error
unsigned int connection_start (Connection *connection) {

	unsigned int retval = 1;

	if (connection && connection->active) {
		retval = 0;

		if (
			connection->use_send_queue
			&& !__atomic_load_n (&connection->send_thread_running, __ATOMIC_SEQ_CST)
		) {
			retval = connection_start_send (connection);
		}

		if (!retval) connection_set_state (connection, CONNECTION_STATE_WORKING);
	}

	return retval;

}

#pragma endregion
