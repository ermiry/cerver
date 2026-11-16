#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"
#include "cerver/collections/htab.h"

#include "cerver/admin/admin.h"
#include "cerver/admin/cerver.h"
#include "cerver/auth/auth.h"
#include "cerver/auth/sessions.h"
#include "cerver/client/client.h"
#include "cerver/connection/internal.h"
#include "cerver/connection/registry.h"
#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"
#include "cerver/handler/receive.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/bsem.h"
#include "cerver/threads/jobs.h"
#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

#pragma region registry

// gets the connection from the on hold connections map in cerver
Connection *connection_get_with_sock_fd_from_on_hold (
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
Connection *connection_get_with_sock_fd_from_client (
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
Connection *connection_get_with_sock_fd_from_admin (
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
		connection->client = client;

		if (!dlist_insert_at_end (client->connections, connection)) {
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
					"Registered a new connection to client (id): %lu",
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
