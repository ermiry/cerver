#ifndef _CERVER_CLIENT_REQUESTS_H_
#define _CERVER_CLIENT_REQUESTS_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region requests

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this is a blocking method, as it will wait until a complete cerver response has been received
// the response will be handled using the client's packet handler
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// retruns 0 when the response has been handled, 1 on error
CERVER_EXPORT unsigned int client_request_to_cerver (
	Client *client, struct _Connection *connection,
	struct _Packet *request
);

// when a client is already connected to the cerver, a request can be made to the cerver
// the response will be handled by the client's handlers
// this method will NOT block
// this method only works if your response consists only of one packet
// neither client nor the connection will be stopped after the request has ended, the request packet won't be deleted
// CLIENT_EVENT_CONNECTION_DATA is triggered when the response was handled,
// or wait for it with client_requests_wait ()
// returns 0 on success request, 1 on error
CERVER_EXPORT unsigned int client_request_to_cerver_async (
	Client *client, struct _Connection *connection,
	struct _Packet *request
);

// waits until every async request and connect of the client has ended
// it can not be called from the client's handlers, they run in those threads
CERVER_EXPORT void client_requests_wait (Client *client);

/*** files ***/

// adds a new file path to take into account when getting a request for a file
// returns 0 on success, 1 on error
CERVER_EXPORT u8 client_files_add_path (
	Client *client, const char *path
);

// sets the default uploads path to be used when receiving a file
CERVER_EXPORT void client_files_set_uploads_path (
	Client *client, const char *uploads_path
);

// sets a custom method to be used to handle a file upload (receive)
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
CERVER_EXPORT void client_files_set_file_upload_handler (
	Client *client,
	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
);

// sets a callback to be executed after a file has been successfully received
CERVER_EXPORT void client_files_set_file_upload_cb (
	Client *client,
	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	)
);

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
CERVER_PUBLIC String *client_files_search_file (
	Client *client, const char *filename
);

// requests a file from the cerver
// the client's uploads_path should have been configured before calling this method
// returns 0 on success sending request, 1 on failed to send request
CERVER_EXPORT u8 client_file_get (
	Client *client, struct _Connection *connection,
	const char *filename
);

// sends a file to the cerver
// returns 0 on success sending request, 1 on failed to send request
CERVER_EXPORT u8 client_file_send (
	Client *client, struct _Connection *connection,
	const char *filename
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
