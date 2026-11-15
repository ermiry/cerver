#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/packets/packets.h"

#include "cerver/game/game.h"
#include "cerver/game/lobby.h"
#include "cerver/game/player.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#define PLAYER_MILLISECONDS_PER_SECOND			1000
#define PLAYER_NANOSECONDS_PER_MILLISECOND		1000000

// the milliseconds of the monotonic clock that the activity of the players uses
u64 player_activity_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return ((u64) now.tv_sec * PLAYER_MILLISECONDS_PER_SECOND)
		+ ((u64) now.tv_nsec / PLAYER_NANOSECONDS_PER_MILLISECOND);

}

Player *player_new (void) {

	Player *player = (Player *) malloc (sizeof (Player));
	if (player) {
		player->id = NULL;
		player->client = NULL;
		player->last_activity = player_activity_now ();
		player->data = NULL;
		player->data_delete = NULL;
	}

	return player;

}

void player_delete (void *player_ptr) {

	if (player_ptr) {
		Player *player = (Player *) player_ptr;

		str_delete (player->id);

		player->client = NULL;

		if (player->data) {
			if (player->data_delete)
				player->data_delete (player->data);
			else free (player->data);
		}

		free (player);
	}

}

// sets the player id
void player_set_id (Player *player, const char *id) {

	if (player) {
		// a second id replaces the first one
		str_delete (player->id);
		player->id = str_new (id);
	}

}

// sets player data and a way to delete it
void player_set_data (Player *player, void *data, Action data_delete) {

	if (player) {
		player->data = data;
		player->data_delete = data_delete;
	}

}

int player_comparator_with_id (const void *a, const void *b) {

	return str_compare (((Player *) a)->id, ((Player *) b)->id);

}

int player_comparator_client_id (const void *a, const void *b) {

	return client_comparator_client_id (((Player *) a)->client, ((Player *) b)->client);

}

// the player sent a packet now
void player_activity_update (Player *player) {

	if (player) {
		__atomic_store_n (&player->last_activity, player_activity_now (), __ATOMIC_RELAXED);
	}

}

// registers each of the player's client connections to the lobby poll structures
u8 player_register_to_lobby_poll (Lobby *lobby, Player *player) {

	u8 errors = 0;

	if (lobby && player && player->client) {
		for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
			errors |= lobby_poll_register_connection (lobby, player, (Connection *) le->data);
		}
	}

	else {
		errors = 1;
	}

	return errors;

}

// the packets of the connections of the player are of the lobby
static void player_connections_set_lobby (const Player *player, Lobby *current, Lobby *lobby) {

	for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
		Connection *connection = (Connection *) le->data;
		if (connection->lobby == current) connection->lobby = lobby;
	}

}

// registers a player to the lobby --> add him to lobby's structures
u8 player_register_to_lobby (Lobby *lobby, Player *player) {

	u8 retval = 1;

	if (lobby && player && player->client) {
		if (!dlist_insert_at_end (lobby->players, player)) {
			lobby->n_current_players++;

			player_activity_update (player);
			player_connections_set_lobby (player, NULL, lobby);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_PLAYER,
				"Registered a new player to lobby %s, it has %u players.",
				lobby->id->str, lobby->n_current_players
			);
			#endif

			retval = 0;
		}
	}

	return retval;

}

// unregisters each of the player's client connections from the lobby poll structures
u8 player_unregister_from_lobby_poll (Lobby *lobby, Player *player) {

	u8 errors = 0;

	if (lobby && player && player->client) {
		for (ListElement *le = dlist_start (player->client->connections); le; le = le->next) {
			errors |= lobby_poll_unregister_connection (lobby, player, (Connection *) le->data);
		}
	}

	else {
		errors = 1;
	}

	return errors;

}

// the next player is the owner, NULL in an empty lobby
static void player_owner_replace (Lobby *lobby) {

	ListElement *start = dlist_start (lobby->players);
	lobby->owner = start ? (Player *) start->data : NULL;

	#ifdef CERVER_DEBUG
	if (lobby->owner) {
		cerver_log (
			LOG_TYPE_DEBUG, LOG_TYPE_PLAYER,
			"Client %lu is the new owner of lobby %s.",
			lobby->owner->client->id, lobby->id->str
		);
	}
	#endif

}

// unregisters a player from a lobby --> removes him from lobby's structures
u8 player_unregister_from_lobby (Lobby *lobby, Player *player) {

	u8 retval = 1;

	if (lobby && player && player->client) {
		ListElement *element = dlist_get_element (lobby->players, player, NULL);
		if (element && (element->data == player)) {
			if (lobby->poll_connections) {
				(void) player_unregister_from_lobby_poll (lobby, player);
			}

			player_connections_set_lobby (player, lobby, NULL);

			(void) dlist_remove_element (lobby->players, element);
			lobby->n_current_players--;

			if (lobby->owner == player) player_owner_replace (lobby);

			#ifdef CERVER_DEBUG
			cerver_log (
				LOG_TYPE_SUCCESS, LOG_TYPE_PLAYER,
				"Unregistered a player from lobby %s, it has %u players.",
				lobby->id->str, lobby->n_current_players
			);
			#endif

			retval = 0;
		}
	}

	return retval;

}

// gets a player from the lobby using the query
Player *player_get_from_lobby (Lobby *lobby, Player *query) {

	return (lobby ? (Player *) dlist_search (lobby->players, query, NULL) : NULL);

}

// get sthe list element associated with the player
ListElement *player_get_le_from_lobby (Lobby *lobby, Player *player) {

	return (lobby ? dlist_get_element (lobby->players, player, NULL) : NULL);

}

// gets a player from the lobby player's list suing the sock fd
Player *player_get_with_sock_fd_list (Lobby *lobby, i32 sock_fd) {

	if (lobby) {
		Player *player = NULL;
		Connection *connection = NULL;
		for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
			player = (Player *) le->data;
			for (ListElement *le_sub = dlist_start (player->client->connections); le_sub; le_sub = le_sub->next) {
				connection = (Connection *) le_sub->data;
				if (connection->socket->sock_fd == sock_fd) return player;
			}
		}
	}

	return NULL;

}

// broadcasts a packet to all the players in the lobby
void player_broadcast_to_all (
	Cerver *cerver, const Lobby *lobby, Packet *packet,
	Protocol protocol, int flags
) {

	// the protocol is taken from each player's connection
	(void) protocol;

	if (lobby && packet) {
		game_cerver_lock (lobby->cerver);

		for (ListElement *le = dlist_start (lobby->players); le; le = le->next) {
			Player *player = (Player *) le->data;

			for (ListElement *le_sub = dlist_start (player->client->connections); le_sub; le_sub = le_sub->next) {
				Connection *connection = (Connection *) le_sub->data;
				packet_set_network_values (packet, cerver, player->client, connection, (Lobby *) lobby);
				(void) packet_send (packet, flags, NULL, false);
			}
		}

		game_cerver_unlock (lobby->cerver);
	}

}
