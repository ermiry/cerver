#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/connection/registry.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/events.h"
#include "cerver/handler/handler.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"
#include "cerver/packets/serializer.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"
#include "cerver/game/player.h"
#include "cerver/game/types.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/sha256.h"
#include "cerver/utils/utils.h"

#define LOBBY_ID_SOURCE_SIZE				64
#define LOBBY_ID_HASH_SIZE					32
#define LOBBY_ID_HASH_STRING_SIZE			65

#define LOBBY_WAKE_READ						0
#define LOBBY_WAKE_WRITE					1
#define LOBBY_WAKE_BYTE						1
#define LOBBY_WAKE_DRAIN_SIZE				64

#define LOBBY_NANOSECONDS					1000000000L
#define LOBBY_MILLISECONDS_PER_SECOND		1000

// the reference of the creator of the lobby, that lobby_teardown () releases
#define LOBBY_CREATOR_REFERENCES			1

SERIALIZER_DEFINE (LobbyCreate, lobby_create_request, LOBBY_OBJECTS_VERSION, LOBBY_CREATE_FIELDS)
SERIALIZER_DEFINE (LobbyJoin, lobby_join_request, LOBBY_OBJECTS_VERSION, LOBBY_JOIN_FIELDS)
SERIALIZER_DEFINE (LobbyRequest, lobby_request, LOBBY_OBJECTS_VERSION, LOBBY_REQUEST_FIELDS)
SERIALIZER_DEFINE (LobbyPlayerSummary, lobby_player_summary, LOBBY_OBJECTS_VERSION, LOBBY_PLAYER_SUMMARY_FIELDS)
SERIALIZER_DEFINE (LobbySettingsSummary, lobby_settings_summary, LOBBY_OBJECTS_VERSION, LOBBY_SETTINGS_SUMMARY_FIELDS)
SERIALIZER_DEFINE (LobbySummary, lobby_summary, LOBBY_OBJECTS_VERSION, LOBBY_SUMMARY_FIELDS)

static unsigned long lobby_id_counter = 0;

void *lobby_default_id_generator (const void *data_ptr) {

	// the default id does not depend on the data
	(void) data_ptr;

	// the counter tells apart the lobbies of the same second
	char source[LOBBY_ID_SOURCE_SIZE] = { 0 };
	const int length = snprintf (
		source, LOBBY_ID_SOURCE_SIZE, "%ld-%lu",
		(long) time (NULL),
		__atomic_add_fetch (&lobby_id_counter, 1, __ATOMIC_SEQ_CST)
	);

	uint8_t hash[LOBBY_ID_HASH_SIZE];
	char hash_string[LOBBY_ID_HASH_STRING_SIZE];

	sha256_calc (hash, source, (size_t) length);
	sha256_hash_to_string (hash_string, hash);

	return str_new (hash_string);

}

static CerverLobby *cerver_lobby_new (Cerver *cerver, Lobby *lobby) {

	CerverLobby *cerver_lobby = (CerverLobby *) malloc (sizeof (CerverLobby));
	if (cerver_lobby) {
		cerver_lobby->cerver = cerver;
		cerver_lobby->lobby = lobby;
	}

	return cerver_lobby;

}

static void cerver_lobby_delete (CerverLobby *cerver_lobby) {

	if (cerver_lobby) {
		cerver_lobby->cerver = NULL;
		cerver_lobby->lobby = NULL;
		free (cerver_lobby);
	}

}

/*** Lobby ***/

#pragma region lobby

static LobbyStats *lobby_stats_new (void) {

	LobbyStats *stats = (LobbyStats *) malloc (sizeof (LobbyStats));
	if (stats) {
		(void) memset (stats, 0, sizeof (LobbyStats));
		stats->received_packets = packets_per_type_new ();
		stats->sent_packets = packets_per_type_new ();
	}

	return stats;

}

static inline void lobby_stats_delete (LobbyStats *stats) {

	if (stats) {
		packets_per_type_delete (stats->received_packets);
		packets_per_type_delete (stats->sent_packets);
		free (stats);
	}

}

// sets the lobby stats threshold time (how often the stats get reset)
void lobby_stats_set_threshold_time (Lobby *lobby, time_t threshold_time) {

	if (lobby) {
		if (lobby->stats) lobby->stats->threshold_time = threshold_time;
	}

}

void lobby_stats_print (Lobby *lobby) {

	if (lobby) {
		if (lobby->stats) {
			(void) printf ("\nLobby's %s stats: ", lobby->id->str);
			(void) printf ("\nThreshold time:            %ld\n", (long) lobby->stats->threshold_time);

			(void) printf ("Total packets received:        %lu\n", lobby->stats->n_packets_received);
			(void) printf ("Total receives done:           %lu\n", lobby->stats->n_receives_done);
			(void) printf ("Total bytes received:          %lu\n\n", lobby->stats->bytes_received);

			(void) printf ("N packets sent:                %lu\n", lobby->stats->n_packets_sent);
			(void) printf ("Total bytes sent:              %lu\n", lobby->stats->bytes_sent);

			(void) printf ("\nReceived packets:\n");
			packets_per_type_print (lobby->stats->received_packets);

			(void) printf ("\nSent packets:\n");
			packets_per_type_print (lobby->stats->sent_packets);
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_GAME,
				"Lobby %s does not have a reference to lobby stats!",
				lobby->id->str
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_CERVER,
			"Cant print stats of a NULL lobby!"
		);
	}

}

