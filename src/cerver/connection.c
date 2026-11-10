#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <errno.h>
#include <unistd.h>
#include <time.h>

#include <poll.h>

#include <sys/socket.h>

#include <arpa/inet.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/htab.h"
#include "cerver/collections/dlist.h"

#include "cerver/admin.h"
#include "cerver/auth.h"
#include "cerver/cerver.h"
#include "cerver/sessions.h"
#include "cerver/client.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/socket.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

void connection_remove_auth_data (Connection *connection);

static void *connection_send_thread (void *connection_ptr);

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

#pragma region stats

static void connection_stats_init (ConnectionStats *stats) {

	(void) memset (stats, 0, sizeof (ConnectionStats));

	stats->received_packets = &stats->received_packets_storage;
	stats->sent_packets = &stats->sent_packets_storage;

}

ConnectionStats *connection_stats_new (void) {

	ConnectionStats *stats = (ConnectionStats *) malloc (sizeof (ConnectionStats));
	if (stats) {
		connection_stats_init (stats);
	}

	return stats;

}

// the stats of a connection live inside it,
// only stats that were created with connection_stats_new () are released
static inline void connection_stats_delete (
	Connection *connection, ConnectionStats *stats
) {

	if (stats && (stats != &connection->stats_storage)) {
		free (stats);
	}

}

void connection_stats_print (Connection *connection) {

	if (connection) {
		if (connection->stats) {
			cerver_log_msg ("Threshold time:            %ld", connection->stats->threshold_time);

			cerver_log_msg ("N receives done:           %ld", connection->stats->n_receives_done);

			cerver_log_msg ("Total bytes received:      %ld", connection->stats->total_bytes_received);
			cerver_log_msg ("Total bytes sent:          %ld", connection->stats->total_bytes_sent);

			cerver_log_msg ("N packets received:        %ld", connection->stats->n_packets_received);
			cerver_log_msg ("N packets sent:            %ld", connection->stats->n_packets_sent);

			cerver_log_msg ("\nReceived packets:");
			packets_per_type_print (connection->stats->received_packets);

			cerver_log_msg ("\nSent packets:");
			packets_per_type_print (connection->stats->sent_packets);
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_NONE,
				"Connection does not have a reference to a connection stats!"
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_NONE,
			"Can't get stats of a NULL connection!"
		);
	}

}

#pragma endregion

#pragma region recycle

static FreeList connection_recycled = FREELIST_INITIALIZER (
	Connection, recycle_next, CONNECTION_RECYCLE_LIMIT
);

