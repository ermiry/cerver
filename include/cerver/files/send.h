#ifndef _CERVER_FILES_SEND_H_
#define _CERVER_FILES_SEND_H_

#include "cerver/config.h"

#include "cerver/files/files.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region send

#define FILE_HEADER_VERSION				1

// the name and the length of a file that is sent or requested,
// the contents of the file are sent after its packet
#define FILE_HEADER_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, filename, FILENAME_DEFAULT_SIZE, SERIALIZER_REQUIRED)		\
	SCALAR(T, 2, U64, len, SERIALIZER_OPTIONAL, 0)

SERIALIZER_DECLARE (FileHeader, file_header, FILE_HEADER_FIELDS)

// a FileHeader that comes from a packet is used only if its filename is NUL terminated,
// not empty, a single name (without '/') and different from "." and "..",
// so it can not read past the header or reach a file outside of the configured paths
CERVER_PUBLIC bool file_header_is_valid (const FileHeader *file_header);

// decodes the FileHeader of a packet and validates it
// returns true if the packet has a valid header
CERVER_PUBLIC bool file_header_get (
	const struct _Packet *packet, FileHeader *file_header
);

// creates a request packet ready to be sent with the FileHeader of the file
// returns NULL on error
CERVER_PRIVATE struct _Packet *file_header_packet_create (
	const u32 request_type, const char *filename, const u64 len
);

// opens a file and sends its contents
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
CERVER_PUBLIC ssize_t file_send (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	const char *filename
);

// sends the file contents of the file referenced by a fd
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
CERVER_PUBLIC ssize_t file_send_with_fd (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