// lobby constructor
Lobby *lobby_new (void) {

	Lobby *lobby = (Lobby *) malloc (sizeof (Lobby));
	if (lobby) {
		(void) memset (lobby, 0, sizeof (Lobby));

		lobby->cerver = NULL;

		lobby->id = NULL;

		lobby->references = LOBBY_CREATOR_REFERENCES;

		lobby->players_fds = NULL;
		lobby->poll_timeout = LOBBY_DEFAULT_POLL_TIMEOUT;
		lobby->poll_connections = false;
		lobby->wake_fds[LOBBY_WAKE_READ] = -1;
		lobby->wake_fds[LOBBY_WAKE_WRITE] = -1;

		lobby->running = lobby->in_game = false;
		lobby->initialized = false;
		lobby->ended = false;

		lobby->players = NULL;
		lobby->owner = NULL;
		lobby->max_players = LOBBY_DEFAULT_MAX_PLAYERS;

		lobby->handler_started = false;
		lobby->default_handler = true;
		lobby->handler = lobby_poll;
		lobby->packet_handler = NULL;

		lobby->game_type = NULL;

		lobby->game_settings = NULL;
		lobby->game_settings_delete = NULL;

		lobby->game_data = NULL;
		lobby->game_data_delete = NULL;

		lobby->update_started = false;
		lobby->update = NULL;

		(void) pthread_mutex_init (&lobby->stop_mutex, NULL);
		(void) pthread_cond_init (&lobby->stop_cond, NULL);

		lobby->stats = NULL;
	}

	return lobby;

}

static void lobby_wake_close (Lobby *lobby) {

	for (unsigned int i = 0; i < 2; i++) {
		if (lobby->wake_fds[i] > -1) {
			(void) close (lobby->wake_fds[i]);
			lobby->wake_fds[i] = -1;
		}
	}

}

void lobby_delete (void *lobby_ptr) {

	if (lobby_ptr) {
		Lobby *lobby = (Lobby *) lobby_ptr;

		str_delete (lobby->id);

		dlist_delete (lobby->players);

		if (lobby->players_fds) free (lobby->players_fds);
		lobby_wake_close (lobby);

		lobby->owner = NULL;

		if (lobby->game_settings) {
			if (lobby->game_settings_delete)
				lobby->game_settings_delete (lobby->game_settings);
			else free (lobby->game_settings);
		}

		if (lobby->game_data) {
			if (lobby->game_data_delete)
				lobby->game_data_delete (lobby->game_data);
			else free (lobby->game_data);
		}

		lobby_stats_delete (lobby->stats);

		(void) pthread_mutex_destroy (&lobby->stop_mutex);
		(void) pthread_cond_destroy (&lobby->stop_cond);

		free (lobby);
	}

}

// takes a reference to the lobby, returns the lobby (NULL is safe)
Lobby *lobby_reference (Lobby *lobby) {

	if (lobby) (void) __atomic_add_fetch (&lobby->references, 1, __ATOMIC_SEQ_CST);

	return lobby;

}

// releases a reference to the lobby, the last one deletes it (NULL is safe)
void lobby_release (Lobby *lobby) {

	if (lobby) {
		if (__atomic_sub_fetch (&lobby->references, 1, __ATOMIC_SEQ_CST) == 0) {
			lobby_delete (lobby);
		}
	}

}

// initializes a new lobby
static u8 lobby_init (GameCerver *game_cerver, Lobby *lobby, const u32 max_players) {

	u8 retval = 1;

	lobby->creation_time_stamp = time (NULL);
	lobby->max_players = max_players ? max_players : LOBBY_DEFAULT_MAX_PLAYERS;

	void *id = game_cerver->lobby_id_generator (lobby);
	if (id) {
		lobby->id = (String *) id;
		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_GAME,
			"Lobby id: %s", lobby->id->str
		);
		#endif

		lobby->players = dlist_init (player_delete, player_comparator_client_id);

		lobby->stats = lobby_stats_new ();

		if (lobby->players && lobby->stats) retval = 0;
	}

	return retval;

}

//compares two lobbys based on their ids
int lobby_comparator (const void *one, const void *two) {

	if (one && two) return str_compare (((Lobby *) one)->id, ((Lobby *) two)->id);
	else if (!one && two) return -1;
	else if (one && ! two) return 1;
	return 0;

}

// set lobby poll function timeout in mili secs
// how often we are checking for new packages
void lobby_set_poll_time_out (Lobby *lobby, unsigned int timeout) {

	if (lobby) lobby->poll_timeout = timeout;

}

// set the lobby hanlder
void lobby_set_handler (Lobby *lobby, Action handler) { if (lobby) lobby->handler = handler; }

// set the lobby packet handler
void lobby_set_packet_handler (Lobby *lobby, Action packet_handler) {

	if (lobby) lobby->packet_handler = packet_handler;

}

// sets the lobby settings and a function to delete it
void lobby_set_game_settings (Lobby *lobby, void *game_settings, Action game_settings_delete) {

	if (lobby) {
		lobby->game_settings = game_settings;
		lobby->game_settings_delete = game_settings_delete;
	}

}