// a new connection initializes its synchronization values once,
// a recycled one keeps them
static Connection *connection_allocate (void) {

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
static void connection_recycle (Connection *connection) {

	if (freelist_push (&connection_recycled, connection)) {
		connection_destroy (connection);
	}

}

// the recycled connections are released when the library is unloaded
__attribute__ ((destructor)) static void connection_recycled_drain (void) {

	freelist_drain (&connection_recycled, connection_destroy);

}

// only the values that were not taken from the connection's storage are released
static void connection_synchronization_delete (Connection *connection) {

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

	if (connection) connection->receive_packet_buffer_size = size;

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

// sets the connection auth data to send whenever the cerver requires authentication
// and a method to destroy it once the connection has ended,
// if delete_auth_data is NULL, the auth data won't be deleted
void connection_set_auth_data (
	Connection *connection,
	void *auth_data, size_t auth_data_size, Action delete_auth_data,
	bool admin_auth
) {

	if (connection && auth_data) {
		connection_remove_auth_data (connection);

		connection->auth_data = auth_data;
		connection->auth_data_size = auth_data_size;
		connection->delete_auth_data = delete_auth_data;
		connection->admin_auth = admin_auth;
	}

}

// encodes the object as the auth data of the connection
// returns 0 on success, 1 on error
static u8 connection_set_auth_encoded (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	const bool admin_auth, const bool token
) {

	u8 retval = 1;

	const size_t size = serializer_encoded_size (schema, object);
	void *buffer = size ? malloc (size) : NULL;

	if (buffer && (serializer_encode (schema, object, buffer, size, NULL) == SERIALIZER_RESULT_NONE)) {
		connection_set_auth_data (connection, buffer, size, free, admin_auth);
		connection->auth_serialized = true;
		connection->auth_token = token;

		retval = 0;
	}

	else {
		free (buffer);
	}

	return retval;

}

// sets the credentials as an object of the auth schema of the cerver (cerver_set_auth_schema ()),
// the object is encoded now, so it can be released after this
// returns 0 on success, 1 if the object can not be encoded
u8 connection_set_auth_object (
	Connection *connection,
	const SerializerSchema *schema, const void *object,
	bool admin_auth
) {

	return (connection && schema && object)
		? connection_set_auth_encoded (connection, schema, object, admin_auth, false)
		: 1;

}

// sets the session token that a cerver with sessions gave to the client,
// to authenticate the connection with it instead of the credentials
// returns 0 on success, 1 on error
u8 connection_set_auth_token (
	Connection *connection, const char *token,
	bool admin_auth
) {

	u8 retval = 1;

	if (connection && token && (strlen (token) < TOKEN_SIZE)) {
		SessionToken session_token = { { 0 } };
		(void) strncpy (session_token.token, token, TOKEN_SIZE - 1);

		retval = connection_set_auth_encoded (
			connection, &session_token_schema, &session_token, admin_auth, true
		);
	}

	return retval;

}

// the request type of the auth packet: the credentials or a session token, of a client or an admin
static u32 connection_auth_request_type (const Connection *connection) {

	u32 request_type = connection->admin_auth ? AUTH_PACKET_TYPE_ADMIN_AUTH : AUTH_PACKET_TYPE_CLIENT_AUTH;

	if (connection->auth_token) {
		request_type = connection->admin_auth ? AUTH_PACKET_TYPE_ADMIN_TOKEN : AUTH_PACKET_TYPE_CLIENT_TOKEN;
	}

	return request_type;

}

// removes the connection auth data using the connection's delete_auth_data method
// if not such method, the data won't be deleted
// the connection's auth data & delete method will be equal to NULL
void connection_remove_auth_data (Connection *connection) {

	if (connection) {
		if (connection->auth_data) {
			if (connection->delete_auth_data)
				connection->delete_auth_data (connection->auth_data);
		}

		if (connection->auth_packet) {
			packet_delete (connection->auth_packet);
			connection->auth_packet = NULL;
		}

		connection->auth_data = NULL;
		connection->auth_data_size = 0;
		connection->delete_auth_data = NULL;
		connection->auth_serialized = false;
		connection->auth_token = false;
	}

}

// generates the connection auth packet to be send to the server
// this is also generated automatically whenever the cerver ask for authentication
// returns 0 on success, 1 on error
u8 connection_generate_auth_packet (Connection *connection) {

	u8 retval = 1;

	if (connection) {
		if (connection->auth_data) {
			connection->auth_packet = packet_generate_request (
				PACKET_TYPE_AUTH,
				connection_auth_request_type (connection),
				connection->auth_data, connection->auth_data_size
			);

			// an encoded object is sent with its flag
			if (connection->auth_packet && connection->auth_serialized) {
				connection->auth_packet->header.flags |= PACKET_FLAG_SERIALIZED;
				if (packet_generate (connection->auth_packet)) {
					packet_delete (connection->auth_packet);
					connection->auth_packet = NULL;
				}
			}

			if (connection->auth_packet) retval = 0;
		}
	}

	return retval;

}

// sets the token that an authenticated UDP connection sends after the packet in every datagram
// the token is written before the flag, a sender that sees the flag reads it complete
void connection_set_datagram_token (
	Connection *connection, const u8 *token
) {

	if (connection && token) {
		(void) memcpy (connection->datagram_token, token, CONNECTION_DATAGRAM_TOKEN_SIZE);
		__atomic_store_n (&connection->datagram_token_set, true, __ATOMIC_RELEASE);
	}

}

// copies the datagram token of the connection into token
// returns true if the connection has one
bool connection_get_datagram_token (
	const Connection *connection, u8 *token
) {

	bool has_token = false;

	if (connection && __atomic_load_n (&connection->datagram_token_set, __ATOMIC_ACQUIRE)) {
		(void) memcpy (token, connection->datagram_token, CONNECTION_DATAGRAM_TOKEN_SIZE);
		has_token = true;
	}

	return has_token;

}

// the connection sends its datagrams without a token again
void connection_clear_datagram_token (Connection *connection) {

	if (connection) {
		__atomic_store_n (&connection->datagram_token_set, false, __ATOMIC_RELEASE);
	}

}

// returns true if the connection sends a token in its datagrams (it authenticated)
bool connection_has_datagram_token (const Connection *connection) {

	return connection && __atomic_load_n (&connection->datagram_token_set, __ATOMIC_ACQUIRE);

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
static u8 connection_connect_round (Connection *connection, const bool fresh) {

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

// gets the connection from the on hold connections map in cerver
Connection *connection_get_by_sock_fd_from_on_hold (
	Cerver *cerver, const i32 sock_fd
) {

	Connection *connection = NULL;

	if (cerver) {
		const i32 *key = &sock_fd;
		void *connection_data = htab_get (
			cerver->on_hold_connection_sock_fd_map,
			key, sizeof (i32)
		);

		if (connection_data) connection = (Connection *) connection_data;
	}

	return connection;

}

// gets the connection from the client by its sock fd
Connection *connection_get_by_sock_fd_from_client (
	Client *client, const i32 sock_fd
) {

	Connection *retval = NULL;

	if (client) {
		Connection *con = NULL;
		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			con = (Connection *) le->data;
			if (con->socket->sock_fd == sock_fd) {
				retval = con;
				break;
			}
		}
	}

	return retval;

}

// gets the connection from the admin cerver by its sock fd
Connection *connection_get_by_sock_fd_from_admin (
	AdminCerver *admin_cerver, const i32 sock_fd
) {

	Connection *retval = NULL;

	if (admin_cerver) {
		Connection *con = NULL;
		for (ListElement *le = dlist_start (admin_cerver->admins); le; le = le->next) {
			for (ListElement *le_sub = dlist_start (((Admin *) le->data)->client->connections); le_sub; le_sub = le_sub->next) {
				con = (Connection *) le_sub->data;
				if (con->socket->sock_fd == sock_fd) {
					retval = con;
					break;
				}
			}
		}
	}

	return retval;

}

// checks if the connection belongs to the client
bool connection_check_owner (Client *client, Connection *connection) {

	if (client && connection) {
		for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
			if (connection->socket->sock_fd == ((Connection *) le->data)->socket->sock_fd) return true;
		}
	}

	return false;

}

// registers a new connection to a client without adding it to the cerver poll
// returns 0 on success, 1 on error
u8 connection_register_to_client (Client *client, Connection *connection) {

	u8 retval = 1;

	if (client && connection) {
		connection->client = client;

		if (!dlist_insert_at_end (client->connections, connection)) {
			#ifdef CERVER_DEBUG
			if (client->session_id) {
				cerver_log (
					LOG_TYPE_SUCCESS, LOG_TYPE_CLIENT,
					"Registered a new connection to client with session id: %s",
					client->session_id->str
				);
			}

			else {
				cerver_log (
					LOG_TYPE_SUCCESS, LOG_TYPE_CLIENT,
					"Registered a new connection to client (id): %ld",
					client->id
				);
			}
			#endif

			retval = 0;
		}
	}

	return retval;

}

// registers the client connection to the cerver's strcutures (like maps)
// returns 0 on success, 1 on error
u8 connection_register_to_cerver (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver && client && connection) {
		// map the socket fd with the client
		const void *key = &connection->socket->sock_fd;
		retval = (u8) htab_insert (cerver->client_sock_fd_map, key, sizeof (i32),
			client, sizeof (Client));
	}

	return retval;

}

// unregister the client connection from the cerver's structures (like maps)
// returns 0 on success, 1 on error
u8 connection_unregister_from_cerver (
	Cerver *cerver, Connection *connection
) {

	u8 retval = 1;

	if (cerver && connection) {
		// remove the sock fd from each map
		const void *key = &connection->socket->sock_fd;
		if (htab_remove (cerver->client_sock_fd_map, key, sizeof (i32))) {
			// cerver_log_success (
			// 	"Removed sock fd %d from cerver's %s client sock map.",
			//     connection->socket->sock_fd, cerver->info->name
			// );

			retval = 0;
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_CERVER,
				"Failed to remove sock fd %d from cerver's %s client sock map.",
				connection->socket->sock_fd, cerver->info->name
			);
			#endif
		}
	}

	return retval;

}

