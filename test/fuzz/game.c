#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerver/types/string.h>

#include <cerver/client/client.h>
#include <cerver/connection/connection.h>
#include <cerver/connection/registry.h>
#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/packets/packets.h>

#include <cerver/game/game.h>
#include <cerver/game/lobby.h>
#include <cerver/game/types.h>

// the game requests of a client to a game cerver that is not started:
// the input is a sequence of requests, each one a byte with its request type,
// a byte with the size of its object, and the object, that the game cerver handles in order
// with lobbies that it creates, joins, starts, leaves and destroys, and the answers go to a socket pair

#define FUZZ_GAME_NAME					"fuzz-game"
#define FUZZ_GAME_TYPE					"arcade"
#define FUZZ_GAME_MAX_PLAYERS			2
#define FUZZ_GAME_PORT					7000
#define FUZZ_GAME_LOBBY_ID				"fuzz-lobby"

#define FUZZ_GAME_REQUEST_HEADER_SIZE	2
#define FUZZ_GAME_REQUEST_TYPES			9
#define FUZZ_GAME_MAX_REQUESTS			64
#define FUZZ_GAME_DRAIN_SIZE			4096

static void fuzz_game_drain (const int sock_fd) {

	char buffer[FUZZ_GAME_DRAIN_SIZE];
	while (recv (sock_fd, buffer, FUZZ_GAME_DRAIN_SIZE, MSG_DONTWAIT) > 0) {}

}

static void fuzz_game_request (
	Cerver *cerver, Client *client, Connection *connection,
	const u32 request_type, const uint8_t *object, const size_t size
) {

	Packet *packet = packet_new ();
	if (packet) {
		packet->header.packet_type = PACKET_TYPE_GAME;
		packet->header.request_type = request_type;
		packet->header.flags = PACKET_FLAG_SERIALIZED;

		packet->data = malloc (size ? size : 1);
		if (packet->data) {
			(void) memcpy (packet->data, object, size);
			packet->data_size = size;

			packet_set_network_values (packet, cerver, client, connection, NULL);

			game_packet_handler (packet);
		}

		packet_delete (packet);
	}

}

static unsigned int fuzz_game_lobbies = 0;

// the ids are known, so the requests of an input can name the lobbies that it created:
// fuzz-lobby-1 is the first one of each input
static void *fuzz_game_lobby_id (const void *data) {

	(void) data;

	char id[LOBBY_ID_SIZE] = { 0 };
	(void) snprintf (id, LOBBY_ID_SIZE, "%s-%u", FUZZ_GAME_LOBBY_ID, ++fuzz_game_lobbies);

	return str_new (id);

}

static Cerver *fuzz_game_cerver_create (void) {

	fuzz_game_lobbies = 0;

	Cerver *cerver = cerver_create (
		CERVER_TYPE_GAME, FUZZ_GAME_NAME, FUZZ_GAME_PORT, PROTOCOL_TCP, false, CERVER_DEFAULT_CONNECTION_QUEUE
	);

	if (cerver) {
		GameCerver *game_cerver = (GameCerver *) cerver->cerver_data;
		game_set_lobby_id_generator (game_cerver, fuzz_game_lobby_id);

		GameType *game_type = game_type_create (FUZZ_GAME_TYPE, NULL, NULL, NULL, NULL);
		if (game_type) {
			(void) game_type_add_lobby_config (game_type, true, NULL, FUZZ_GAME_MAX_PLAYERS);
			(void) game_type_register (game_cerver->game_types, game_type);
		}
	}

	return cerver;

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	int sock_fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, sock_fds)) return 0;

	// a full socket never blocks the answers, they are dropped
	(void) fcntl (sock_fds[0], F_SETFL, O_NONBLOCK);

	Cerver *cerver = fuzz_game_cerver_create ();
	Client *client = client_create ();

	if (cerver && client) {
		struct sockaddr_storage address;
		(void) memset (&address, 0, sizeof (struct sockaddr_storage));
		address.ss_family = AF_INET;

		Connection *connection = connection_create (sock_fds[0], &address, PROTOCOL_TCP);
		if (connection) {
			connection->active = true;
			(void) connection_register_to_client (client, connection);

			size_t offset = 0;
			unsigned int requests = 0;
			while (
				((offset + FUZZ_GAME_REQUEST_HEADER_SIZE) <= size)
				&& (requests < FUZZ_GAME_MAX_REQUESTS)
			) {
				const u32 request_type = data[offset] % FUZZ_GAME_REQUEST_TYPES;
				size_t object_size = data[offset + 1];
				offset += FUZZ_GAME_REQUEST_HEADER_SIZE;

				if ((offset + object_size) > size) object_size = size - offset;

				fuzz_game_request (cerver, client, connection, request_type, data + offset, object_size);

				offset += object_size;
				requests++;

				fuzz_game_drain (sock_fds[1]);
			}

			// the player leaves every lobby before its client is released
			game_connection_end (cerver, client, connection);
		}
	}

	if (cerver) (void) cerver_teardown (cerver);
	client_delete (client);

	(void) close (sock_fds[1]);

	return 0;

}