// sets the lobby game data and a function to delete it
void lobby_set_game_data (Lobby *lobby, void *game_data, Action game_data_delete) {

	if (lobby) {
		lobby->game_data = game_data;
		lobby->game_data_delete = game_data_delete;
	}

}

// sets the lobby update action, the lobby will we passed as the args
void lobby_set_update (Lobby *lobby, Action update) { if (lobby) lobby->update = update; }

// returns true while the lobby runs
bool lobby_is_running (const Lobby *lobby) {

	return lobby ? __atomic_load_n (&lobby->running, __ATOMIC_SEQ_CST) : false;

}

// searches a lobby in the game cerver and returns a reference to it
Lobby *lobby_get (GameCerver *game_cerver, Lobby *query) {

	Lobby *found = NULL;

	if (game_cerver && query) {
		(void) pthread_mutex_lock (&game_cerver->lock);

		for (ListElement *le = dlist_start (game_cerver->current_lobbys); le && !found; le = le->next) {
			if (!lobby_comparator (le->data, query)) found = (Lobby *) le->data;
		}

		(void) pthread_mutex_unlock (&game_cerver->lock);
	}

	return found;

}

// seraches for a lobby with matching id
Lobby *lobby_search_with_id (Cerver *cerver, const char *id) {

	Lobby *found = NULL;

	if (cerver && id && (cerver->type == CERVER_TYPE_GAME) && cerver->cerver_data) {
		GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

		game_cerver_lock (cerver);

		for (ListElement *le = dlist_start (game_cerver->current_lobbys); le && !found; le = le->next) {
			Lobby *lobby = (Lobby *) le->data;
			if (!strcmp (lobby->id->str, id)) found = lobby;
		}

		game_cerver_unlock (cerver);
	}

	return found;

}

// the connections of the player in the lobby poll go back to the cerver poll
static void lobby_player_connections_return (Lobby *lobby, Player *player) {

	for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
		Connection *connection = (Connection *) le->data;

		(void) lobby_poll_unregister_connection (lobby, player, connection);
		(void) connection_register_to_cerver_poll (lobby->cerver, connection);
	}

}

// the connections of the player go from the cerver poll to the lobby poll
static void lobby_player_connections_take (Lobby *lobby, Player *player) {

	for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
		Connection *connection = (Connection *) le->data;

		(void) connection_unregister_from_cerver_poll (lobby->cerver, connection);
		(void) lobby_poll_register_connection (lobby, player, connection);
	}

}

static void lobby_players_connections_clear (Lobby *lobby) {

	for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
		Player *player = (Player *) le->data;

		for (ListElement *le_sub = dlist_start (player->client->connections); le_sub; le_sub = le_sub->next) {
			Connection *connection = (Connection *) le_sub->data;
			if (connection->lobby == lobby) connection->lobby = NULL;
		}
	}

}

static void lobby_players_connections_return (Lobby *lobby) {

	if (lobby->poll_connections) {
		for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
			lobby_player_connections_return (lobby, (Player *) le->data);
		}

		lobby->poll_connections = false;
	}

}

// wakes up the lobby poll and the update thread, so they see that the lobby stopped
static void lobby_stop_signal (Lobby *lobby) {

	if (lobby->wake_fds[LOBBY_WAKE_WRITE] > -1) {
		const char byte = LOBBY_WAKE_BYTE;
		(void) !write (lobby->wake_fds[LOBBY_WAKE_WRITE], &byte, sizeof (char));
	}

	(void) pthread_mutex_lock (&lobby->stop_mutex);
	(void) pthread_cond_broadcast (&lobby->stop_cond);
	(void) pthread_mutex_unlock (&lobby->stop_mutex);

}

// a thread of the lobby can end the lobby, like the last player that leaves in the lobby poll,
// it can not join itself, so it is detached and releases its reference when it returns
static void lobby_thread_end (const bool started, const pthread_t thread) {

	if (started) {
		if (pthread_equal (pthread_self (), thread)) (void) pthread_detach (thread);
		else (void) pthread_join (thread, NULL);
	}

}

static void lobby_game_data_release (Lobby *lobby) {

	if (lobby->game_data) {
		if (lobby->game_data_delete) lobby->game_data_delete (lobby->game_data);
		else free (lobby->game_data);

		lobby->game_data = NULL;
	}

}

// stops the lobby, disconnects clinets (players) if any, destroys lobby game data,
// and releases the reference of its creator at the end
int lobby_teardown (GameCerver *game_cerver, Lobby *lobby) {

	int retval = 1;

	if (lobby) {
		Cerver *cerver = lobby->cerver;

		game_cerver_lock (cerver);

		const bool ended = lobby->ended;
		const bool in_game = lobby->in_game;
		const bool handler_started = lobby->handler_started;
		const bool update_started = lobby->update_started;

		if (!ended) {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_DEBUG, LOG_TYPE_GAME,
				"Starting lobby %s teardown...", lobby->id->str
			);
			#endif

			lobby->ended = true;
			__atomic_store_n (&lobby->running, false, __ATOMIC_SEQ_CST);
			lobby->in_game = false;

			// the packets of its players are not of the lobby anymore
			lobby_players_connections_clear (lobby);

			game_cerver_unregister_lobby (
				game_cerver ? game_cerver : (GameCerver *) cerver->cerver_data,
				lobby
			);
		}

		game_cerver_unlock (cerver);

		if (!ended) {
			lobby_stop_signal (lobby);

			lobby_thread_end (handler_started, lobby->handler_thread_id);
			lobby_thread_end (update_started, lobby->update_thread_id);

			game_cerver_lock (cerver);
			lobby_players_connections_return (lobby);
			game_cerver_unlock (cerver);

			// the end of the game type only runs for a game that started, a lobby can have no game type
			if (in_game && lobby->game_type && lobby->game_type->end) (void) lobby->game_type->end (lobby);

			cerver_event_trigger_lobby (CERVER_EVENT_LOBBY_DESTROY, cerver, NULL, NULL, lobby);

			lobby_game_data_release (lobby);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_GAME,
				"Lobby %s teardown was successfull!", lobby->id->str
			);
			#endif

			lobby_release (lobby);
		}

		retval = 0;
	}

	return retval;

}

