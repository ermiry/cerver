#ifndef _CERVER_CONNECTION_REGISTRY_H_
#define _CERVER_CONNECTION_REGISTRY_H_

#include "cerver/config.h"

#include "cerver/connection/connection.h"

#ifdef __cplusplus
extern "C" {
#endif

// gets the connection from the on hold connections map in cerver
CERVER_PRIVATE Connection *connection_get_with_sock_fd_from_on_hold (
	struct _Cerver *cerver, const i32 sock_fd
);

// gets the connection from the client by its sock fd
CERVER_PRIVATE Connection *connection_get_with_sock_fd_from_client (
	struct _Client *client, const i32 sock_fd
);

// gets the connection from the admin cerver by its sock fd
CERVER_PRIVATE Connection *connection_get_with_sock_fd_from_admin (
	struct _AdminCerver *admin_cerver, const i32 sock_fd
);

// checks if the connection belongs to the client
CERVER_PRIVATE bool connection_check_owner (
	struct _Client *client, Connection *connection
);

// registers a new connection to a client without adding it to the cerver poll
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_register_to_client (
	struct _Client *client, Connection *connection
);

// registers the client connection to the cerver's strcutures (like maps)
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_register_to_cerver (
	struct _Cerver *cerver,
	struct _Client *client, Connection *connection
);

// unregister the client connection from the cerver's structures (like maps)
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_unregister_from_cerver (
	struct _Cerver *cerver, Connection *connection
);

// wrapper function for easy access
// registers a client connection to the cerver poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_register_to_cerver_poll (
	struct _Cerver *cerver, Connection *connection
);

// wrapper function for easy access
// unregisters a client connection from the cerver poll array
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_unregister_from_cerver_poll (
	struct _Cerver *cerver, Connection *connection
);

// first adds the client connection to the cerver's poll array, and upon success,
// adds the connection to the cerver's structures
// this method is equivalent to call connection_register_to_cerver_poll () & connection_register_to_cerver
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_add_to_cerver (
	struct _Cerver *cerver,
	struct _Client *client,
	Connection *connection
);

// removes the connection's sock fd from the cerver's poll array and then removes the connection
// from the cerver's structures
// this method is equivalent to call connection_unregister_from_cerver_poll () & connection_unregister_from_cerver ()
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 connection_remove_from_cerver (
	struct _Cerver *cerver, Connection *connection
);

#ifdef __cplusplus
}
#endif

#endif
