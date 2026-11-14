#ifndef _CERVER_FILES_RECEIVE_H_
#define _CERVER_FILES_RECEIVE_H_

#include "cerver/config.h"

#include "cerver/files/files.h"
#include "cerver/files/send.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region receive

// opens the file using an already created filename
// and use the fd to receive and save the file
CERVER_PRIVATE u8 file_receive_actual (
	struct _Client *client, struct _Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
