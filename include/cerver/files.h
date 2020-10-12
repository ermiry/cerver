#ifndef _CERVER_FILES_H_
#define _CERVER_FILES_H_

#include <stdio.h>

#include <sys/stat.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/utils/json.h"

struct _Cerver;
struct _Client;
struct _connection;

struct _FileHeader;

#define DEFAULT_FILENAME_LEN			1024

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
		struct _FileHeader *, char **saved_filename
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
CERVER_EXPORT u8 file_cerver_add_path (FileCerver *file_cerver, const char *path);

// sets the default uploads path to be used when a client sends a file
CERVER_EXPORT void file_cerver_set_uploads_path (FileCerver *file_cerver, const char *uploads_path);

// sets a custom method to be used to handle a file upload
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
CERVER_EXPORT void file_cerver_set_file_upload_handler (
	FileCerver *file_cerver,
	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *, 
		struct _FileHeader *, char **saved_filename
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
CERVER_PUBLIC String *file_cerver_search_file (FileCerver *file_cerver, const char *filename);

// opens a file and sends the content back to the client
// first the FileHeader in a regular packet, then the file contents between sockets
// if the file is not found, a CERVER_ERROR_FILE_NOT_FOUND error packet will be sent
// returns the number of bytes sent, or -1 on error
CERVER_PUBLIC ssize_t file_cerver_send_file (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename
);

CERVER_EXPORT void file_cerver_stats_print (FileCerver *file_cerver);

#pragma endregion

#pragma region main

// check if a directory already exists, and if not, creates it
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_create_dir (const char *dir_path, mode_t mode);

// returns an allocated string with the file extension
// NULL if no file extension
CERVER_EXPORT char *files_get_file_extension (const char *filename);

// returns a list of strings containg the names of all the files in the directory
CERVER_EXPORT DoubleList *files_get_from_dir (const char *dir);

// reads eachone of the file's lines into a newly created string and returns them inside a dlist
CERVER_EXPORT DoubleList *file_get_lines (const char *filename);

// returns true if the filename exists
CERVER_EXPORT bool file_exists (const char *filename);

// opens a file and returns it as a FILE
CERVER_EXPORT FILE *file_open_as_file (
	const char *filename,
	const char *modes, struct stat *filestatus
);

// opens and reads a file into a buffer
// sets file size to the amount of bytes read
CERVER_EXPORT char *file_read (const char *filename, size_t *file_size);

// opens a file with the required flags
// returns fd on success, -1 on error
CERVER_EXPORT int file_open_as_fd (const char *filename, struct stat *filestatus, int flags);

CERVER_EXPORT json_value *file_json_parse (const char *filename);

#pragma endregion

#pragma region send

struct _FileHeader {

	char filename[DEFAULT_FILENAME_LEN];
	size_t len;

};

typedef struct _FileHeader FileHeader;

// opens a file and sends its contents
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
CERVER_PUBLIC ssize_t file_send (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename
);

// sends the file contents of the file referenced by a fd
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
CERVER_PUBLIC ssize_t file_send_by_fd (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

#pragma endregion

#pragma region receive

// opens the file using an already created filename
// and use the fd to receive and save the file
CERVER_PRIVATE u8 file_receive_actual (
	Client *client, Connection *connection,
	FileHeader *file_header, char **saved_filename
);

#pragma endregion

#endif