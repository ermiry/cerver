#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/client/client.h"
#include "cerver/client/events.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/events.h"
#include "cerver/packets/packets.h"
#include "cerver/packets/serializer.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"
#include "cerver/game/player.h"
#include "cerver/game/types.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#define GAME_ERROR_BAD_REQUEST			"Bad game request!"
#define GAME_ERROR_NOT_GAME_CERVER		"The cerver does not handle game packets!"
#define GAME_ERROR_UNKNOWN_REQUEST		"Unknown game request!"
#define GAME_ERROR_BAD_GAME_TYPE		"Bad game type!"
#define GAME_ERROR_CREATE				"Cerver error while creating lobby!"
#define GAME_ERROR_ALREADY_PLAYER		"The client is already in a lobby!"
#define GAME_ERROR_LOBBY_NOT_FOUND		"Not lobby found with id!"
#define GAME_ERROR_JOIN					"Failed to join lobby!"
#define GAME_ERROR_NOT_PLAYER			"The client is not in the lobby!"
#define GAME_ERROR_NOT_OWNER			"You are not the lobby owner!"
#define GAME_ERROR_IN_GAME				"The game already started!"
#define GAME_ERROR_START				"Failed to start game in lobby!"

/*** Game Cerver */

#pragma region Game Cerver

static GameCerverStats *game_cerver_stats_new (void) {

	GameCerverStats *stats = (GameCerverStats *) malloc (sizeof (GameCerverStats));
	if (stats) (void) memset (stats, 0, sizeof (GameCerverStats));
	return stats;

}

static void game_cerver_stats_delete (GameCerverStats *stats) {

	if (stats) free (stats);

}

void game_cerver_stats_print (Cerver *cerver) {

	if (cerver) {
		if (cerver->type == CERVER_TYPE_GAME) {
			GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

			(void) printf ("Current active lobbys:         %u\n", game_cerver->stats->current_active_lobbys);
			(void) printf ("Total lobbys created:          %u\n", game_cerver->stats->lobbys_created);
		}

		else {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_CERVER,
				"Can't print game stats of cerver %s -- it is not a game cerver.",
				cerver->info->name
			);
		}
	}

}

static void game_lock_init (GameCerver *game) {

	pthread_mutexattr_t attributes;
	(void) pthread_mutexattr_init (&attributes);
	(void) pthread_mutexattr_settype (&attributes, PTHREAD_MUTEX_RECURSIVE);
	(void) pthread_mutex_init (&game->lock, &attributes);
	(void) pthread_mutexattr_destroy (&attributes);

}

GameCerver *game_new (void) {

	GameCerver *game = (GameCerver *) malloc (sizeof (GameCerver));
	if (game) {
		game->cerver = NULL;

		game_lock_init (game);

		game->lobby_threads = 0;
		(void) pthread_mutex_init (&game->lobby_threads_mutex, NULL);
		(void) pthread_cond_init (&game->lobby_threads_cond, NULL);

		game->current_lobbys = dlist_init (lobby_delete, lobby_comparator);

		game->lobby_id_generator = lobby_default_id_generator;
		game->player_comparator = NULL;

		game->game_types = dlist_init (game_type_delete, NULL);

		game->load_game_data = NULL;
		game->delete_game_data = NULL;
		game->game_data = NULL;

		game->final_game_action = NULL;
		game->final_action_args = NULL;

		game->stats = game_cerver_stats_new ();
	}

	return game;

}

void game_delete (void *game_ptr) {

	if (game_ptr) {
		GameCerver *game = (GameCerver *) game_ptr;

		dlist_delete (game->current_lobbys);

		dlist_delete (game->game_types);

		if (game->game_data) {
			if (game->delete_game_data)
				game->delete_game_data (game->game_data);
			else free (game->game_data);
		}

		game_cerver_stats_delete (game->stats);

		(void) pthread_mutex_destroy (&game->lock);

		(void) pthread_mutex_destroy (&game->lobby_threads_mutex);
		(void) pthread_cond_destroy (&game->lobby_threads_cond);

		free (game);
	}

}

