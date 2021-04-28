#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>
#include <time.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/htab.h"
#include "cerver/collections/dlist.h"

#include "cerver/admin.h"
#include "cerver/auth.h"
#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/receive.h"
#include "cerver/socket.h"

#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

void connection_remove_auth_data (Connection *connection);

static void *connection_update_thread (void *connection_ptr);
static void *connection_send_thread (void *connection_ptr);

const char *connection_state_string (
	const ConnectionState state
) {

	switch (state) {
		#define XX(num, name, string, description) case CONNECTIONS_STATE_##name: return #string;
		CONNECTIONS_STATE_MAP(XX)
		#undef XX
	}

	return connection_state_string (CONNECTIONS_STATE_NONE);

}

const char *connection_state_description (
	const ConnectionState state
) {

	switch (state) {
		#define XX(num, name, string, description) case CONNECTIONS_STATE_##name: return #description;
		CONNECTIONS_STATE_MAP(XX)
		#undef XX
	}

	return connection_state_description (CONNECTIONS_STATE_NONE);

}

#pragma region stats

ConnectionStats *connection_stats_new (void) {

	ConnectionStats *stats = (ConnectionStats *) malloc (sizeof (ConnectionStats));
	if (stats) {
		(void) memset (stats, 0, sizeof (ConnectionStats));
		stats->received_packets = packets_per_type_new ();
		stats->sent_packets = packets_per_type_new ();
	}

	return stats;

}

static inline void connection_stats_delete (ConnectionStats *stats) {

	if (stats) {
		packets_per_type_delete (stats->received_packets);
		packets_per_type_delete (stats->sent_packets);

		free (stats);
	}

}