#pragma endregion

/*** Lobby Poll ***/

#pragma region lobby poll

static void lobby_poll_fds_reset (struct pollfd *fds, const u32 first, const u32 last) {

	for (u32 i = first; i < last; i++) {
		fds[i].fd = -1;
		fds[i].events = 0;
		fds[i].revents = 0;
	}

}

// inits the lobby poll structures
// returns 0 on success, 1 on error
u8 lobby_poll_init (Lobby *lobby, unsigned int max_players_fds) {

	u8 retval = 1;

	if (lobby) {
		const u32 size = max_players_fds ? max_players_fds : LOBBY_DEFAULT_MAX_PLAYERS;

		struct pollfd *fds = (struct pollfd *) realloc (lobby->players_fds, size * sizeof (struct pollfd));
		if (fds) {
			lobby->players_fds = fds;
			lobby->max_players_fds = size;
			lobby->current_players_fds = 0;

			lobby_poll_fds_reset (lobby->players_fds, 0, lobby->max_players_fds);

			retval = 0;
		}
	}

	return retval;

}

// the poll grows by its initial size, the new slots start empty
// returns 0 on success, 1 on error
static u8 lobby_realloc_poll_fds (Lobby *lobby) {

	u8 retval = 1;

	const u32 step = lobby->max_players ? lobby->max_players : LOBBY_DEFAULT_MAX_PLAYERS;
	const u32 size = lobby->max_players_fds + step;

	struct pollfd *fds = (struct pollfd *) realloc (lobby->players_fds, size * sizeof (struct pollfd));
	if (fds) {
		lobby_poll_fds_reset (fds, lobby->max_players_fds, size);

		lobby->players_fds = fds;
		lobby->max_players_fds = size;

		retval = 0;
	}

	return retval;

}

// gets a free idx in the lobby poll structure
static i32 lobby_poll_get_free_idx (const Lobby *lobby) {

	for (u32 i = 0; i < lobby->max_players_fds; i++)
		if (lobby->players_fds[i].fd == -1) return (i32) i;

	return -1;

}

// gets the idx of the connection fd in the poll fds
static i32 lobby_poll_get_idx_with_sock_fd (const Lobby *lobby, const i32 sock_fd) {

	for (u32 i = 0; i < lobby->max_players_fds; i++)
		if (lobby->players_fds[i].fd == sock_fd) return (i32) i;

	return -1;

}

// registers a player's client connection to the lobby poll
u8 lobby_poll_register_connection (Lobby *lobby, Player *player, Connection *connection) {

	u8 retval = 1;

	if (lobby && player && connection) {
		if (!lobby->players_fds) (void) lobby_poll_init (lobby, lobby->max_players);

		i32 idx = lobby_poll_get_free_idx (lobby);
		if ((idx < 0) && !lobby_realloc_poll_fds (lobby)) idx = lobby_poll_get_free_idx (lobby);

		if (idx >= 0) {
			lobby->players_fds[idx].fd = connection->socket->sock_fd;
			lobby->players_fds[idx].events = POLLIN;
			lobby->players_fds[idx].revents = 0;
			lobby->current_players_fds++;

			retval = 0;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_GAME,
				"Failed to realloc lobby %s poll structure!",
				lobby->id->str
			);
		}
	}

	return retval;

}

// unregisters a player's client connection from the lobby poll structure
// returns 0 on success, 1 on error
u8 lobby_poll_unregister_connection (Lobby *lobby, Player *player, Connection *connection) {

	u8 retval = 1;

	if (lobby && player && connection && lobby->players_fds) {
		const i32 idx = lobby_poll_get_idx_with_sock_fd (lobby, connection->socket->sock_fd);
		if (idx >= 0) {
			lobby->players_fds[idx].fd = -1;
			lobby->players_fds[idx].events = 0;
			lobby->players_fds[idx].revents = 0;
			lobby->current_players_fds--;

			retval = 0;
		}
	}

	return retval;

}

static u8 lobby_wake_open (Lobby *lobby) {

	u8 retval = 1;

	if (lobby->wake_fds[LOBBY_WAKE_READ] > -1) {
		retval = 0;
	}

	else if (!pipe (lobby->wake_fds)) {
		(void) fcntl (lobby->wake_fds[LOBBY_WAKE_READ], F_SETFL, O_NONBLOCK);
		(void) fcntl (lobby->wake_fds[LOBBY_WAKE_WRITE], F_SETFL, O_NONBLOCK);
		(void) fcntl (lobby->wake_fds[LOBBY_WAKE_READ], F_SETFD, FD_CLOEXEC);
		(void) fcntl (lobby->wake_fds[LOBBY_WAKE_WRITE], F_SETFD, FD_CLOEXEC);

		retval = 0;
	}

	return retval;

}