static GameCerver *game_cerver_of (const Cerver *cerver) {

	return (cerver && (cerver->type == CERVER_TYPE_GAME)) ? (GameCerver *) cerver->cerver_data : NULL;

}

// takes the lock of the game cerver of the cerver, NULL is safe
void game_cerver_lock (const Cerver *cerver) {

	GameCerver *game = game_cerver_of (cerver);
	if (game) (void) pthread_mutex_lock (&game->lock);

}

// releases the lock of the game cerver of the cerver, NULL is safe
void game_cerver_unlock (const Cerver *cerver) {

	GameCerver *game = game_cerver_of (cerver);
	if (game) (void) pthread_mutex_unlock (&game->lock);

}

/*** Game Cerver configuration ***/

// sets the game cerver's cerver reference
void game_set_cerver_reference (GameCerver *game_cerver, Cerver *cerver) {

	if (game_cerver) game_cerver->cerver = cerver;

}

// option to set the game cerver lobby id generator
void game_set_lobby_id_generator (GameCerver *game_cerver, void *(*lobby_id_generator) (const void *)) {

	if (game_cerver) game_cerver->lobby_id_generator = lobby_id_generator;

}

// option to set the game cerver comparator
void game_set_player_comparator (GameCerver *game_cerver, Comparator player_comparator) {

	if (game_cerver) game_cerver->player_comparator = player_comparator;

}

// sets a way to get and destroy game cerver game data
void game_set_load_game_data (
	GameCerver *game_cerver, Action load_game_data, Action delete_game_data
) {

	if (game_cerver) {
		game_cerver->load_game_data = load_game_data;
		game_cerver->delete_game_data = delete_game_data;
	}

}

// option to set an action to be performed right before the game cerver teardown
// eg. send a message to all players
void game_set_final_action (
	GameCerver *game_cerver, Action final_action, void *final_action_args
) {

	if (game_cerver) {
		game_cerver->final_game_action = final_action;
		game_cerver->final_action_args = final_action_args;
	}

}

/*** Game Cerver Lobbys ***/

// registers a new lobby to the game cerver
void game_cerver_register_lobby (GameCerver *game_cerver, Lobby *lobby) {

	if (game_cerver && lobby) {
		(void) pthread_mutex_lock (&game_cerver->lock);

		// check if the lobby is already registered to the game cerver
		if (!dlist_search (game_cerver->current_lobbys, lobby, NULL)) {
			(void) dlist_insert_at_end (game_cerver->current_lobbys, lobby);
			game_cerver->stats->current_active_lobbys += 1;
		}

		else {
			cerver_log (
				LOG_TYPE_WARNING, LOG_TYPE_GAME,
				"Lobby %s is already registered to its game cerver.",
				lobby->id->str
			);
		}

		(void) pthread_mutex_unlock (&game_cerver->lock);
	}

}

// unregisters a lobby from the game cerver
void game_cerver_unregister_lobby (GameCerver *game_cerver, Lobby *lobby) {

	if (game_cerver && lobby) {
		(void) pthread_mutex_lock (&game_cerver->lock);

		// dlist_remove_element () with a NULL element removes the start of the list
		ListElement *lobby_element = dlist_get_element (
			game_cerver->current_lobbys, lobby, NULL
		);

		if (lobby_element && (lobby_element->data == lobby)) {
			(void) dlist_remove_element (game_cerver->current_lobbys, lobby_element);
			game_cerver->stats->current_active_lobbys -= 1;
		}

		(void) pthread_mutex_unlock (&game_cerver->lock);
	}

}

// counts a thread of a lobby that starts
void game_cerver_lobby_thread_start (const Cerver *cerver) {

	GameCerver *game = game_cerver_of (cerver);
	if (game) {
		(void) pthread_mutex_lock (&game->lobby_threads_mutex);
		game->lobby_threads += 1;
		(void) pthread_mutex_unlock (&game->lobby_threads_mutex);
	}

}

// counts a thread of a lobby that ends, as the last thing it does
void game_cerver_lobby_thread_end (const Cerver *cerver) {

	GameCerver *game = game_cerver_of (cerver);
	if (game) {
		(void) pthread_mutex_lock (&game->lobby_threads_mutex);
		game->lobby_threads -= 1;
		if (!game->lobby_threads) (void) pthread_cond_broadcast (&game->lobby_threads_cond);
		(void) pthread_mutex_unlock (&game->lobby_threads_mutex);
	}

}