void connection_stats_print (Connection *connection) {

	if (connection) {
		if (connection->stats) {
			cerver_log_msg ("Threshold time:            %ld", connection->stats->threshold_time);

			cerver_log_msg ("N receives done:           %lu", connection->stats->n_receives_done);

			cerver_log_msg ("Total bytes received:      %lu", connection->stats->total_bytes_received);
			cerver_log_msg ("Total bytes sent:          %lu", connection->stats->total_bytes_sent);

			cerver_log_msg ("N packets received:        %lu", connection->stats->n_packets_received);
			cerver_log_msg ("N packets sent:            %lu", connection->stats->n_packets_sent);

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

#pragma region main

Connection *connection_new (void) {

	Connection *connection = (Connection *) malloc (sizeof (Connection));
	if (connection) {
		connection->client = NULL;

		(void) memset (connection->name, 0, CONNECTION_NAME_SIZE);

		connection->socket = NULL;
		connection->port = 0;
		connection->protocol = CONNECTION_DEFAULT_PROTOCOL;
		connection->use_ipv6 = CONNECTION_DEFAULT_USE_IPV6;

		(void) memset (connection->ip, 0, CONNECTION_IP_SIZE);
		(void) memset (&connection->address, 0, sizeof (struct sockaddr_storage));

		connection->state = CONNECTIONS_STATE_NONE;
		connection->state_mutex = NULL;

		connection->connected_timestamp = 0;

		connection->cerver_report = NULL;

		connection->max_sleep = CONNECTION_DEFAULT_MAX_SLEEP;
		connection->active = false;
		connection->updating = false;

		connection->auth_tries = CONNECTION_DEFAULT_MAX_AUTH_TRIES;
		connection->bad_packets = 0;

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

			.header = (PacketHeader) {
				.packet_type = PACKET_TYPE_NONE,
				.packet_size = 0,
				.handler_id = 0,
				.request_type = 0,
				.sock_fd = 0
			},

			.header_end = NULL,
			.remaining_header = 0,

			.spare_packet = NULL
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
		connection->send_thread_id = 0;
		connection->send_queue = NULL;

		connection->authenticated = false;
		connection->auth_data = NULL;
		connection->auth_data_size = 0;
		connection->delete_auth_data = NULL;
		connection->admin_auth = false;
		connection->auth_packet = NULL;

		connection->stats = NULL;

		connection->cond = NULL;
		connection->mutex = NULL;
	}

	return connection;

}

void connection_delete (void *connection_ptr) {

	if (connection_ptr) {
		Connection *connection = (Connection *) connection_ptr;

		connection->client = NULL;

		socket_delete (connection->socket);

		if (connection->state_mutex)
			pthread_mutex_delete (connection->state_mutex);

		if (connection->active) connection_end (connection);

		cerver_report_delete (connection->cerver_report);

		if (connection->received_data && connection->received_data_delete)
			connection->received_data_delete (connection->received_data);

		if (connection->custom_receive_args) {
			if (connection->custom_receive_args_delete) {
				connection->custom_receive_args_delete (connection->custom_receive_args);
			}
		}

		job_queue_delete (connection->send_queue);

		connection_remove_auth_data (connection);

		connection_stats_delete (connection->stats);

		if (connection->cond)
			pthread_cond_delete (connection->cond);

		if (connection->mutex)
			pthread_mutex_delete (connection->mutex);

		free (connection);
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

		connection->stats = connection_stats_new ();
	}

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

	ConnectionState state = CONNECTIONS_STATE_NONE;

	if (connection) {
		(void) pthread_mutex_lock (connection->state_mutex);

		state = connection->state;

		(void) pthread_mutex_unlock (connection->state_mutex);
	}

	return state;

}

void connection_set_state (
	Connection *connection, const ConnectionState state
) {

	if (connection) {
		(void) pthread_mutex_lock (connection->state_mutex);

		connection->state = state;

		(void) pthread_mutex_unlock (connection->state_mutex);
	}

}

// sets the connection max sleep (wait time) to try to connect to the cerver
void connection_set_max_sleep (Connection *connection, u32 max_sleep) {

	if (connection) connection->max_sleep = max_sleep;

}

// sets if the connection will receive packets or not (default true)
// if true, a new thread is created that handled incoming packets
void connection_set_receive (Connection *connection, bool receive) {

	if (connection) connection->receive_packets = receive;

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

// sets the connection received data
// a place to safely store the request response,
// like when using client_connection_request_to_cerver ()
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
// a reference to the client and connection will be passed to the action
// as a ConnectionCustomReceiveData structure
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
void connection_set_send_queue (
	Connection *connection, int flags
) {

	if (connection) {
		connection->use_send_queue = true;
		connection->send_flags = flags;

		connection->send_queue = job_queue_create (JOB_QUEUE_TYPE_JOBS);
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
				connection->admin_auth ? AUTH_PACKET_TYPE_ADMIN_AUTH : AUTH_PACKET_TYPE_CLIENT_AUTH,
				connection->auth_data, connection->auth_data_size
			);

			if (connection->auth_packet) retval = 0;
		}
	}

	return retval;

}

// sets the ability to attempt a reconnection
// if the connection has been stopped
// ability to set the default time (secs) to wait before connecting
void connection_set_attempt_reconnect (
	Connection *connection, unsigned int wait_time
) {

	if (connection) {
		connection->attempt_reconnect = true;
		connection->reconnect_wait_time = wait_time;
	}

}

// sets up the new connection values
u8 connection_init (Connection *connection) {

	u8 retval = 1;

	if (connection) {
		if (!connection->active) {
			// init the new connection socket
			switch (connection->protocol) {
				case IPPROTO_TCP:
					connection->socket->sock_fd = socket (
						(connection->use_ipv6 == 1 ? AF_INET6 : AF_INET),
						SOCK_STREAM, 0
					);
					break;
				case IPPROTO_UDP:
					connection->socket->sock_fd = socket (
						(connection->use_ipv6 == 1 ? AF_INET6 : AF_INET),
						SOCK_DGRAM, 0
					);
					break;

				default:
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_NONE,
						"Unkonw protocol type!"
					);
					return 1;
			}

			if (connection->socket->sock_fd > 0) {
				if (connection->use_ipv6) {
					struct sockaddr_in6 *addr = (struct sockaddr_in6 *) &connection->address;
					addr->sin6_family = AF_INET6;
					addr->sin6_addr = in6addr_any;
					addr->sin6_port = htons (connection->port);
				}

				else {
					struct sockaddr_in *addr = (struct sockaddr_in *) &connection->address;
					addr->sin_family = AF_INET;
					addr->sin_addr.s_addr = inet_addr (connection->ip);
					addr->sin_port = htons (connection->port);
				}

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

// try to connect a client to an address (server) with exponential backoff
static unsigned int connection_try (
	Connection *connection, const struct sockaddr_storage address
) {

	unsigned int retval = 1;

	u32 numsec = 0;
	for (numsec = 2; numsec <= connection->max_sleep; numsec <<= 1) {
		if (!connect (
			connection->socket->sock_fd,
			(const struct sockaddr *) &address,
			sizeof (struct sockaddr)
		)) {
			retval = 0;
			break;
		}

		if (numsec <= connection->max_sleep / 2) (void) sleep (numsec);
	}

	return retval;

}

// connects to the specified address (ip and port)
// sets connection's state based on result
// returns 0 on success, 1 on error
unsigned int connection_connect (Connection *connection) {

	unsigned int retval = 1;

	if (connection) {
		connection_set_state (connection, CONNECTIONS_STATE_CONNECTING);

		if (!connection_try (connection, connection->address)) {
			connection_set_state (connection, CONNECTIONS_STATE_READY);

			retval = 0;
		}

		else {
			connection_set_state (connection, CONNECTIONS_STATE_UNAVAILABLE);
		}
	}

	return retval;

}

// ends a connection
void connection_end (Connection *connection) {

	if (connection) {
		if (connection->active) {
			close (connection->socket->sock_fd);
			connection->socket->sock_fd = -1;
			connection->active = false;
		}
	}

}

void connection_drop (Cerver *cerver, Connection *connection) {

	if (connection) {
		// close the socket
		connection_end (connection);

		if (cerver) {
			// move the socket to the cerver's socket pool to avoid destroying it
			// to handle if any other thread is waiting to access the socket's mutex
			cerver_sockets_pool_push (cerver, connection->socket);
			connection->socket = NULL;
		}

		connection_delete (connection);
	}

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
		if (!dlist_insert_after (client->connections, dlist_end (client->connections), connection)) {
			connection->client = client;
			
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

// starts listening and receiving data in the connection sock
static void *connection_update_thread (void *connection_ptr) {

	char client_name[THREAD_NAME_BUFFER_SIZE] = { 0 };
	char connection_name[THREAD_NAME_BUFFER_SIZE] = { 0 };

	Connection *connection = (Connection *) connection_ptr;
	Client *client = (Client *) connection->client;

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Client %s - connection %s connection_update () thread has started",
		client->name, connection->name
	);
	#endif

	(void) strncpy (client_name, client->name, THREAD_NAME_BUFFER_SIZE - 1);
	(void) strncpy (connection_name, connection->name, THREAD_NAME_BUFFER_SIZE - 1);

	if (strcmp (CONNECTION_DEFAULT_NAME, connection->name)) {
		(void) thread_set_name (connection_name);
	}

	connection->receive_handle.client = client;
	connection->receive_handle.connection = connection;

	connection->receive_handle.state = RECEIVE_HANDLE_STATE_NORMAL;

	const size_t buffer_size = connection->receive_packet_buffer_size;
	char *buffer = (char *) calloc (buffer_size, sizeof (char));
	if (buffer) {
		(void) sock_set_timeout (
			connection->socket->sock_fd,
			connection->update_timeout
		);

		connection->updating = true;

		// check if we have a custom receive method
		if (connection->custom_receive) {
			ConnectionCustomReceiveData custom_data = {
				.client = client,
				.connection = connection,
				.args = connection->custom_receive_args
			};

			while (
				client->running
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
				client->running
				&& connection->active
				&& !client_receive_internal (
					client, connection,
					buffer, buffer_size
				)
			);
		}

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

	// signal waiting thread
	(void) pthread_mutex_lock (connection->mutex);
	connection->updating = false;
	(void) pthread_cond_signal (connection->cond);
	(void) pthread_mutex_unlock (connection->mutex);

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Client %s - connection %s connection_update () thread has ended",
		client_name, connection_name
	);
	#endif

	return NULL;

}

#pragma endregion

#pragma region send

void connection_send_packet (
	Connection *connection, Packet *packet
) {

	if (connection) {
		(void) job_queue_push (
			connection->send_queue,
			job_create (NULL, packet)
		);
	}

}

static void *connection_send_thread (void *connection_ptr) {

	char client_name[THREAD_NAME_BUFFER_SIZE] = { 0 };
	char connection_name[THREAD_NAME_BUFFER_SIZE] = { 0 };

	Connection *connection = (Connection *) connection_ptr;
	Client *client = (Client *) connection->client;

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Client %s - connection %s connection_send () thread has started",
		client->name, connection->name
	);
	#endif

	(void) strncpy (client_name, client->name, THREAD_NAME_BUFFER_SIZE - 1);
	(void) strncpy (connection_name, connection->name, THREAD_NAME_BUFFER_SIZE - 1);

	Job *job = NULL;
	size_t sent = 0;
	Packet *packet = NULL;
	u8 failed = 0;
	while (connection->active && !failed) {
		bsem_wait (connection->send_queue->has_jobs);

		if (connection->active) {
			job = job_queue_pull (connection->send_queue);
			if (job) {
				packet = (Packet *) job->args;

				failed = packet_send_actual (
					packet,
					connection->send_flags, &sent,
					client, connection
				);

				packet_delete (packet);

				job_delete (job);
			}
		}
	}

	#ifdef CONNECTION_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_CONNECTION,
		"Client %s - connection %s connection_send () thread has ended",
		client_name, connection_name
	);
	#endif

	return NULL;

}

#pragma endregion