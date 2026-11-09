#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <unistd.h>

#include <sys/socket.h>

#include <cerver/client.h>
#include <cerver/connection.h>
#include <cerver/network.h>

#include <cerver/collections/dlist.h>

// the stream that a cerver sends to a client, read with client_connection_get_next_packet ():
// it is written to a socket pair and every packet is read until the stream ends,
// a packet can end its own connection (a close request), so it is not read after it

#define FUZZ_CLIENT_MAX_PACKET_SIZE		(1 << 16)
#define FUZZ_CLIENT_MAX_PACKETS			(1 << 10)

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

			(void) connection_register_to_client (client, connection);

			unsigned int packets = 0;
			while (
				(packets < FUZZ_CLIENT_MAX_PACKETS)
				&& fuzz_client_has_connection (client, connection)
				&& !client_connection_get_next_packet (client, connection)
			) {
				packets += 1;
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