static void game_cerver_lobby_threads_wait (GameCerver *game) {

	(void) pthread_mutex_lock (&game->lobby_threads_mutex);

	while (game->lobby_threads) {
		(void) pthread_cond_wait (&game->lobby_threads_cond, &game->lobby_threads_mutex);
	}

	(void) pthread_mutex_unlock (&game->lobby_threads_mutex);

}

// tears down every lobby before the cerver ends its connections and its handlers
void game_cerver_stop (Cerver *cerver) {

	GameCerver *game = game_cerver_of (cerver);
	if (game) {
		Lobby *lobby = NULL;
		do {
			(void) pthread_mutex_lock (&game->lock);
			ListElement *start = dlist_start (game->current_lobbys);
			lobby = start ? lobby_reference ((Lobby *) start->data) : NULL;
			(void) pthread_mutex_unlock (&game->lock);

			if (lobby) {
				(void) lobby_teardown (game, lobby);
				lobby_release (lobby);
			}
		} while (lobby);

		// the threads that ended their own lobbies are detached
		game_cerver_lobby_threads_wait (game);
	}

}

#pragma endregion

/*** Game Packet Handler ***/

#pragma region game handler

static void game_error_send (const Packet *packet, const CerverErrorType error_type, const char *msg) {

	Packet *error_packet = error_packet_generate (error_type, msg);
	if (error_packet) {
		packet_set_network_values (error_packet, packet->cerver, packet->client, packet->connection, NULL);
		(void) packet_send (error_packet, 0, NULL, false);
		packet_delete (error_packet);
	}

}

static void game_packet_send (const Packet *request, Packet *response) {

	if (response) {
		packet_set_network_values (response, request->cerver, request->client, request->connection, NULL);
		(void) packet_send (response, 0, NULL, false);
		packet_delete (response);
	}

}

static bool game_request_decode (const Packet *packet, const SerializerSchema *schema, void *object) {

	return (packet_get_object (packet, schema, NULL, object) == SERIALIZER_RESULT_NONE);

}

// the lobby of the request, with the lock of the game cerver
static Lobby *game_lobby_find (const GameCerver *game, const char *id) {

	Lobby *found = NULL;

	for (ListElement *le = dlist_start (game->current_lobbys); le && !found; le = le->next) {
		Lobby *lobby = (Lobby *) le->data;
		if (!lobby->ended && !strcmp (lobby->id->str, id)) found = lobby;
	}

	return found;

}

static bool game_lobby_is_type (const Lobby *lobby, const char *game_type) {

	return lobby->game_type && lobby->game_type->name && !strcmp (lobby->game_type->name->str, game_type);

}

static bool game_client_is_owner (const Lobby *lobby, const Client *client) {

	return lobby->owner && (lobby->owner->client == client);

}

static void game_lobby_on_join (Lobby *lobby, Player *player) {

	if (player && lobby->game_type && lobby->game_type->on_lobby_join) {
		LobbyPlayer lobby_player = { lobby, player };
		lobby->game_type->on_lobby_join (&lobby_player);
	}

}

// creates a lobby of the game type with the client as its owner, with the lock of the game cerver
static Lobby *game_lobby_create_of_type (GameCerver *game, const Packet *packet, GameType *game_type) {

	Lobby *lobby = lobby_create (
		packet->cerver, packet->client,
		game_type->use_default_handler, game_type->custom_handler, game_type->max_players
	);

	if (lobby) {
		lobby->game_type = game_type;

		game_cerver_register_lobby (game, lobby);
		game->stats->lobbys_created += 1;

		game_lobby_on_join (lobby, lobby->owner);
	}

	return lobby;

}

static void game_lobby_created (const Packet *packet, Lobby *lobby) {

	lobby_send (lobby, true, packet->cerver, packet->client, packet->connection);

	cerver_event_trigger_lobby (
		CERVER_EVENT_LOBBY_CREATE, packet->cerver, packet->client, packet->connection, lobby
	);

}

