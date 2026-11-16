/**
 * @file cerver/files/receive.h
 * @brief How a file that is sent after its packet is received and saved.
 */

#ifndef _CERVER_FILES_RECEIVE_H_
#define _CERVER_FILES_RECEIVE_H_

#include "cerver/config.h"

#include "cerver/files/files.h"
#include "cerver/files/send.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_receive Receive
 * @ingroup files
 * @brief Receiving a file.
 * @{
 */

/**
 * @brief Saves a file in a path that was already created: first the bytes that came with its packet, then the
 * rest of the file from the connection.
 *
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @param[in] file_header the header of the file, with its length
 * @param[in] file_data the first bytes of the file that came in the buffer of the packet, it can be NULL
 * @param[in] file_data_len the bytes of file_data, the ones beyond the length of the file are not used
 * @param[in,out] saved_filename the path where the file is saved, allocated with malloc (); on error it is
 * released and set to NULL
 * @retval 0 the whole file was saved
 * @retval 1 the file could not be opened, written or received; a file that was opened is removed, so no partial
 * file is left in its path
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 file_receive_actual (
	struct _Client *client, struct _Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