static void lobby_wake_drain (const Lobby *lobby) {

	char buffer[LOBBY_WAKE_DRAIN_SIZE];
	while (read (lobby->wake_fds[LOBBY_WAKE_READ], buffer, LOBBY_WAKE_DRAIN_SIZE) > 0) {}

}

// the poll works on its own copy of the fds, with the pipe that wakes it up at the end
// returns the copy, that grows with the fds of the lobby, or NULL on error
static struct pollfd *lobby_poll_copy_fds (
	Lobby *lobby, struct pollfd *polled, u32 *polled_size, u32 *n_fds
) {

	game_cerver_lock (lobby->cerver);

	const u32 size = lobby->max_players_fds + 1;
	if (size > *polled_size) {
		struct pollfd *bigger = (struct pollfd *) realloc (polled, size * sizeof (struct pollfd));
		if (bigger) {
			polled = bigger;
			*polled_size = size;
		}
	}

	if (polled && (size <= *polled_size)) {
		if (lobby->max_players_fds) {
			(void) memcpy (polled, lobby->players_fds, lobby->max_players_fds * sizeof (struct pollfd));
		}

		polled[lobby->max_players_fds].fd = lobby->wake_fds[LOBBY_WAKE_READ];
		polled[lobby->max_players_fds].events = POLLIN;
		polled[lobby->max_players_fds].revents = 0;

		*n_fds = lobby->max_players_fds;
	}

	game_cerver_unlock (lobby->cerver);

	return polled;

}

// receives a fd of a player with the receive of the cerver,
// the packets get the lobby from the connection
static void lobby_poll_handle (
	Lobby *lobby, const struct pollfd *polled,
	char *buffer, const size_t buffer_size
) {

	CerverReceive *cr = cerver_receive_create (RECEIVE_TYPE_NORMAL, lobby->cerver, polled->fd);
	if (cr) {
		if (cr->socket) {
			cr->lobby = lobby;

			// the data that arrived is read first, even if the connection was also closed
			if (polled->revents & POLLIN) {
				cerver_receive_internal (cr, buffer, buffer_size);
			}

			else if (polled->revents & (POLLHUP | POLLERR | POLLNVAL)) {
				cerver_receive_handle_failed (cr);
			}
		}

		cerver_receive_delete (cr);
	}

}

// lobby default handler, receives the packets of the players while the lobby runs
void lobby_poll (void *ptr) {

	CerverLobby *cerver_lobby = (CerverLobby *) ptr;
	Lobby *lobby = cerver_lobby ? cerver_lobby->lobby : NULL;

	if (lobby) {
		const size_t buffer_size = lobby->cerver->receive_buffer_size;
		char *buffer = (char *) malloc (buffer_size);

		struct pollfd *polled = NULL;
		u32 polled_size = 0;

		#ifdef CERVER_DEBUG
		cerver_log (
			LOG_TYPE_SUCCESS, LOG_TYPE_GAME,
			"Lobby %s poll has started!", lobby->id->str
		);
		#endif

		while (buffer && lobby_is_running (lobby)) {
			u32 n_fds = 0;
			polled = lobby_poll_copy_fds (lobby, polled, &polled_size, &n_fds);
			if (!polled) break;

			const int poll_retval = poll (polled, n_fds + 1, (int) lobby->poll_timeout);

			if (poll_retval < 0) {
				if (errno == EINTR) continue;

				cerver_log (
					LOG_TYPE_ERROR, LOG_TYPE_GAME,
					"Lobby %s poll has failed!", lobby->id->str
				);

				break;
			}

			if (polled[n_fds].revents) lobby_wake_drain (lobby);

			// every slot is checked, the players that left leave holes
			for (u32 i = 0; (i < n_fds) && lobby_is_running (lobby); i++) {
				if ((polled[i].fd > -1) && polled[i].revents) {
					lobby_poll_handle (lobby, &polled[i], buffer, buffer_size);
				}
			}
		}

		free (polled);
		free (buffer);
	}

}

#pragma endregion

/*** Public lobby functions ***/

#pragma region public

// creates and inits a new lobby
// creates a new user associated with the client and makes him the owner
Lobby *lobby_create (
	Cerver *cerver, Client *client,
	bool use_default_handler, Action custom_handler, u32 max_players
) {

	Lobby *lobby = NULL;

	if (cerver && client && cerver->cerver_data) {
		GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

		lobby = lobby_new ();
		if (lobby) {
			lobby->cerver = cerver;

			if (!lobby_init (game_cerver, lobby, max_players)) {
				if (!use_default_handler && custom_handler) {
					lobby->handler = custom_handler;
					lobby->default_handler = false;
				}

				// make the client that make the request, the lobby owner
				Player *owner = player_new ();
				if (owner) {
					owner->client = client;

					game_cerver_lock (cerver);

					if (!player_register_to_lobby (lobby, owner)) {
						lobby->owner = owner;
					}

					else {
						player_delete (owner);
						owner = NULL;
					}

					game_cerver_unlock (cerver);
				}

				if (!owner) {
					cerver_log (
						LOG_TYPE_ERROR, LOG_TYPE_PLAYER,
						"Failed to register owner to the lobby %s.",
						lobby->id->str
					);

					lobby_delete (lobby);
					lobby = NULL;
				}
			}

			else {
				#ifdef CERVER_DEBUG
				cerver_log (LOG_TYPE_ERROR, LOG_TYPE_GAME, "Failed to init the new lobby!");
				#endif

				lobby_delete (lobby);
				lobby = NULL;
			}
		}
	}

	return lobby;

}

