#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <sys/socket.h>
#include <unistd.h>

#include <cerver/collections/dlist.h>

#include <cerver/client/client.h>
#include <cerver/client/connect.h>
#include <cerver/connection/connection.h>
#include <cerver/connection/registry.h>
#include <cerver/network/network.h>

// the stream that a cerver sends to a client, read with client_connection_get_next_packet ():
// it is written to a socket pair and every packet is read until the stream ends,
// a packet can end its own connection (a close request), so it is not read after it;
// what the client answers is read from the other end after every piece of the stream

#define FUZZ_CLIENT_MAX_PACKET_SIZE		(1 << 16)
#define FUZZ_CLIENT_MAX_PACKETS			(1 << 10)
#define FUZZ_CLIENT_DRAIN_SIZE			4096

// the seconds that an answer waits for space in a full socket, instead of the ones of the default
#define FUZZ_CLIENT_SEND_TIMEOUT		1

// the answers of the client are read and dropped: when nobody reads them the socket fills up,
// and every answer after that waits for the send timeout of its connection
static void fuzz_client_drain (const int sock_fd) {

	char buffer[FUZZ_CLIENT_DRAIN_SIZE];
	while (recv (sock_fd, buffer, FUZZ_CLIENT_DRAIN_SIZE, MSG_DONTWAIT) > 0) {}

}

static bool fuzz_client_has_connection (Client *client, const Connection *connection) {

	bool found = false;

	for (ListElement *le = dlist_start (client->connections); le; le = le->next) {
		if (le->data == connection) {
			found = true;
			break;
		}
	}

	return found;

}

static void fuzz_client_write (const int sock_fd, const uint8_t *data, const size_t size) {

	size_t written = 0;
	while (written < size) {
		const ssize_t result = send (sock_fd, data + written, size - written, MSG_NOSIGNAL);
		if (result <= 0) break;

		written += (size_t) result;
	}

	(void) shutdown (sock_fd, SHUT_WR);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	int sock_fds[2] = { -1, -1 };
	if (socketpair (AF_UNIX, SOCK_STREAM, 0, sock_fds)) return 0;

	// the fuzzer limits the input to less than the buffer of the socket
	fuzz_client_write (sock_fds[1], data, size);

	Client *client = client_create ();
	if (client) {
		client_set_max_received_packet_size (client, FUZZ_CLIENT_MAX_PACKET_SIZE);

		struct sockaddr_storage address;
		(void) memset (&address, 0, sizeof (struct sockaddr_storage));
		address.ss_family = AF_INET;

		Connection *connection = connection_create (sock_fds[0], &address, PROTOCOL_TCP);
		if (connection) {
			// the mutex and the cond of a connection of a client, the end uses them
			connection_set_synchronization (connection);

			// like after client_connect (), so its end closes the socket
			connection->active = true;

			connection_set_send_timeout (connection, FUZZ_CLIENT_SEND_TIMEOUT);

			(void) connection_register_to_client (client, connection);

			unsigned int packets = 0;
			while (
				(packets < FUZZ_CLIENT_MAX_PACKETS)
				&& fuzz_client_has_connection (client, connection)
				&& !client_connection_get_next_packet (client, connection)
			) {
				packets += 1;

				fuzz_client_drain (sock_fds[1]);
			}
		}

		else {
			(void) close (sock_fds[0]);
		}

		// it ends the connection that is still registered, and closes its socket
		(void) client_teardown (client);
	}

	else {
		(void) close (sock_fds[0]);
	}

	(void) close (sock_fds[1]);

	return 0;

}