// wrapper function for easy access
// registers a client connection to the cerver poll array
// returns 0 on success, 1 on error
u8 connection_register_to_cerver_poll (
	Cerver *cerver, Connection *connection
) {

	return (cerver && connection) ?
		cerver_poll_register_connection (cerver, connection) : 1;

}

// wrapper function for easy access
// unregisters a client connection from the cerver poll array
// returns 0 on success, 1 on error
u8 connection_unregister_from_cerver_poll (
	Cerver *cerver, Connection *connection
) {

	return (cerver && connection) ?
		cerver_poll_unregister_connection (cerver, connection) : 1;

}

// first adds the client connection to the cerver's poll array, and upon success,
// adds the connection to the cerver's structures
// this method is equivalent to call connection_register_to_cerver_poll () & connection_register_to_cerver
// returns 0 on success, 1 on error
u8 connection_add_to_cerver (
	Cerver *cerver, Client *client, Connection *connection
) {

	u8 retval = 1;

	if (cerver && client && connection) {
		if (!connection_register_to_cerver_poll (cerver, connection)) {
			retval = connection_register_to_cerver (cerver, client, connection);
		}
	}

	return retval;

}

// removes the connection's sock fd from the cerver's poll array and then removes the connection
// from the cerver's structures
// this method is equivalent to call connection_unregister_from_cerver_poll () & connection_unregister_from_cerver ()
// returns 0 on success, 1 on error
u8 connection_remove_from_cerver (
	Cerver *cerver, Connection *connection
) {

	u8 errors = 0;

	if (cerver && connection) {
		switch (cerver->handler_type) {
			case CERVER_HANDLER_TYPE_POLL:
				errors |= connection_unregister_from_cerver_poll (cerver, connection);
				break;

			default: break;
		}

		errors |= connection_unregister_from_cerver (cerver, connection);
	}

	return errors;

}

