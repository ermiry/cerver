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

// the methods that the parts of the files share

CERVER_HIDDEN ssize_t file_send_actual (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

CERVER_HIDDEN int file_send_open (
	const char *filename,
	struct stat *filestatus,
	const char **actual_filename
);

CERVER_HIDDEN u8 file_cerver_receive (
	Cerver *cerver, Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

// it was exported before the files were split, it stays exported
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