static void game_lobby_joined (const Packet *packet, Lobby *lobby) {

	lobby_send (lobby, false, packet->cerver, packet->client, packet->connection);

	cerver_event_trigger_lobby (
		CERVER_EVENT_LOBBY_JOIN, packet->cerver, packet->client, packet->connection, lobby
	);

}

static void game_lobby_create (const Packet *packet) {

	GameCerver *game = (GameCerver *) packet->cerver->cerver_data;

	LobbyCreate request;
	if (game_request_decode (packet, &lobby_create_request_schema, &request)) {
		(void) pthread_mutex_lock (&game->lock);

		GameType *game_type = game_type_get_with_name (game->game_types, request.game_type);
		const bool player = lobby_client_is_player (packet->client);
		Lobby *lobby = (game_type && !player) ?
			lobby_reference (game_lobby_create_of_type (game, packet, game_type)) : NULL;

		(void) pthread_mutex_unlock (&game->lock);

		if (lobby) {
			game_lobby_created (packet, lobby);
			lobby_release (lobby);
		}

		else {
			game_error_send (
				packet, CERVER_ERROR_CREATE_LOBBY,
				!game_type ? GAME_ERROR_BAD_GAME_TYPE : (player ? GAME_ERROR_ALREADY_PLAYER : GAME_ERROR_CREATE)
			);
		}
	}

	else {
		game_error_send (packet, CERVER_ERROR_CREATE_LOBBY, GAME_ERROR_BAD_REQUEST);
	}

}

// joins the lobby with the lock of the game cerver, returns true on success
static bool game_lobby_join_locked (const Packet *packet, Lobby *lobby) {

	const bool joined = !lobby_join (packet->cerver, packet->client, lobby);
	if (joined) game_lobby_on_join (lobby, lobby_player_of_client (lobby, packet->client));

	return joined;

}

static void game_lobby_join_specific (const Packet *packet, const LobbyJoin *request) {

	GameCerver *game = (GameCerver *) packet->cerver->cerver_data;

	(void) pthread_mutex_lock (&game->lock);

	Lobby *lobby = game_lobby_find (game, request->lobby_id);
	const bool found = (lobby != NULL);
	const bool joined = found && game_lobby_is_type (lobby, request->game_type)
		&& game_lobby_join_locked (packet, lobby);

	if (joined) (void) lobby_reference (lobby);

	(void) pthread_mutex_unlock (&game->lock);

	if (joined) {
		game_lobby_joined (packet, lobby);
		lobby_release (lobby);
	}

	else {
		game_error_send (
			packet, CERVER_ERROR_JOIN_LOBBY,
			found ? GAME_ERROR_JOIN : GAME_ERROR_LOBBY_NOT_FOUND
		);
	}

}

// the first lobby of the game type with a place for the client
static Lobby *game_lobby_search (const GameCerver *game, const Packet *packet, const char *game_type) {

	Lobby *found = NULL;

	for (ListElement *le = dlist_start (game->current_lobbys); le && !found; le = le->next) {
		Lobby *lobby = (Lobby *) le->data;
		if (
			!lobby->ended && !lobby->in_game && game_lobby_is_type (lobby, game_type)
			&& (lobby->n_current_players < lobby->max_players)
			&& !lobby_player_of_client (lobby, packet->client)
		) {
			found = lobby;
		}
	}

	return found;

}

// joins the first lobby of the game type with a place, or creates one when none has it
static void game_lobby_join_search (const Packet *packet, const LobbyJoin *request) {

	GameCerver *game = (GameCerver *) packet->cerver->cerver_data;

	(void) pthread_mutex_lock (&game->lock);

	GameType *game_type = game_type_get_with_name (game->game_types, request->game_type);

	Lobby *lobby = game_type ? game_lobby_search (game, packet, request->game_type) : NULL;
	bool created = false;

	if (lobby) {
		lobby = game_lobby_join_locked (packet, lobby) ? lobby_reference (lobby) : NULL;
	}

	else if (game_type && !lobby_client_is_player (packet->client)) {
		lobby = lobby_reference (game_lobby_create_of_type (game, packet, game_type));
		created = (lobby != NULL);
	}

	(void) pthread_mutex_unlock (&game->lock);

	if (lobby) {
		if (created) game_lobby_created (packet, lobby);
		else game_lobby_joined (packet, lobby);

		lobby_release (lobby);
	}

	else {
		game_error_send (
			packet, CERVER_ERROR_FIND_LOBBY,
			game_type ? GAME_ERROR_JOIN : GAME_ERROR_BAD_GAME_TYPE
		);
	}

}