// the client is already the player of a lobby
bool lobby_client_is_player (const Client *client) {

	bool player = false;

	for (ListElement *le = dlist_start (client->connections); le && !player; le = le->next) {
		player = (((Connection *) le->data)->lobby != NULL);
	}

	return player;

}

// a client joins a lobby that has a place for it
static bool lobby_can_join (const Lobby *lobby, const Client *client) {

	return !lobby->ended
		&& !lobby->in_game
		&& (lobby->n_current_players < lobby->max_players)
		&& !lobby_player_of_client (lobby, client)
		&& !lobby_client_is_player (client);

}

// a client wants to join a game, so we create a new player and register him to the lobby
// returns 0 on success, 1 on error
u8 lobby_join (Cerver *cerver, Client *client, Lobby *lobby) {

	u8 retval = 1;

	if (cerver && client && lobby && cerver->cerver_data) {
		game_cerver_lock (cerver);

		if (lobby_can_join (lobby, client)) {
			Player *member = player_new ();
			if (member) {
				member->client = client;

				if (!player_register_to_lobby (lobby, member)) {
					// a player that joins a running lobby gets its poll
					if (lobby->poll_connections) lobby_player_connections_take (lobby, member);

					retval = 0;
				}

				else {
					player_delete (member);
				}
			}
		}

		else {
			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_PLAYER,
				"Client %lu can not join lobby %s.",
				client->id, lobby->id->str
			);
			#endif
		}

		game_cerver_unlock (cerver);
	}

	return retval;

}

// the player of the client in the lobby
Player *lobby_player_of_client (const Lobby *lobby, const Client *client) {

	Player *found = NULL;

	if (lobby && client) {
		for (ListElement *le = dlist_start (lobby->players); le && !found; le = le->next) {
			if (((Player *) le->data)->client == client) found = (Player *) le->data;
		}
	}

	return found;

}

static bool lobby_has_player (const Lobby *lobby, const Player *player) {

	bool found = false;

	for (ListElement *le = dlist_start (lobby->players); le && !found; le = le->next) {
		found = (le->data == player);
	}

	return found;

}

static void lobby_player_on_leave (Lobby *lobby, Player *player) {

	if (lobby->game_type && lobby->game_type->on_lobby_leave) {
		LobbyPlayer lobby_player = { lobby, player };
		lobby->game_type->on_lobby_leave (&lobby_player);
	}

}

// a player of the lobby that left, ended its connection or stopped sending
// returns true if the lobby is empty after it
bool lobby_player_remove (Lobby *lobby, Player *player) {

	bool empty = false;

	if (lobby && player) {
		game_cerver_lock (lobby->cerver);

		if (!lobby->ended && lobby_has_player (lobby, player)) {
			const bool poll_connections = lobby->poll_connections;

			lobby_player_on_leave (lobby, player);

			(void) player_unregister_from_lobby (lobby, player);

			if (poll_connections) {
				for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
					(void) connection_register_to_cerver_poll (lobby->cerver, (Connection *) le->data);
				}
			}

			cerver_event_trigger_lobby (CERVER_EVENT_LOBBY_LEAVE, lobby->cerver, player->client, NULL, lobby);

			player_delete (player);

			empty = (lobby->n_current_players == 0);
		}

		game_cerver_unlock (lobby->cerver);
	}

	return empty;

}

// called when a player requests to leave the lobby
// returns 0 on success, 1 on error
u8 lobby_leave (Cerver *cerver, Lobby *lobby, Player *player) {

	u8 retval = 1;

	if (cerver && lobby && player) {
		// the lobby is not released while the last player tears it down
		(void) lobby_reference (lobby);

		game_cerver_lock (cerver);
		const bool found = lobby_has_player (lobby, player);
		const bool empty = found ? lobby_player_remove (lobby, player) : false;
		game_cerver_unlock (cerver);

		if (found) {
			if (empty) (void) lobby_teardown ((GameCerver *) cerver->cerver_data, lobby);

			retval = 0;
		}

		lobby_release (lobby);
	}

	return retval;

}

// the players that did not send a packet in the player timeout of the game type leave the lobby
// returns true if the lobby is empty after it
bool lobby_players_check_timeouts (Lobby *lobby, const u64 now) {

	bool empty = false;

	const u64 timeout = lobby->game_type ?
		((u64) lobby->game_type->player_timeout * LOBBY_MILLISECONDS_PER_SECOND) : 0;

	if (timeout > 0) {
		game_cerver_lock (lobby->cerver);

		Player *expired = NULL;
		do {
			expired = NULL;
			for (ListElement *le = dlist_start (lobby->players); le && !expired; le = le->next) {
				Player *player = (Player *) le->data;
				const u64 last = __atomic_load_n (&player->last_activity, __ATOMIC_RELAXED);
				if ((now > last) && ((now - last) >= timeout)) expired = player;
			}

			if (expired) {
				#ifdef CERVER_DEBUG
				cerver_log (
					LOG_TYPE_DEBUG, LOG_TYPE_GAME,
					"Client %lu is inactive and leaves lobby %s.",
					expired->client->id, lobby->id->str
				);
				#endif

				empty = lobby_player_remove (lobby, expired);
			}
		} while (expired && !empty);

		game_cerver_unlock (lobby->cerver);
	}

	return empty;

}

