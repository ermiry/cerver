#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "cerver/types/types.h"

#include "cerver/collections/htab.h"

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

static const char http_web_socket_join_undefined[] = "Undefined";

const char *http_web_socket_join_to_string (const HttpWebSocketJoin join) {

	switch (join) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_JOIN_##name: return #string;
		HTTP_WEB_SOCKET_JOIN_MAP(XX)
		#undef XX
	}

	return http_web_socket_join_undefined;

}

const char *http_web_socket_join_description (const HttpWebSocketJoin join) {

	switch (join) {
		#define XX(num, name, string, description) case HTTP_WEB_SOCKET_JOIN_##name: return #description;
		HTTP_WEB_SOCKET_JOIN_MAP(XX)
		#undef XX
	}

	return http_web_socket_join_undefined;

}

// a room of the index, it has no members when the registry is released
static void http_web_socket_room_delete (void *room_ptr) {

	HttpWebSocketRoom *room = (HttpWebSocketRoom *) room_ptr;
	if (room) {
		free (room->name);
		free (room);
	}

}

HttpWebSocketRegistry *http_web_socket_registry_create (void) {

	HttpWebSocketRegistry *registry = (HttpWebSocketRegistry *) calloc (1, sizeof (HttpWebSocketRegistry));
	if (registry) {
		registry->rooms = htab_create (0, NULL, http_web_socket_room_delete);
		if (!registry->rooms) {
			free (registry);
			registry = NULL;
		}
	}

	if (registry) {
		(void) pthread_mutex_init (&registry->mutex, NULL);

		registry->first = NULL;
		registry->count = 0;
		registry->reserved = 0;
		registry->max_connections = HTTP_WEB_SOCKET_DEFAULT_MAX_CONNECTIONS;
		registry->max_queue = HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE;
		registry->overflow = HTTP_WEB_SOCKET_OVERFLOW_DROP;
		registry->stopping = false;
	}

	return registry;

}