static void game_lobby_join (const Packet *packet) {

	LobbyJoin request;
	if (game_request_decode (packet, &lobby_join_request_schema, &request)) {
		if (request.lobby_id[0]) game_lobby_join_specific (packet, &request);
		else game_lobby_join_search (packet, &request);
	}

	else {
		game_error_send (packet, CERVER_ERROR_JOIN_LOBBY, GAME_ERROR_BAD_REQUEST);
	}

}

// the lobby of a LobbyRequest with a reference, NULL with an error sent to the client
static Lobby *game_request_lobby (const Packet *packet, const CerverErrorType error_type) {

	GameCerver *game = (GameCerver *) packet->cerver->cerver_data;

	Lobby *lobby = NULL;

	LobbyRequest request;
	if (game_request_decode (packet, &lobby_request_schema, &request)) {
		(void) pthread_mutex_lock (&game->lock);
		lobby = lobby_reference (game_lobby_find (game, request.lobby_id));
		(void) pthread_mutex_unlock (&game->lock);

		if (!lobby) game_error_send (packet, error_type, GAME_ERROR_LOBBY_NOT_FOUND);
	}

	else {
		game_error_send (packet, error_type, GAME_ERROR_BAD_REQUEST);
	}

	return lobby;

}

static void game_lobby_leave (const Packet *packet) {

	Lobby *lobby = game_request_lobby (packet, CERVER_ERROR_LEAVE_LOBBY);
	if (lobby) {
		game_cerver_lock (packet->cerver);

		Player *player = lobby_player_of_client (lobby, packet->client);
		Packet *response = player ?
			lobby_summary_packet_create (lobby, GAME_PACKET_TYPE_LOBBY_LEAVE, false) : NULL;
		const bool empty = player ? lobby_player_remove (lobby, player) : false;

		game_cerver_unlock (packet->cerver);

		if (player) {
			game_packet_send (packet, response);

			if (empty) (void) lobby_teardown ((GameCerver *) packet->cerver->cerver_data, lobby);
			else lobby_send_to_players (lobby, GAME_PACKET_TYPE_LOBBY_UPDATE);
		}

		else {
			game_error_send (packet, CERVER_ERROR_LEAVE_LOBBY, GAME_ERROR_NOT_PLAYER);
		}

		lobby_release (lobby);
	}

}

// prepares the game of the lobby with the init of its game type, only the owner
static void game_lobby_init (const Packet *packet) {

	Lobby *lobby = game_request_lobby (packet, CERVER_ERROR_GAME_INIT);
	if (lobby) {
		game_cerver_lock (packet->cerver);

		const bool owner = game_client_is_owner (lobby, packet->client);
		const bool in_game = lobby->in_game;

		if (owner && !in_game) {
			if (lobby->game_type && lobby->game_type->init) {
				LobbyPlayer lobby_player = { lobby, lobby->owner };
				lobby->game_type->init (&lobby_player);
			}

			lobby->initialized = true;
		}

		game_cerver_unlock (packet->cerver);

		if (owner && !in_game) {
			game_packet_send (packet, lobby_summary_packet_create (lobby, GAME_PACKET_TYPE_GAME_INIT, false));
		}

		else {
			game_error_send (
				packet, CERVER_ERROR_GAME_INIT,
				owner ? GAME_ERROR_IN_GAME : GAME_ERROR_NOT_OWNER
			);
		}

		lobby_release (lobby);
	}

}