// the custom handler of a lobby runs once with a CerverLobby
static void *lobby_handler_thread (void *cerver_lobby_ptr) {

	CerverLobby *cerver_lobby = (CerverLobby *) cerver_lobby_ptr;
	Cerver *cerver = cerver_lobby->cerver;
	Lobby *lobby = cerver_lobby->lobby;

	lobby->handler (cerver_lobby);

	cerver_lobby_delete (cerver_lobby);
	lobby_release (lobby);

	game_cerver_lobby_thread_end (cerver);

	return NULL;

}

// waits for the next frame, or until the lobby stops
static void lobby_update_wait (Lobby *lobby, const u32 fps) {

	struct timespec deadline = { 0, 0 };
	(void) clock_gettime (CLOCK_REALTIME, &deadline);

	const long frame = LOBBY_NANOSECONDS / (long) fps;
	deadline.tv_nsec += frame;
	if (deadline.tv_nsec >= LOBBY_NANOSECONDS) {
		deadline.tv_sec += 1;
		deadline.tv_nsec -= LOBBY_NANOSECONDS;
	}

	(void) pthread_mutex_lock (&lobby->stop_mutex);
	if (lobby_is_running (lobby)) {
		(void) pthread_cond_timedwait (&lobby->stop_cond, &lobby->stop_mutex, &deadline);
	}
	(void) pthread_mutex_unlock (&lobby->stop_mutex);

}

// calls the update of the lobby every frame and removes the inactive players
static void *lobby_update_thread (void *cerver_lobby_ptr) {

	CerverLobby *cerver_lobby = (CerverLobby *) cerver_lobby_ptr;
	Cerver *cerver = cerver_lobby->cerver;
	Lobby *lobby = cerver_lobby->lobby;

	const u32 fps = (lobby->game_type && lobby->game_type->fps) ? lobby->game_type->fps : GAME_TYPE_DEFAULT_FPS;

	while (lobby_is_running (lobby)) {
		if (lobby->update) lobby->update (cerver_lobby);

		if (lobby_players_check_timeouts (lobby, player_activity_now ())) {
			(void) lobby_teardown ((GameCerver *) lobby->cerver->cerver_data, lobby);
		}

		else {
			lobby_update_wait (lobby, fps);
		}
	}

	cerver_lobby_delete (cerver_lobby);
	lobby_release (lobby);

	game_cerver_lobby_thread_end (cerver);

	return NULL;

}

// the lobby poll thread runs the default handler with its own CerverLobby
static void *lobby_poll_thread (void *cerver_lobby_ptr) {

	CerverLobby *cerver_lobby = (CerverLobby *) cerver_lobby_ptr;
	Cerver *cerver = cerver_lobby->cerver;
	Lobby *lobby = cerver_lobby->lobby;

	lobby_poll (cerver_lobby);

	cerver_lobby_delete (cerver_lobby);
	lobby_release (lobby);

	game_cerver_lobby_thread_end (cerver);

	return NULL;

}

// creates a thread of the lobby with its own reference and CerverLobby
static bool lobby_thread_create (
	Cerver *cerver, Lobby *lobby,
	pthread_t *thread, void *(*work) (void *)
) {

	bool created = false;

	game_cerver_lobby_thread_start (cerver);

	CerverLobby *cerver_lobby = cerver_lobby_new (cerver, lobby_reference (lobby));
	if (cerver_lobby) {
		created = !pthread_create (thread, NULL, work, cerver_lobby);
		if (!created) {
			cerver_lobby_delete (cerver_lobby);
		}
	}

	if (!created) {
		lobby_release (lobby);
		game_cerver_lobby_thread_end (cerver);
	}

	return created;

}

// the lobby poll only takes the connections of a cerver with the poll handler type,
// with the threads handler, each connection already has its thread
static bool lobby_uses_poll (const Cerver *cerver, const Lobby *lobby) {

	return lobby->default_handler && (cerver->handler_type == CERVER_HANDLER_TYPE_POLL);

}

static void lobby_start_poll (Cerver *cerver, Lobby *lobby) {

	if (!lobby_poll_init (lobby, lobby->max_players) && !lobby_wake_open (lobby)) {
		for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
			lobby_player_connections_take (lobby, (Player *) le->data);
		}

		lobby->poll_connections = true;

		lobby->handler_started = lobby_thread_create (
			cerver, lobby, &lobby->handler_thread_id, lobby_poll_thread
		);
	}

	if (!lobby->handler_started) {
		lobby_players_connections_return (lobby);

		cerver_log_error ("Failed to start the poll of lobby %s!", lobby->id->str);
	}

}

static bool lobby_needs_update (const Lobby *lobby) {

	return lobby->update || (lobby->game_type && (lobby->game_type->player_timeout > 0));

}

