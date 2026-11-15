#ifndef _CERVER_CLIENT_INTERNAL_H_
#define _CERVER_CLIENT_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/files/send.h"
#include "cerver/handler/receive.h"
#include "cerver/packets/packets.h"

#ifdef __cplusplus
extern "C" {
#endif

// the methods that the parts of the client share, never exported

CERVER_HIDDEN ClientConnection *client_connection_aux_new (
	Client *client, Connection *connection
);

CERVER_HIDDEN u8 client_start (Client *client);

CERVER_HIDDEN void client_event_delete (void *ptr);

CERVER_HIDDEN void client_error_delete (void *client_error_ptr);

CERVER_HIDDEN void client_error_packet_handler (Packet *packet);

CERVER_HIDDEN void client_async_thread_count (Client *client, const int delta);

CERVER_HIDDEN unsigned int client_async_thread_create (
	ClientConnection *cc, void *(*work) (void *)
);

CERVER_HIDDEN bool client_connection_end_request (Connection *connection);

CERVER_HIDDEN u8 client_file_receive (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

CERVER_HIDDEN u8 client_packet_handler (Packet *packet);

CERVER_HIDDEN void client_receive_handle_buffer (ReceiveHandle *receive_handle);

CERVER_HIDDEN ReceiveError client_receive_actual (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t *rc
);

CERVER_HIDDEN u8 client_receive_datagram (
	Client *client, Connection *connection,
	const char *datagram, const size_t size
);

// request to read x amount of bytes from the connection's sock fd
// into the specified buffer
// this method will only return once the requested bytes
// have been received or on any error
CERVER_HIDDEN ReceiveError client_receive_data (
	Client *client, Connection *connection,
	char *buffer, const size_t buffer_size,
	size_t requested_data
);

#ifdef __cplusplus
}
#endif

#endif
