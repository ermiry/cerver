#ifndef _CERVER_FILES_CERVER_H_
#define _CERVER_FILES_CERVER_H_

#include "cerver/config.h"

#include "cerver/files/files.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region cerver

#define FILE_CERVER_MAX_PATHS           32

typedef struct FileCerverStats {

	u64 n_files_requests;				// n requests to get a file
	u64 n_success_files_requests;		// fulfilled requests
	u64 n_bad_files_requests;			// bad requests
	u64 n_files_sent;					// n files sent
	u64 n_bad_files_sent;				// n files that failed to send
	u64 n_bytes_sent;					// total bytes sent

	u64 n_files_upload_requests;		// n requests to upload a file
	u64 n_success_files_uploaded;		// n files received
	u64 n_bad_files_upload_requests;	// bad requests to upload files
	u64 n_bad_files_received;			// files that failed to be received
	u64 n_bytes_received;				// total bytes received

} FileCerverStats;

struct _FileCerver {

	struct _Cerver *cerver;

	// search for requested files in these paths
	unsigned int n_paths;
	String *paths[FILE_CERVER_MAX_PATHS];

	// default path where uploads files will be placed
	String *uploads_path;

	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	);

	void (*file_upload_cb) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		const char *saved_filename
	);
	
	FileCerverStats *stats;

};

typedef struct _FileCerver FileCerver;

CERVER_PRIVATE FileCerver *file_cerver_new (void);

CERVER_PRIVATE void file_cerver_delete (void *file_cerver_ptr);

CERVER_PRIVATE FileCerver *file_cerver_create (struct _Cerver *cerver);

// adds a new file path to take into account when a client request for a file
// returns 0 on success, 1 on error
CERVER_EXPORT u8 file_cerver_add_path (
	FileCerver *file_cerver, const char *path
);

// sets the default uploads path to be used when a client sends a file
CERVER_EXPORT void file_cerver_set_uploads_path (
	FileCerver *file_cerver, const char *uploads_path
);

// sets a custom method to be used to handle a file upload
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
CERVER_EXPORT void file_cerver_set_file_upload_handler (
	FileCerver *file_cerver,
	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
);

// sets a callback to be executed after a file has been successfully uploaded by a client
CERVER_EXPORT void file_cerver_set_file_upload_cb (
	FileCerver *file_cerver,
	void (*file_upload_cb) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		const char *saved_filename
	)
);

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
CERVER_PUBLIC String *file_cerver_search_file (
	FileCerver *file_cerver, const char *filename
);

// opens a file and sends the content back to the client
// first the FileHeader in a regular packet, then the file contents between sockets
// if the file is not found, a CERVER_ERROR_FILE_NOT_FOUND error packet will be sent
// returns the number of bytes sent, or -1 on error
CERVER_PUBLIC ssize_t file_cerver_send_file (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	const char *filename
);

CERVER_EXPORT void file_cerver_stats_print (FileCerver *file_cerver);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