// starts the lobby's handler and update threads
// returns 0 on success, 1 on error
u8 lobby_start (Cerver *cerver, Lobby *lobby) {

	u8 retval = 1;

	if (cerver && lobby) {
		game_cerver_lock (cerver);

		if (!lobby->ended && !lobby_is_running (lobby)) {
			__atomic_store_n (&lobby->running, true, __ATOMIC_SEQ_CST);

			if (lobby_uses_poll (cerver, lobby)) {
				lobby_start_poll (cerver, lobby);
			}

			else if (!lobby->default_handler && lobby->handler) {
				lobby->handler_started = lobby_thread_create (
					cerver, lobby, &lobby->handler_thread_id, lobby_handler_thread
				);
			}

			if (lobby_needs_update (lobby)) {
				lobby->update_started = lobby_thread_create (
					cerver, lobby, &lobby->update_thread_id, lobby_update_thread
				);
			}

			retval = 0;
		}

		game_cerver_unlock (cerver);
	}

	return retval;

}

#pragma endregion

/*** seralization ***/

#pragma region serialization

static void lobby_summary_players_set (LobbySummary *summary, const Lobby *lobby) {

	summary->players_count = 0;

	for (ListElement *le = dlist_start (lobby->players);
		le && (summary->players_count < LOBBY_SUMMARY_PLAYERS_MAX); le = le->next) {
		const Player *player = (const Player *) le->data;
		LobbyPlayerSummary *player_summary = &summary->players[summary->players_count];

		serializer_object_init (&lobby_player_summary_schema, player_summary);
		player_summary->client_id = player->client->id;
		if (player->id) {
			(void) snprintf (player_summary->player_id, LOBBY_PLAYER_ID_SIZE, "%s", player->id->str);
		}

		player_summary->owner = (player == lobby->owner);

		summary->players_count++;
	}

}

// fills the summary with the current state of the lobby
void lobby_summary_set (LobbySummary *summary, const Lobby *lobby, const bool created) {

	if (summary && lobby) {
		serializer_object_init (&lobby_summary_schema, summary);

		(void) snprintf (summary->id, LOBBY_ID_SIZE, "%s", lobby->id->str);
		summary->creation = (i64) lobby->creation_time_stamp;

		if (lobby->game_type && lobby->game_type->name) {
			(void) snprintf (summary->game_type, LOBBY_GAME_TYPE_SIZE, "%s", lobby->game_type->name->str);
		}

		summary->created = created;
		summary->running = lobby_is_running (lobby);
		summary->in_game = lobby->in_game;
		summary->max_players = lobby->max_players;
		summary->n_players = lobby->n_current_players;
		summary->owner_client_id = (lobby->owner && lobby->owner->client) ? lobby->owner->client->id : 0;

		lobby_summary_players_set (summary, lobby);

		serializer_object_init (&lobby_settings_summary_schema, &summary->settings);
		summary->settings.max_players = lobby->max_players;
		if (lobby->game_type) {
			summary->settings.player_timeout = lobby->game_type->player_timeout;
			summary->settings.fps = lobby->game_type->fps;
		}
	}

}

// creates a GAME packet of the request type with the summary of the lobby
Packet *lobby_summary_packet_create (const Lobby *lobby, const u32 request_type, const bool created) {

	Packet *packet = NULL;

	LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
	if (summary) {
		game_cerver_lock (lobby->cerver);
		lobby_summary_set (summary, lobby, created);
		game_cerver_unlock (lobby->cerver);

		packet = packet_create_serialized (
			PACKET_TYPE_GAME, request_type,
			&lobby_summary_schema, summary
		);

		free (summary);
	}

	return packet;

}

// sends a lobby and all of its data to the client
// created: if the lobby was just created
void lobby_send (
	Lobby *lobby, bool created,
	Cerver *cerver, Client *client, Connection *connection
) {

	if (lobby) {
		Packet *lobby_packet = lobby_summary_packet_create (
			lobby,
			created ? GAME_PACKET_TYPE_LOBBY_CREATE : GAME_PACKET_TYPE_LOBBY_JOIN,
			created
		);

		if (lobby_packet) {
			packet_set_network_values (lobby_packet, cerver, client, connection, lobby);
			(void) packet_send (lobby_packet, 0, NULL, false);
			packet_delete (lobby_packet);
		}
	}

}

// sends the summary of the lobby to every player of the lobby
void lobby_send_to_players (Lobby *lobby, const u32 request_type) {

	if (lobby) {
		Packet *packet = lobby_summary_packet_create (lobby, request_type, false);
		if (packet) {
			player_broadcast_to_all (lobby->cerver, lobby, packet, PROTOCOL_TCP, 0);
			packet_delete (packet);
		}
	}

}

static void lobby_packet_release (void *lobby_ptr) {

	lobby_release ((Lobby *) lobby_ptr);

}

// the lobby of a received packet: takes a reference to the lobby of the connection
void lobby_packet_attach (Packet *packet) {

	if (
		packet && packet->cerver && packet->connection
		&& (packet->cerver->type == CERVER_TYPE_GAME) && packet->cerver->cerver_data
	) {
		game_cerver_lock (packet->cerver);

		Lobby *lobby = lobby_reference (packet->connection->lobby);
		if (lobby) player_activity_update (lobby_player_of_client (lobby, packet->client));

		game_cerver_unlock (packet->cerver);

		if (lobby) {
			packet->lobby = lobby;
			packet->release = lobby_packet_release;
			packet->release_args = lobby;
		}
	}

}

#pragma endregion
