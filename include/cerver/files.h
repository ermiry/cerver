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

#define DEFAULT_FILENAME_LEN			1024

#pragma region cerver

#define FILE_CERVER_MAX_PATHS           32

struct _FileCerver {

	struct _Cerver *cerver;

	// search for requested files in these paths
	unsigned int n_paths;
	String *paths[FILE_CERVER_MAX_PATHS];

	// default path where uploads files will be placed
	String *uploads_path;
	
	// stats
	u64 n_files_requests;
	u64 n_success_files_requests;
	u64 n_bad_files_requests;
	u64 n_bytes_sent;

	u64 n_files_uploaded;
	u64 n_success_files_uploaded;
	u64 n_bad_files_uploaded;
	u64 n_bytes_received;

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

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
CERVER_PUBLIC String *file_cerver_search_file (FileCerver *file_cerver, const char *filename);

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

typedef struct FileHeader {

	char filename[DEFAULT_FILENAME_LEN];
	size_t len;

} FileHeader;

// opens a file and sends the content back to the client
// first the FileHeader in a regular packet, then the file contents between sockets
// returns 0 on success, 1 on error
CERVER_PUBLIC ssize_t file_send (
	struct _Cerver *cerver, struct _Client *client, struct _Connection *connection,
	const char *filename
);

#pragma endregion

#endif