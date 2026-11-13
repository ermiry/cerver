#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/packets.h>

#include <cerver/game/game.h>
#include <cerver/game/gametype.h>
#include <cerver/game/lobby.h>

#include "../test.h"

// the game cerver of the cerver/game pair: the lobbies of the arcade game have two players,
// and while a lobby runs its packet handler sends every app packet back to its player

#define GAME_PAIR_NAME					"game-cerver"
#define GAME_PAIR_TYPE					"arcade"
#define GAME_PAIR_MAX_PLAYERS			2
#define GAME_PAIR_BUFFER_SIZE			4096
#define GAME_PAIR_THREADS				4
#define GAME_PAIR_POLL_TIMEOUT			1000

static Cerver *cerver = NULL;

// the cerver is released in main () when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	(void) cerver_shutdown (cerver);

}

// the lobby sends the packet back to the player that sent it
static void game_pair_lobby_packet_handler (void *packet_ptr) {

	const Packet *packet = (const Packet *) packet_ptr;

	Packet *echo = packet_create_request (PACKET_TYPE_APP, packet->header.request_type);
	if (echo) {
		packet_set_network_values (echo, packet->cerver, packet->client, packet->connection, NULL);
		(void) packet_send (echo, 0, NULL, false);
		packet_delete (echo);
	}

}

static void *game_pair_start (void *lobby_ptr) {

	lobby_set_packet_handler ((Lobby *) lobby_ptr, game_pair_lobby_packet_handler);

	return NULL;

}

int main (void) {

	srand ((unsigned int) time (NULL));

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);

	cerver = cerver_create (
		CERVER_TYPE_GAME, GAME_PAIR_NAME,
		CERVER_DEFAULT_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	test_check_ptr (cerver);
	test_check_ptr (cerver->cerver_data);

	test_check_unsigned_eq (cerver_set_bind_address (cerver, test_address ()), 0, NULL);

	cerver_set_receive_buffer_size (cerver, GAME_PAIR_BUFFER_SIZE);
	cerver_set_thpool_n_threads (cerver, GAME_PAIR_THREADS);
	cerver_set_reusable_address_flags (cerver, true);
	cerver_set_handler_type (cerver, CERVER_HANDLER_TYPE_POLL);
	cerver_set_poll_time_out (cerver, GAME_PAIR_POLL_TIMEOUT);

	GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;

	GameType *game_type = game_type_create (GAME_PAIR_TYPE, NULL, NULL, game_pair_start, NULL);
	test_check_ptr (game_type);
	test_check_int_eq (game_type_add_lobby_config (game_type, true, NULL, GAME_PAIR_MAX_PLAYERS), 0, NULL);
	test_check_int_eq (game_type_register (game_cerver->game_types, game_type), 0, NULL);

	test_check_unsigned_eq (cerver_start (cerver), 0, "Failed to start cerver!");

	test_check_unsigned_eq (cerver_teardown (cerver), 0, NULL);

	return 0;

}