// the owner starts the game: the start of its game type, and the lobby threads
static CerverErrorType game_lobby_start_locked (const Packet *packet, Lobby *lobby, const char **message) {

	CerverErrorType error = CERVER_ERROR_NONE;

	if (!game_client_is_owner (lobby, packet->client)) {
		error = CERVER_ERROR_GAME_START;
		*message = GAME_ERROR_NOT_OWNER;
	}

	else if (lobby->in_game) {
		error = CERVER_ERROR_GAME_START;
		*message = GAME_ERROR_IN_GAME;
	}

	else {
		// the start of the game type sets the handlers and the update before the threads start
		if (lobby->game_type && lobby->game_type->start) (void) lobby->game_type->start (lobby);

		if (!lobby_is_running (lobby) && lobby_start (packet->cerver, lobby)) {
			error = CERVER_ERROR_GAME_START;
			*message = GAME_ERROR_START;
		}

		else {
			lobby->in_game = true;
		}
	}

	return error;

}

// initializes the lobby update (starts the game)
static void game_lobby_start (const Packet *packet) {

	Lobby *lobby = game_request_lobby (packet, CERVER_ERROR_GAME_START);
	if (lobby) {
		const char *message = NULL;

		game_cerver_lock (packet->cerver);
		const CerverErrorType error = game_lobby_start_locked (packet, lobby, &message);
		game_cerver_unlock (packet->cerver);

		if (error == CERVER_ERROR_NONE) {
			lobby_send_to_players (lobby, GAME_PACKET_TYPE_GAME_START);

			cerver_event_trigger_lobby (
				CERVER_EVENT_LOBBY_START, packet->cerver, packet->client, packet->connection, lobby
			);
		}

		else {
			game_error_send (packet, error, message);
		}

		lobby_release (lobby);
	}

}

// sends the current state of the lobby to one of its players
static void game_lobby_update (const Packet *packet) {

	Lobby *lobby = game_request_lobby (packet, CERVER_ERROR_FIND_LOBBY);
	if (lobby) {
		game_cerver_lock (packet->cerver);
		const bool player = (lobby_player_of_client (lobby, packet->client) != NULL);
		game_cerver_unlock (packet->cerver);

		if (player) game_packet_send (packet, lobby_summary_packet_create (lobby, GAME_PACKET_TYPE_LOBBY_UPDATE, false));
		else game_error_send (packet, CERVER_ERROR_FIND_LOBBY, GAME_ERROR_NOT_PLAYER);

		lobby_release (lobby);
	}

}

// the owner destroys the lobby, its players get a LOBBY_DESTROY before it
static void game_lobby_destroy (const Packet *packet) {

	Lobby *lobby = game_request_lobby (packet, CERVER_ERROR_DESTROY_LOBBY);
	if (lobby) {
		game_cerver_lock (packet->cerver);
		const bool owner = game_client_is_owner (lobby, packet->client);
		game_cerver_unlock (packet->cerver);

		if (owner) {
			lobby_send_to_players (lobby, GAME_PACKET_TYPE_LOBBY_DESTROY);
			(void) lobby_teardown ((GameCerver *) packet->cerver->cerver_data, lobby);
		}

		else {
			game_error_send (packet, CERVER_ERROR_DESTROY_LOBBY, GAME_ERROR_NOT_OWNER);
		}

		lobby_release (lobby);
	}

}

static void game_packet_handle_request (const Packet *packet) {

	switch (packet->header.request_type) {
		// prepares lobby's game data structures
		case GAME_PACKET_TYPE_GAME_INIT: game_lobby_init (packet); break;

		// initializes the lobby's handler & update (starts the game)
		case GAME_PACKET_TYPE_GAME_START: game_lobby_start (packet); break;

		case GAME_PACKET_TYPE_LOBBY_CREATE: game_lobby_create (packet); break;
		case GAME_PACKET_TYPE_LOBBY_JOIN: game_lobby_join (packet); break;
		case GAME_PACKET_TYPE_LOBBY_LEAVE: game_lobby_leave (packet); break;
		case GAME_PACKET_TYPE_LOBBY_UPDATE: game_lobby_update (packet); break;
		case GAME_PACKET_TYPE_LOBBY_DESTROY: game_lobby_destroy (packet); break;

		default: game_error_send (packet, CERVER_ERROR_PACKET_ERROR, GAME_ERROR_UNKNOWN_REQUEST); break;
	}

}

