/**
 * @file files/internal.h
 * @internal
 * @brief The methods that the parts of the files share.
 */

#ifndef _CERVER_FILES_INTERNAL_H_
#define _CERVER_FILES_INTERNAL_H_

#include <stddef.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/connection/connection.h"
#include "cerver/core/cerver.h"
#include "cerver/files/files.h"
#include "cerver/files/receive.h"
#include "cerver/files/send.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @internal
 * @brief Sends the FileHeader and then the contents of an open file, with the write lock of the socket.
 *
 * @param[in] cerver the cerver
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @param[in] file_fd the fd of the file
 * @param[in] actual_filename the name of the file in its FileHeader
 * @param[in] filelen the bytes of the file
 * @return the bytes of the file that were sent, -1 on error
 */
CERVER_HIDDEN ssize_t file_send_actual (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

/**
 * @internal
 * @brief Opens a file to send it, and finds its name after the last '/'.
 *
 * @param[in] filename the path of the file
 * @param[out] filestatus the status of the file
 * @param[out] actual_filename the name after the last '/', or the whole filename when it has none
 * @return the fd, -1 on error or for a filename that ends with '/' (it has no name)
 */
CERVER_HIDDEN int file_send_open (
	const char *filename,
	struct stat *filestatus,
	const char **actual_filename
);

/**
 * @internal
 * @brief The default upload handler: saves the file in the uploads path as "<time>-<random>-<filename>".
 *
 * @param[in] cerver the files cerver
 * @param[in] client the client
 * @param[in] connection the connection
 * @param[in] file_header the header of the file
 * @param[in] file_data the first bytes of the file that came with its packet
 * @param[in] file_data_len the bytes of file_data
 * @param[out] saved_filename the path where the file was saved, allocated with malloc ()
 * @retval 0 the file was saved
 * @retval 1 it could not be saved, or the files cerver has no uploads path: then the client gets a
 * `CERVER_ERROR_SEND_FILE` and the connection is ended (the bytes of the file follow its packet)
 */
CERVER_HIDDEN u8 file_cerver_receive (
	Cerver *cerver, Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

/**
 * @internal
 * @brief See the public declaration in cerver/files/receive.h; it was exported before the files were split, so it
 * stays exported.
 *
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @param[in] file_header the header of the file
 * @param[in] file_data the first bytes of the file
 * @param[in] file_data_len the bytes of file_data
 * @param[in,out] saved_filename the path where the file is saved
 * @return 0 when the whole file was saved, 1 on error
 */
u8 file_receive_actual (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

#ifdef __cplusplus
}
#endif

#endif
