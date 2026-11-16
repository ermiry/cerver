/**
 * @file cerver/files/send.h
 * @brief The header of a file that is sent or requested, and how a file is sent.
 */

#ifndef _CERVER_FILES_SEND_H_
#define _CERVER_FILES_SEND_H_

#include "cerver/config.h"

#include "cerver/files/files.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup files_send Send
 * @ingroup files
 * @brief The FileHeader and sending a file.
 * @{
 */

/** @brief The version of the schema of the FileHeader. */
#define FILE_HEADER_VERSION				1

/**
 * @brief The X-macro table of the fields of the FileHeader: the name and the length of a file that is sent or
 * requested; the contents of the file are sent after its packet.
 *
 * SERIALIZER_DECLARE () makes the struct FileHeader and the schema file_header_schema from it.
 */
#define FILE_HEADER_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, filename, FILENAME_DEFAULT_SIZE, SERIALIZER_REQUIRED)		\
	SCALAR(T, 2, U64, len, SERIALIZER_OPTIONAL, 0)

SERIALIZER_DECLARE (FileHeader, file_header, FILE_HEADER_FIELDS)

/**
 * @brief Returns whether a FileHeader that comes from a packet can be used.
 *
 * Its filename must be NUL terminated, not empty, a single name (without '/') and different from "." and "..", so
 * it can not read past the header or reach a file outside of the configured paths.
 *
 * @param[in] file_header the header
 * @return true for a valid header, false for a NULL one
 */
CERVER_PUBLIC bool file_header_is_valid (const FileHeader *file_header);

/**
 * @brief Decodes the FileHeader of a packet and validates it with file_header_is_valid ().
 *
 * @param[in] packet the packet
 * @param[out] file_header where the header is copied, only when it is valid
 * @return true if the packet has a valid header, false for a NULL argument
 */
CERVER_PUBLIC bool file_header_get (
	const struct _Packet *packet, FileHeader *file_header
);

/**
 * @brief Creates a request packet with the FileHeader of a file, ready to be sent.
 *
 * @param[in] request_type the request type of the packet
 * @param[in] filename the name of the file, shorter than #FILENAME_DEFAULT_SIZE
 * @param[in] len the length of the file
 * @return the new packet, NULL for a NULL or too long filename or on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE struct _Packet *file_header_packet_create (
	const u32 request_type, const char *filename, const u64 len
);

/**
 * @brief Opens a file and sends it to a connection: first its FileHeader in a packet, then its contents.
 *
 * @param[in] cerver the cerver
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @param[in] filename the path of the file
 * @return the bytes of the file that were sent, -1 for a NULL filename or connection or on error
 * @note The name that is sent is the one after the last '/', or the whole filename when it has none; a filename that
 * ends with '/' has no name, and it returns -1.
 * @threadsafety It holds the write lock of the socket of the connection while it sends.
 */
CERVER_PUBLIC ssize_t file_send (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	const char *filename
);

/**
 * @brief Sends the contents of an open file to a connection: first its FileHeader in a packet, then its contents.
 *
 * @param[in] cerver the cerver
 * @param[in] client the client of the connection
 * @param[in] connection the connection
 * @param[in] file_fd the fd of the file, the caller closes it
 * @param[in] actual_filename the name of the file in its FileHeader
 * @param[in] filelen the bytes of the file
 * @return the bytes of the file that were sent, -1 for a NULL client, connection or name, or on error
 * @threadsafety It holds the write lock of the socket of the connection while it sends.
 */
CERVER_PUBLIC ssize_t file_send_with_fd (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
