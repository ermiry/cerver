/**
 * @file cerver/client/requests.h
 * @brief The requests of a client to a cerver, and its files.
 */

#ifndef _CERVER_CLIENT_REQUESTS_H_
#define _CERVER_CLIENT_REQUESTS_H_

#include "cerver/config.h"

#include "cerver/client/client.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup client_requests Requests
 * @ingroup client
 * @brief The requests that a connected client makes to a cerver, and the files that it gets and sends.
 * @{
 */

/**
 * @brief Sends a request to the cerver and handles its response with the packet handlers of the client.
 *
 * It blocks until a complete response arrives; it only works with a response of one packet. Neither the client
 * nor the connection are stopped after the request, and the request packet is not deleted.
 *
 * @param[in,out] client the client, already connected
 * @param[in,out] connection the connection
 * @param[in,out] request the request, its network values are set; the caller keeps it
 * @retval 0 the request was sent and its response was handled
 * @retval 1 a NULL argument, the request could not be sent, or its response could not be received
 */
CERVER_EXPORT unsigned int client_request_to_cerver (
	Client *client, struct _Connection *connection,
	struct _Packet *request
);

/**
 * @brief Sends a request to the cerver and receives its response in a new detachable thread, without blocking.
 *
 * It only works with a response of one packet; neither the client nor the connection are stopped after the
 * request, and the request packet is not deleted. CLIENT_EVENT_CONNECTION_DATA is triggered when the response was
 * handled, or wait for it with client_requests_wait ().
 *
 * @param[in,out] client the client, already connected
 * @param[in,out] connection the connection
 * @param[in,out] request the request, its network values are set; the caller keeps it
 * @retval 0 the request was sent and the thread was created
 * @retval 1 a NULL argument, the request could not be sent, or the thread could not be created
 */
CERVER_EXPORT unsigned int client_request_to_cerver_async (
	Client *client, struct _Connection *connection,
	struct _Packet *request
);

/**
 * @brief Waits until every async request and connect of the client has ended.
 *
 * @param[in] client the client
 * @warning It can not be called from the handlers of the client, they run in those threads.
 */
CERVER_EXPORT void client_requests_wait (Client *client);

/**
 * @brief Adds a path where the client searches the files that a cerver requests.
 *
 * @param[in,out] client the client
 * @param[in] path the path, copied
 * @retval 0 the path was added
 * @retval 1 a NULL argument or the client already has #CLIENT_FILES_MAX_PATHS
 */
CERVER_EXPORT u8 client_files_add_path (
	Client *client, const char *path
);

/**
 * @brief Sets the path where the received files are placed; without it, the client rejects the files that a
 * cerver sends.
 *
 * @param[in,out] client the client
 * @param[in] uploads_path the path, copied; NULL keeps the current one
 */
CERVER_EXPORT void client_files_set_uploads_path (
	Client *client, const char *uploads_path
);

/**
 * @brief Sets a custom method that handles a file that a cerver sends (an upload to the client).
 *
 * The method consumes the contents of the file from the socket, after the ones that came with the packet
 * (file_data), and returns 0 on success and 1 on error; by default the client saves the file in its uploads path.
 *
 * @param[in,out] client the client
 * @param[in] file_upload_handler the method, it can set saved_filename to a string allocated with malloc (), that
 * the client releases
 */
CERVER_EXPORT void client_files_set_file_upload_handler (
	Client *client,
	u8 (*file_upload_handler) (
		struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
);

/**
 * @brief Sets a callback that runs after a file was received.
 *
 * @param[in,out] client the client
 * @param[in] file_upload_cb the callback, it gets the saved filename
 */
CERVER_EXPORT void client_files_set_file_upload_cb (
	Client *client,
	void (*file_upload_cb) (
		struct _Client *, struct _Connection *,
		const char *saved_filename
	)
);

/**
 * @brief Searches a file in the paths of the client, in the order they were added.
 *
 * @param[in] client the client
 * @param[in] filename the name of the file
 * @return the path and the name where it was found, a new String; NULL if it was not found or for a NULL argument
 */
CERVER_PUBLIC String *client_files_search_file (
	Client *client, const char *filename
);

/**
 * @brief Requests a file from the cerver.
 *
 * @param[in,out] client the client, with its uploads path (client_files_set_uploads_path ())
 * @param[in,out] connection the connection
 * @param[in] filename the name of the file
 * @retval 0 the request was sent
 * @retval 1 a NULL argument, a client without its uploads path, or the request could not be sent
 */
CERVER_EXPORT u8 client_file_get (
	Client *client, struct _Connection *connection,
	const char *filename
);

/**
 * @brief Sends a file to the cerver.
 *
 * @param[in,out] client the client
 * @param[in,out] connection the connection
 * @param[in] filename the path of the file; the cerver gets the name after its last `/`, or the whole filename
 * when it has none (a file of the current directory)
 * @retval 0 the file was sent completely
 * @retval 1 a NULL argument, a filename that ends with `/`, the file could not be opened, or it was not sent
 * completely
 */
CERVER_EXPORT u8 client_file_send (
	Client *client, struct _Connection *connection,
	const char *filename
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