void http_web_socket_registry_delete (HttpWebSocketRegistry *registry) {

	if (registry) {
		htab_destroy (registry->rooms);
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
			// its rooms are kept for its on_close (), the broadcasts skip it
			web_socket->registered = false;

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

void http_cerver_set_web_socket_queue (
	HttpCerver *http_cerver, const size_t max_queue, const HttpWebSocketOverflow overflow
) {

	if (http_cerver && http_cerver->web_sockets) {
		(void) pthread_mutex_lock (&http_cerver->web_sockets->mutex);
		http_cerver->web_sockets->max_queue = max_queue ? max_queue : HTTP_WEB_SOCKET_DEFAULT_MAX_QUEUE;
		http_cerver->web_sockets->overflow = overflow;
		(void) pthread_mutex_unlock (&http_cerver->web_sockets->mutex);
	}

}

#pragma endregion

#pragma region rooms

// the rooms are an index of the registry: a room has the list of its members, and a connection the list of its
// rooms; both lists, and the index, are protected by the mutex of the registry

// the membership of the connection in the room, NULL when it is not in it
static HttpWebSocketMembership *http_web_socket_membership_find (
	const HttpWebSocket *web_socket, const char *room
) {

	HttpWebSocketMembership *found = NULL;

	for (HttpWebSocketMembership *membership = web_socket->rooms; !found && membership; membership = membership->next_room) {
		if (!strcmp (membership->room->name, room)) found = membership;
	}

	return found;

}

static HttpWebSocketRoom *http_web_socket_room_get (
	const HttpWebSocketRegistry *registry, const char *name
) {

	return (HttpWebSocketRoom *) htab_get (registry->rooms, name, strlen (name));

}

// a room without members, in the index
static HttpWebSocketRoom *http_web_socket_room_create (
	HttpWebSocketRegistry *registry, const char *name, const size_t length
) {

	HttpWebSocketRoom *room = (HttpWebSocketRoom *) calloc (1, sizeof (HttpWebSocketRoom));
	if (room) {
		room->name = (char *) malloc (length + 1);
		if (room->name) (void) memcpy (room->name, name, length + 1);
		room->length = length;
	}

	if (room && (!room->name || htab_insert (registry->rooms, name, length, room, sizeof (HttpWebSocketRoom)))) {
		http_web_socket_room_delete (room);
		room = NULL;
	}

	return room;

}

// a room is removed with its last member
static void http_web_socket_room_remove (HttpWebSocketRegistry *registry, HttpWebSocketRoom *room) {

	(void) htab_remove (registry->rooms, room->name, room->length);
	http_web_socket_room_delete (room);

}

// the connection is a member of the room
static void http_web_socket_membership_link (
	HttpWebSocketMembership *membership, HttpWebSocketRoom *room, HttpWebSocket *web_socket
) {

	membership->room = room;
	membership->web_socket = web_socket;

	membership->previous = NULL;
	membership->next = room->first;
	if (room->first) room->first->previous = membership;
	room->first = membership;
	room->count += 1;

	membership->next_room = web_socket->rooms;
	web_socket->rooms = membership;
	web_socket->n_rooms += 1;

}

// the membership leaves the list of its room, that is removed without members; the list of the connection is
// changed by the caller
static void http_web_socket_membership_unlink (
	HttpWebSocketRegistry *registry, HttpWebSocketMembership *membership
) {

	HttpWebSocketRoom *room = membership->room;

	if (membership->previous) membership->previous->next = membership->next;
	else room->first = membership->next;

	if (membership->next) membership->next->previous = membership->previous;

	room->count -= 1;
	if (!room->count) http_web_socket_room_remove (registry, room);

	free (membership);

}

// takes the membership of the room out of the list of the connection, NULL when it was not in it
static HttpWebSocketMembership *http_web_socket_membership_take (
	HttpWebSocket *web_socket, const char *room
) {

	HttpWebSocketMembership **link = &web_socket->rooms;
	while (*link && strcmp ((*link)->room->name, room)) link = &(*link)->next_room;

	HttpWebSocketMembership *membership = *link;
	if (membership) {
		*link = membership->next_room;
		web_socket->n_rooms -= 1;
	}

	return membership;

}

static HttpWebSocketJoin http_web_socket_room_add (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket, const char *name
) {

	HttpWebSocketJoin result = HTTP_WEB_SOCKET_JOIN_FAILED;

	HttpWebSocketRoom *room = http_web_socket_room_get (registry, name);
	if (!room) room = http_web_socket_room_create (registry, name, strlen (name));

	HttpWebSocketMembership *membership = room ?
		(HttpWebSocketMembership *) calloc (1, sizeof (HttpWebSocketMembership)) : NULL;

	if (membership) {
		http_web_socket_membership_link (membership, room, web_socket);
		result = HTTP_WEB_SOCKET_JOIN_JOINED;
	}

	// a room that was created for it is not left without members
	else if (room && !room->count) {
		http_web_socket_room_remove (registry, room);
	}

	return result;

}

static HttpWebSocketJoin http_web_socket_room_join (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket, const char *name
) {

	HttpWebSocketJoin result = HTTP_WEB_SOCKET_JOIN_UNREGISTERED;

	// a connection that has its place joins in its on_open (), before it is registered: a broadcast to its rooms
	// reaches it once it is (S7)
	if (web_socket->registered || web_socket->reserved) {
		result = http_web_socket_membership_find (web_socket, name) ?
			HTTP_WEB_SOCKET_JOIN_ALREADY : http_web_socket_room_add (registry, web_socket, name);
	}

	return result;

}

static unsigned int http_web_socket_rooms_leave (
	HttpWebSocketRegistry *registry, HttpWebSocket *web_socket
) {

	unsigned int left = 0;

	while (web_socket->rooms) {
		HttpWebSocketMembership *membership = web_socket->rooms;
		web_socket->rooms = membership->next_room;

		http_web_socket_membership_unlink (registry, membership);
		left += 1;
	}

	web_socket->n_rooms = 0;

	return left;

}

// the bytes of the copy of the names of the rooms: the array and every name with its terminator
static size_t http_web_socket_rooms_size (const HttpWebSocket *web_socket) {

	size_t size = web_socket->n_rooms * sizeof (char *);

	for (const HttpWebSocketMembership *membership = web_socket->rooms; membership; membership = membership->next_room) {
		size += membership->room->length + 1;
	}

	return size;

}

static char **http_web_socket_rooms_copy (const HttpWebSocket *web_socket, unsigned int *n_rooms) {

	char **rooms = web_socket->n_rooms ? (char **) malloc (http_web_socket_rooms_size (web_socket)) : NULL;

	if (rooms) {
		char *names = (char *) (rooms + web_socket->n_rooms);

		unsigned int idx = 0;
		for (const HttpWebSocketMembership *membership = web_socket->rooms; membership; membership = membership->next_room) {
			(void) memcpy (names, membership->room->name, membership->room->length + 1);
			rooms[idx] = names;
			names += membership->room->length + 1;
			idx += 1;
		}

		*n_rooms = idx;
	}

	return rooms;

}

#pragma endregion

#pragma region targets

static const HttpWebSocketTarget http_web_socket_target_every = { NULL, NULL, NULL };

// the connection is registered and it matches the endpoint and the exception of the target, its room is matched by
// the list that is visited
static bool http_web_socket_target_matches (
	const HttpWebSocket *web_socket, const HttpWebSocketTarget *target
) {

	return web_socket->registered
		&& (web_socket != target->except)
		&& (!target->endpoint || (web_socket->endpoint == target->endpoint));

}

// the most connections that the target can reach, the mutex of the registry must be held
static unsigned int http_web_socket_target_bound (
	const HttpWebSocketRegistry *registry, const HttpWebSocketTarget *target
) {

	unsigned int bound = registry->count;

	if (target->room) {
		const HttpWebSocketRoom *room = http_web_socket_room_get (registry, target->room);
		bound = room ? room->count : 0;
	}

	return bound;

}

// calls the method with each connection of the target while the mutex of the registry is held:
// a room only goes through its members
static void http_web_socket_target_visit (
	const HttpWebSocketRegistry *registry, const HttpWebSocketTarget *target,
	void (*method)(HttpWebSocket *web_socket, void *args), void *args
) {

	if (target->room) {
		const HttpWebSocketRoom *room = http_web_socket_room_get (registry, target->room);
		for (const HttpWebSocketMembership *membership = room ? room->first : NULL; membership; membership = membership->next) {
			if (http_web_socket_target_matches (membership->web_socket, target)) method (membership->web_socket, args);
		}
	}

	else {
		for (HttpWebSocket *web_socket = registry->first; web_socket; web_socket = web_socket->next) {
			if (http_web_socket_target_matches (web_socket, target)) method (web_socket, args);
		}
	}

}

static void http_web_socket_target_count (HttpWebSocket *web_socket, void *args) {

	(void) web_socket;

	*((unsigned int *) args) += 1;

}

// a target without fields is every connection of the registry
static bool http_web_socket_target_is_every (const HttpWebSocketTarget *target) {

	return !target->room && !target->endpoint && !target->except;

}

unsigned int http_cerver_web_socket_count (
	HttpCerver *http_cerver, const HttpWebSocketTarget *target
) {

	unsigned int count = 0;

	if (http_cerver && http_cerver->web_sockets) {
		HttpWebSocketRegistry *registry = http_cerver->web_sockets;
		const HttpWebSocketTarget *reached = target ? target : &http_web_socket_target_every;

		(void) pthread_mutex_lock (&registry->mutex);

		if (http_web_socket_target_is_every (reached)) count = registry->count;
		else http_web_socket_target_visit (registry, reached, http_web_socket_target_count, &count);

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return count;

}

#pragma endregion

#pragma region stats

// the bytes of the queues of the connections, the mutex of the registry must be held
static size_t http_web_socket_registry_queued_locked (const HttpWebSocketRegistry *registry) {

	size_t queued = 0;

	for (const HttpWebSocket *web_socket = registry->first; web_socket; web_socket = web_socket->next) {
		queued += __atomic_load_n (&web_socket->queue_size, __ATOMIC_SEQ_CST);
	}

	return queued;

}

size_t http_web_socket_registry_queued (HttpWebSocketRegistry *registry) {

	(void) pthread_mutex_lock (&registry->mutex);
	const size_t queued = http_web_socket_registry_queued_locked (registry);
	(void) pthread_mutex_unlock (&registry->mutex);

	return queued;

}

// the counters of the registry are read one by one, like its metrics
static void http_web_socket_registry_stats (
	HttpWebSocketRegistry *registry, HttpWebSocketStats *stats
) {

	(void) pthread_mutex_lock (&registry->mutex);
	stats->n_connections = registry->count;
	stats->n_queued_bytes = http_web_socket_registry_queued_locked (registry);
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
	stats->n_dropped = CERVER_STATS_GET (registry->n_dropped);
	stats->n_drains = CERVER_STATS_GET (registry->n_drains);
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

HttpWebSocket *http_cerver_web_socket_get_with_id (
	HttpCerver *http_cerver, const u64 id
) {

	HttpWebSocket *found = NULL;

	if (http_cerver && http_cerver->web_sockets && id) {
		HttpWebSocketRegistry *registry = http_cerver->web_sockets;
		(void) pthread_mutex_lock (&registry->mutex);

		for (HttpWebSocket *web_socket = registry->first; !found && web_socket; web_socket = web_socket->next) {
			if (web_socket->id == id) found = http_web_socket_retain (web_socket);
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return found;

}

#pragma endregion

#pragma region public rooms

static HttpWebSocketRegistry *http_web_socket_registry_of (const HttpWebSocket *web_socket) {

	return (web_socket && web_socket->http_cerver) ? web_socket->http_cerver->web_sockets : NULL;

}

HttpWebSocketJoin http_web_socket_join (HttpWebSocket *web_socket, const char *room) {

	HttpWebSocketJoin result = HTTP_WEB_SOCKET_JOIN_INVALID;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (web_socket && room && room[0]) {
		result = HTTP_WEB_SOCKET_JOIN_UNREGISTERED;

		if (registry) {
			(void) pthread_mutex_lock (&registry->mutex);
			result = http_web_socket_room_join (registry, web_socket, room);
			(void) pthread_mutex_unlock (&registry->mutex);
		}
	}

	return result;

}

u8 http_web_socket_leave (HttpWebSocket *web_socket, const char *room) {

	u8 retval = 1;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry && room) {
		(void) pthread_mutex_lock (&registry->mutex);

		HttpWebSocketMembership *membership = http_web_socket_membership_take (web_socket, room);
		if (membership) {
			http_web_socket_membership_unlink (registry, membership);
			retval = 0;
		}

		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return retval;

}

unsigned int http_web_socket_leave_all (HttpWebSocket *web_socket) {

	unsigned int left = 0;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry) {
		(void) pthread_mutex_lock (&registry->mutex);
		left = http_web_socket_rooms_leave (registry, web_socket);
		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return left;

}

void http_web_socket_rooms_clear (HttpWebSocket *web_socket) {

	(void) http_web_socket_leave_all (web_socket);

}

bool http_web_socket_in_room (const HttpWebSocket *web_socket, const char *room) {

	bool in_room = false;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry && room) {
		(void) pthread_mutex_lock (&registry->mutex);
		in_room = (http_web_socket_membership_find (web_socket, room) != NULL);
		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return in_room;

}

char **http_web_socket_get_rooms (const HttpWebSocket *web_socket, unsigned int *n_rooms) {

	char **rooms = NULL;

	if (n_rooms) *n_rooms = 0;

	HttpWebSocketRegistry *registry = http_web_socket_registry_of (web_socket);
	if (registry && n_rooms) {
		(void) pthread_mutex_lock (&registry->mutex);
		rooms = http_web_socket_rooms_copy (web_socket, n_rooms);
		(void) pthread_mutex_unlock (&registry->mutex);
	}

	return rooms;

}

#pragma endregion

#pragma region broadcast

// the connections of a snapshot, each one with a reference
typedef struct HttpWebSocketSnapshot {

	HttpWebSocket **connections;
	unsigned int n_connections;

} HttpWebSocketSnapshot;

static void http_web_socket_snapshot_add (HttpWebSocket *web_socket, void *args) {

	HttpWebSocketSnapshot *snapshot = (HttpWebSocketSnapshot *) args;

	snapshot->connections[snapshot->n_connections] = http_web_socket_retain (web_socket);
	snapshot->n_connections += 1;

}

// the connections of the target, each one with a reference,
// the caller releases them and frees the array
static HttpWebSocket **http_web_socket_registry_snapshot (
	HttpWebSocketRegistry *registry, const HttpWebSocketTarget *target,
	unsigned int *n_connections
) {

	HttpWebSocketSnapshot snapshot = { NULL, 0 };

	(void) pthread_mutex_lock (&registry->mutex);

	const unsigned int bound = http_web_socket_target_bound (registry, target);
	if (bound) snapshot.connections = (HttpWebSocket **) malloc (bound * sizeof (HttpWebSocket *));

	if (snapshot.connections) {
		http_web_socket_target_visit (registry, target, http_web_socket_snapshot_add, &snapshot);
	}

	(void) pthread_mutex_unlock (&registry->mutex);

	*n_connections = snapshot.n_connections;

	return snapshot.connections;

}

// sends the message to every connection of the snapshot, and releases them:
// each one takes it in its queue with its own policy, a slow one never waits for the others
static unsigned int http_web_socket_registry_send (
	HttpWebSocket **connections, const unsigned int n_connections,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	unsigned int sent = 0;

	for (unsigned int i = 0; i < n_connections; i++) {
		if (http_web_socket_send_result_is_taken (http_web_socket_send (connections[i], type, data, size))) sent += 1;

		http_web_socket_release (connections[i]);
	}

	free (connections);

	return sent;

}

unsigned int http_cerver_web_socket_broadcast (
	HttpCerver *http_cerver, const HttpWebSocketTarget *target,
	const HttpWebSocketOpcode type, const void *data, const size_t size
) {

	unsigned int sent = 0;

	if (http_cerver && http_cerver->web_sockets && (data || !size)) {
		unsigned int n_connections = 0;
		HttpWebSocket **connections = http_web_socket_registry_snapshot (
			http_cerver->web_sockets, target ? target : &http_web_socket_target_every, &n_connections
		);

		sent = http_web_socket_registry_send (connections, n_connections, type, data, size);
	}

	return sent;

}

void http_cerver_web_socket_for_each (
	HttpCerver *http_cerver, const HttpWebSocketTarget *target,
	void (*method)(HttpWebSocket *web_socket, void *args), void *args
) {

	if (http_cerver && http_cerver->web_sockets && method) {
		unsigned int n_connections = 0;
		HttpWebSocket **connections = http_web_socket_registry_snapshot (
			http_cerver->web_sockets, target ? target : &http_web_socket_target_every, &n_connections
		);

		for (unsigned int i = 0; i < n_connections; i++) {
			method (connections[i], args);
			http_web_socket_release (connections[i]);
		}

		free (connections);
	}

}

#pragma endregion
