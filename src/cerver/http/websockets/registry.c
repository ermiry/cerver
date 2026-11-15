#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/core/cerver.h"

#include "cerver/http/core/http.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/websockets/frame.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

const char *http_web_socket_rejection_to_string (const HttpWebSocketRejection rejection) {

	switch (rejection) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_REJECTION_##name: return #string;
		HTTP_WEB_SOCKET_REJECTION_MAP(XX)
		#undef XX
	}

	return "Undefined";

}

HttpWebSocketRegistry *http_web_socket_registry_create (void) {

	HttpWebSocketRegistry *registry = (HttpWebSocketRegistry *) calloc (1, sizeof (HttpWebSocketRegistry));
	if (registry) {
		(void) pthread_mutex_init (&registry->mutex, NULL);

		registry->first = NULL;
		registry->count = 0;
		registry->reserved = 0;
		registry->max_connections = HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS;
		registry->broadcast_timeout = HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT;
		registry->stopping = false;
		registry->next_id = 0;
	}

	return registry;

}

void http_web_socket_registry_delete (HttpWebSocketRegistry *registry) {

	if (registry) {
		(void) pthread_mutex_destroy (&registry->mutex);
		free (registry);
	}

}

bool http_web_socket_registry_reserve (HttpWebSocketRegistry *registry) {

	bool reserved = false;

	if (registry) {
		(void) pthread_mutex_lock (&registry->mutex);

		if (!registry->stopping && (registry->reserved < registry->max_connections)) {
			registry->reserved += 1;
			reserved = true;
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return reserved;

}

void http_web_socket_registry_unreserve (HttpWebSocketRegistry *registry) {

	if (registry) {
		(void) pthread_mutex_lock (&registry->mutex);
		if (registry->reserved) registry->reserved -= 1;
		(void) pthread_mutex_unlock (&registry->mutex);
	}

}

u8 http_web_socket_registry_add (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
) {

	u8 retval = 1;

	if (registry && web_socket) {
		(void) pthread_mutex_lock (&registry->mutex);

		if (!registry->stopping) {
			registry->next_id += 1;
			web_socket->id = registry->next_id;

			web_socket->previous = NULL;
			web_socket->next = registry->first;
			if (registry->first) registry->first->previous = web_socket;
			registry->first = web_socket;

			web_socket->registered = true;
			registry->count += 1;

			(void) http_web_socket_retain (web_socket);

			CERVER_STATS_ADD (registry->n_opened, 1);

			retval = 0;
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return retval;

}

void http_web_socket_registry_remove (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
) {

	bool removed = false;

	if (registry && web_socket) {
		(void) pthread_mutex_lock (&registry->mutex);

		if (web_socket->registered) {
			if (web_socket->previous) web_socket->previous->next = web_socket->next;
			else registry->first = web_socket->next;

			if (web_socket->next) web_socket->next->previous = web_socket->previous;

			web_socket->previous = NULL;
			web_socket->next = NULL;
			web_socket->registered = false;
			web_socket->n_rooms = 0;

			registry->count -= 1;
			if (registry->reserved) registry->reserved -= 1;

			CERVER_STATS_ADD (registry->n_closed, 1);

			removed = true;
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	if (removed) http_web_socket_release (web_socket);

}

void http_web_socket_registry_go_away (HttpWebSocketRegistry *registry) {

	if (registry) {
		(void) pthread_mutex_lock (&registry->mutex);

		registry->stopping = true;

		for (HttpWebSocket *web_socket = registry->first; web_socket; web_socket = web_socket->next) {
			http_web_socket_go_away (web_socket);
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

}

void http_web_socket_registry_abort (HttpWebSocketRegistry *registry) {

	if (registry) {
		(void) pthread_mutex_lock (&registry->mutex);

		registry->stopping = true;

		for (HttpWebSocket *web_socket = registry->first; web_socket; web_socket = web_socket->next) {
			http_web_socket_abort (web_socket);
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

}

void http_web_socket_registry_count_rejection (
	HttpWebSocketRegistry *registry, const HttpWebSocketRejection rejection
) {

	if (registry && ((unsigned int) rejection < HTTP_WEB_SOCKET_REJECTIONS)) {
		CERVER_STATS_ADD (registry->n_rejected[rejection], 1);
	}

}

void http_web_socket_registry_count_close (
	HttpWebSocketRegistry *registry, const u16 code
) {

	if (registry) {
		if (
			(code >= HTTP_WEB_SOCKET_CLOSE_CODES_FIRST)
			&& (code < (HTTP_WEB_SOCKET_CLOSE_CODES_FIRST + HTTP_WEB_SOCKET_CLOSE_CODES_SIZE))
		) {
			CERVER_STATS_ADD (registry->n_closes[code - HTTP_WEB_SOCKET_CLOSE_CODES_FIRST], 1);
		}

		else {
			CERVER_STATS_ADD (registry->n_closes_other, 1);
		}
	}

}

#pragma region public

void http_cerver_set_web_socket_max_connections (
	HttpCerver *http_cerver, const unsigned int max_connections
) {

	if (http_cerver && http_cerver->web_sockets) {
		(void) pthread_mutex_lock (&http_cerver->web_sockets->mutex);
		http_cerver->web_sockets->max_connections = max_connections ?
			max_connections : HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS;
		(void) pthread_mutex_unlock (&http_cerver->web_sockets->mutex);
	}

}

void http_cerver_set_web_socket_broadcast_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int broadcast_timeout
) {

	if (http_cerver && http_cerver->web_sockets) {
		(void) pthread_mutex_lock (&http_cerver->web_sockets->mutex);
		http_cerver->web_sockets->broadcast_timeout = broadcast_timeout ?
			broadcast_timeout : HTTP_WEB_SOCKET_DEFAULT_BROADCAST_TIMEOUT;
		(void) pthread_mutex_unlock (&http_cerver->web_sockets->mutex);
	}

}

// returns the index of the room in the connection, -1 when it is not in it
// the mutex of the registry must be held
static int http_web_socket_room_find (const HttpWebSocket *web_socket, const char *room) {

	int found = -1;

	for (unsigned int i = 0; (found < 0) && (i < web_socket->n_rooms); i++) {
		if (!strcmp (web_socket->rooms[i], room)) found = (int) i;
	}

	return found;

}

// the connection is in the room, or room is NULL
static bool http_web_socket_room_matches (const HttpWebSocket *web_socket, const char *room) {

	return !room || (http_web_socket_room_find (web_socket, room) >= 0);

}

unsigned int http_cerver_web_socket_count (
	HttpCerver *http_cerver, const char *room
) {

	unsigned int count = 0;

	if (http_cerver && http_cerver->web_sockets) {
		HttpWebSocketRegistry *registry = http_cerver->web_sockets;
		(void) pthread_mutex_lock (&registry->mutex);

		if (!room) {
			count = registry->count;
		}

		else {
			for (HttpWebSocket *web_socket = registry->first; web_socket; web_socket = web_socket->next) {
				if (http_web_socket_room_matches (web_socket, room)) count += 1;
			}
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return count;

}

// the counters of the registry are read one by one, like its metrics
static void http_web_socket_registry_stats (
	HttpWebSocketRegistry *registry, HttpWebSocketStats *stats
) {

	(void) pthread_mutex_lock (&registry->mutex);
	stats->n_connections = registry->count;
	(void) pthread_mutex_unlock (&registry->mutex);

	stats->n_opened = CERVER_STATS_GET (registry->n_opened);
	stats->n_closed = CERVER_STATS_GET (registry->n_closed);

	for (unsigned int i = 0; i < HTTP_WEB_SOCKET_REJECTIONS; i++) {
		stats->n_rejected[i] = CERVER_STATS_GET (registry->n_rejected[i]);
	}

	stats->n_messages_received = CERVER_STATS_GET (registry->n_messages_received);
	stats->n_messages_sent = CERVER_STATS_GET (registry->n_messages_sent);
	stats->n_bytes_received = CERVER_STATS_GET (registry->n_bytes_received);
	stats->n_bytes_sent = CERVER_STATS_GET (registry->n_bytes_sent);

	stats->n_slow = CERVER_STATS_GET (registry->n_slow);
	stats->n_errors = CERVER_STATS_GET (registry->n_errors);

	for (unsigned int i = 0; i < HTTP_WEB_SOCKET_CLOSE_CODES_SIZE; i++) {
		stats->n_closes[i] = CERVER_STATS_GET (registry->n_closes[i]);
	}

	stats->n_closes_other = CERVER_STATS_GET (registry->n_closes_other);

}

u8 http_cerver_web_socket_get_stats (
	const HttpCerver *http_cerver, HttpWebSocketStats *stats
) {

	u8 retval = 1;

	if (http_cerver && http_cerver->web_sockets && stats) {
		(void) memset (stats, 0, sizeof (HttpWebSocketStats));

		http_web_socket_registry_stats (http_cerver->web_sockets, stats);

		retval = 0;
	}

	return retval;

}

u8 http_web_socket_get_stats (
	const HttpWebSocket *web_socket, HttpWebSocketStats *stats
) {

	u8 retval = 1;

	if (web_socket && stats) {
		(void) memset (stats, 0, sizeof (HttpWebSocketStats));

		stats->n_messages_received = __atomic_load_n (&web_socket->n_messages_received, __ATOMIC_SEQ_CST);
		stats->n_messages_sent = __atomic_load_n (&web_socket->n_messages_sent, __ATOMIC_SEQ_CST);
		stats->n_bytes_received = __atomic_load_n (&web_socket->n_bytes_received, __ATOMIC_SEQ_CST);
		stats->n_bytes_sent = __atomic_load_n (&web_socket->n_bytes_sent, __ATOMIC_SEQ_CST);

		retval = 0;
	}

	return retval;

}

static HttpWebSocketRegistry *http_web_socket_registry_of (const HttpWebSocket *web_socket) {

	return (web_socket && web_socket->http_cerver) ? web_socket->http_cerver->web_sockets : NULL;

}

u8 http_web_socket_join (HttpWebSocket *web_socket, const char *room) {

	u8 retval = 1;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry && room && (strlen (room) < HTTP_WEB_SOCKET_ROOM_NAME_SIZE)) {
		(void) pthread_mutex_lock (&registry->mutex);

		if (!web_socket->registered) {
			retval = 1;
		}

		else if (http_web_socket_room_find (web_socket, room) >= 0) {
			retval = 0;
		}

		else if (web_socket->n_rooms < HTTP_WEB_SOCKET_MAX_ROOMS) {
			(void) strcpy (web_socket->rooms[web_socket->n_rooms], room);
			web_socket->n_rooms += 1;
			retval = 0;
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return retval;

}

u8 http_web_socket_leave (HttpWebSocket *web_socket, const char *room) {

	u8 retval = 1;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry && room) {
		(void) pthread_mutex_lock (&registry->mutex);

		const int found = http_web_socket_room_find (web_socket, room);
		if (found >= 0) {
			// the last room takes its place
			web_socket->n_rooms -= 1;
			if ((unsigned int) found != web_socket->n_rooms) {
				(void) memcpy (
					web_socket->rooms[found], web_socket->rooms[web_socket->n_rooms],
					HTTP_WEB_SOCKET_ROOM_NAME_SIZE
				);
			}

			retval = 0;
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return retval;

}

bool http_web_socket_in_room (HttpWebSocket *web_socket, const char *room) {

	bool in_room = false;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry && room) {
		(void) pthread_mutex_lock (&registry->mutex);
		in_room = http_web_socket_room_find (web_socket, room) >= 0;
		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return in_room;

}

// the connections that match, each one with a reference,
// the caller releases them and frees the array
static HttpWebSocket **http_web_socket_registry_snapshot (
	HttpWebSocketRegistry *registry, const char *room,
	const HttpEndpoint *endpoint, const HttpWebSocket *except,
	unsigned int *n_connections, unsigned int *timeout
) {

	HttpWebSocket **connections = NULL;
	*n_connections = 0;

	(void) pthread_mutex_lock (&registry->mutex);

	*timeout = registry->broadcast_timeout;

	if (registry->count) {
		connections = (HttpWebSocket **) malloc (registry->count * sizeof (HttpWebSocket *));
	}

	if (connections) {
		for (HttpWebSocket *web_socket = registry->first; web_socket; web_socket = web_socket->next) {
			if (
				(web_socket != except)
				&& (!endpoint || (web_socket->endpoint == endpoint))
				&& http_web_socket_room_matches (web_socket, room)
			) {
				connections[*n_connections] = http_web_socket_retain (web_socket);
				*n_connections += 1;
			}
		}
	}

	(void) pthread_mutex_unlock (&registry->mutex);

	return connections;

}

// sends the message to every connection of the snapshot, and releases them
static unsigned int http_web_socket_registry_send (
	HttpWebSocket **connections, const unsigned int n_connections,
	const HttpWebSocketOpcode type, const void *data, const size_t size,
	const unsigned int timeout
) {

	unsigned int sent = 0;

	for (unsigned int i = 0; i < n_connections; i++) {
		if (!http_web_socket_send_timeout (connections[i], type, data, size, timeout)) sent += 1;

		http_web_socket_release (connections[i]);
	}

	free (connections);

	return sent;

}

static unsigned int http_web_socket_registry_broadcast (
	HttpCerver *http_cerver, const char *room,
	const HttpEndpoint *endpoint, const HttpWebSocket *except,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	unsigned int sent = 0;

	if (http_cerver && http_cerver->web_sockets && (data || !size)) {
		unsigned int n_connections = 0;
		unsigned int timeout = 0;
		HttpWebSocket **connections = http_web_socket_registry_snapshot (
			http_cerver->web_sockets, room, endpoint, except, &n_connections, &timeout
		);

		sent = http_web_socket_registry_send (connections, n_connections, type, data, size, timeout);
	}

	return sent;

}

unsigned int http_cerver_web_socket_broadcast (
	HttpCerver *http_cerver, const char *room, const HttpWebSocket *except,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	return http_web_socket_registry_broadcast (http_cerver, room, NULL, except, type, data, size);

}

unsigned int http_endpoint_web_socket_broadcast (
	HttpCerver *http_cerver, const HttpEndpoint *endpoint,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	return endpoint ?
		http_web_socket_registry_broadcast (http_cerver, NULL, endpoint, NULL, type, data, size) : 0;

}

void http_cerver_web_socket_for_each (
	HttpCerver *http_cerver, const char *room,
	void (*method)(HttpWebSocket *web_socket, void *args), void *args
) {

	if (http_cerver && http_cerver->web_sockets && method) {
		unsigned int n_connections = 0;
		unsigned int timeout = 0;
		HttpWebSocket **connections = http_web_socket_registry_snapshot (
			http_cerver->web_sockets, room, NULL, NULL, &n_connections, &timeout
		);

		for (unsigned int i = 0; i < n_connections; i++) {
			method (connections[i], args);
			http_web_socket_release (connections[i]);
		}

		free (connections);
	}

}

#pragma endregion