// handles a game type packet
void game_packet_handler (Packet *packet) {

	if (packet && packet->cerver && packet->client && packet->connection) {
		if (game_cerver_of (packet->cerver)) game_packet_handle_request (packet);
		else game_error_send (packet, CERVER_ERROR_PACKET_ERROR, GAME_ERROR_NOT_GAME_CERVER);
	}

}

// passes an app packet of a player to the packet handler of its lobby
bool game_lobby_app_packet (Packet *packet) {

	bool taken = false;

	Lobby *lobby = packet ? packet->lobby : NULL;
	if (lobby && lobby->packet_handler && lobby_is_running (lobby)) {
		lobby->packet_handler (packet);
		packet_delete (packet);

		taken = true;
	}

	return taken;

}

// the player of the client of the connection leaves its lobby, when the connection ends
void game_connection_end (Cerver *cerver, Client *client, Connection *connection) {

	if (game_cerver_of (cerver) && client && connection) {
		game_cerver_lock (cerver);

		Lobby *lobby = lobby_reference (connection->lobby);
		Player *player = lobby ? lobby_player_of_client (lobby, client) : NULL;
		const bool empty = player ? lobby_player_remove (lobby, player) : false;

		game_cerver_unlock (cerver);

		if (lobby) {
			if (empty) (void) lobby_teardown ((GameCerver *) cerver->cerver_data, lobby);
			else if (player) lobby_send_to_players (lobby, GAME_PACKET_TYPE_LOBBY_UPDATE);

			lobby_release (lobby);
		}
	}

}

#pragma endregion

/*** Game Client ***/

#pragma region game client

// sends a game request with its object to the game cerver of the connection
// returns 0 on success, 1 on error
u8 game_request_send (
	Client *client, Connection *connection,
	const u32 request_type, const SerializerSchema *schema, const void *object
) {

	u8 retval = 1;

	if (client && connection && schema && object) {
		Packet *packet = packet_create_serialized (PACKET_TYPE_GAME, request_type, schema, object);
		if (packet) {
			packet_set_network_values (packet, NULL, client, connection, NULL);
			retval = packet_send (packet, 0, NULL, false);
			packet_delete (packet);
		}
	}

	return retval;

}

static ClientEventType game_client_event_of (const u32 request_type) {

	ClientEventType event = CLIENT_EVENT_NONE;

	switch (request_type) {
		case GAME_PACKET_TYPE_GAME_INIT: event = CLIENT_EVENT_GAME_INIT; break;
		case GAME_PACKET_TYPE_GAME_START: event = CLIENT_EVENT_LOBBY_START; break;
		case GAME_PACKET_TYPE_LOBBY_CREATE: event = CLIENT_EVENT_LOBBY_CREATE; break;
		case GAME_PACKET_TYPE_LOBBY_JOIN: event = CLIENT_EVENT_LOBBY_JOIN; break;
		case GAME_PACKET_TYPE_LOBBY_LEAVE: event = CLIENT_EVENT_LOBBY_LEAVE; break;
		case GAME_PACKET_TYPE_LOBBY_UPDATE: event = CLIENT_EVENT_LOBBY_UPDATE; break;
		case GAME_PACKET_TYPE_LOBBY_DESTROY: event = CLIENT_EVENT_LOBBY_DESTROY; break;

		default: break;
	}

	return event;

}

// handles a GAME packet that a client receives
void game_client_packet_handler (Packet *packet) {

	const ClientEventType event = packet ? game_client_event_of (packet->header.request_type) : CLIENT_EVENT_NONE;

	if (event != CLIENT_EVENT_NONE) {
		LobbySummary *summary = (LobbySummary *) malloc (sizeof (LobbySummary));
		if (summary && game_request_decode (packet, &lobby_summary_schema, summary)) {
			client_event_trigger_with_response (
				event, packet->client, packet->connection,
				summary, free
			);
		}

		else {
			cerver_log_warning ("Got a game packet without a lobby summary!");

			free (summary);
		}
	}

	else {
		cerver_log_warning ("Got a game packet of unknown type!");
	}

}

#pragma endregion