#pragma endregion

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

			(void) thread_set_name (connection_name);
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

#pragma region send

// pushes a packet to the connection's send queue,
// the send thread sends it and deletes it
void connection_send_packet (
	Connection *connection, Packet *packet
) {

	if (connection && connection->send_queue && packet) {
		// the job comes from the pool of the queue
		Job *job = job_get (connection->send_queue);
		if (job) {
			job->args = packet;

			if (job_queue_push (connection->send_queue, job)) {
				job_return (connection->send_queue, job);
				packet_delete (packet);
			}
		}

		else {
			packet_delete (packet);
		}
	}

}

// sends a queued packet, returns 0 on success, 1 on error
static u8 connection_send_thread_send (Connection *connection, Job *job) {

	Packet *packet = (Packet *) job->args;

	struct timespec start = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &start);

	size_t sent = 0;
	const u8 failed = packet_send_actual (
		packet,
		connection->send_flags, &sent,
		connection->client, connection
	);

	packet_delete (packet);

	job_queue_record_run (connection->send_queue, &start);
	job_return (connection->send_queue, job);

	return failed;

}

// the send thread was asked to end, read with the lock of the send queue
static bool connection_send_thread_stopped (const void *connection_ptr) {

	const Connection *connection = (const Connection *) connection_ptr;

	return !__atomic_load_n (&connection->send_thread_running, __ATOMIC_SEQ_CST);

}

static void *connection_send_thread (void *connection_ptr) {

	Connection *connection = (Connection *) connection_ptr;

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Connection %s connection_send () thread has started",
		connection->name
	);
	#endif

	// a failed send does not end the thread, the connection can be connected again
	while (!connection_send_thread_stopped (connection)) {
		Job *job = (Job *) job_queue_pull_interruptible (
			connection->send_queue, connection_send_thread_stopped, connection
		);

		if (job) (void) connection_send_thread_send (connection, job);
	}

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Connection %s connection_send () thread has ended",
		connection->name
	);
	#endif

	return NULL;

}

#pragma endregion
