#ifndef _CERVER_FILES_H_
#define _CERVER_FILES_H_

#include <stdio.h>
#include <stdbool.h>

#include <unistd.h>

#include <sys/stat.h>
#include <sys/types.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#define DIRNAME_DEFAULT_SIZE			1024
#define FILENAME_DEFAULT_SIZE			1024

#define FILES_MOVE_SIZE					2048

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;

struct _FileHeader;

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
		struct _FileHeader *,
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
		struct _FileHeader *,
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

#pragma region main

// sanitizes a filename to correctly be used to save a file
// removes every character & whitespaces except for
// alphabet, numbers, '-', '_' and  '.'
CERVER_EXPORT void files_sanitize_filename (char *filename);

// works like files_sanitize_filename ()
// but also keeps '/' characters
CERVER_EXPORT void files_sanitize_complete_filename (char *filename);

// check if a directory already exists, and if not, creates it
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_create_dir (
	const char *dir_path, mode_t mode
);

// recursively creates all directories in dir path
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_create_recursive_dir (
	const char *dir_path, mode_t mode
);

// moves one file from one location to another
// returns 0 on success
CERVER_EXPORT int file_move_to (
	const char *actual_path, const char *saved_path
);

// returns an allocated string with the file extension
// NULL if no file extension
CERVER_EXPORT char *files_get_file_extension (const char *filename);

// returns a reference to the file's extension
CERVER_EXPORT const char *files_get_file_extension_reference (
	const char *filename, unsigned int *ext_len
);

// returns a list of strings containg the names of all the files in the directory
CERVER_EXPORT DoubleList *files_get_from_dir (const char *dir);

// reads each one of the file's lines into newly created strings
// and returns them inside a dlist
CERVER_EXPORT DoubleList *file_get_lines (
	const char *filename, const size_t buffer_size
);

// returns true if the filename exists
CERVER_EXPORT bool file_exists (const char *filename);

// opens a file and returns it as a FILE
CERVER_EXPORT FILE *file_open_as_file (
	const char *filename,
	const char *modes, struct stat *filestatus
);

// opens and reads a file into a buffer
// sets file size to the amount of bytes read
CERVER_EXPORT char *file_read (
	const char *filename, size_t *file_size
);

// opens a file with the required flags
// returns fd on success, -1 on error
CERVER_EXPORT int file_open_as_fd (
	const char *filename, struct stat *filestatus, int flags
);

#pragma endregion

#pragma region images

#define IMAGE_TYPE_MAP(XX)						\
	XX(0,	NONE, 		None, 		undefined)	\
	XX(1,	PNG, 		PNG,		png)		\
	XX(2,	JPEG, 		JPEG, 		jpeg)		\
	XX(3,	GIF, 		GIF,		gif)		\
	XX(4,	BMP, 		BMP,		bmp)

typedef enum ImageType {

	#define XX(num, name, string, extension) IMAGE_TYPE_##name = num,
	IMAGE_TYPE_MAP (XX)
	#undef XX

} ImageType;

CERVER_EXPORT const char *files_image_type_to_string (
	const ImageType type
);

CERVER_EXPORT const char *files_image_type_extension (
	const ImageType type
);

// reads the file's contents to find matching magic bytes
CERVER_EXPORT ImageType files_image_get_type_from_file (
	const void *file
);

// opens the file and returns the file's image type
CERVER_EXPORT ImageType files_image_get_type (
	const char *filename
);

// returns the correct image type based on the filename's extension
CERVER_EXPORT ImageType files_image_get_type_by_extension (
	const char *filename
);

// returns true if jpeg magic bytes are in file
CERVER_EXPORT bool files_image_is_jpeg (const char *filename);

// returns true if the filename's extension is jpg or jpeg
CERVER_EXPORT bool files_image_extension_is_jpeg (
	const char *filename
);

// returns true if png magic bytes are in file
CERVER_EXPORT bool files_image_is_png (const char *filename);

// returns true if the filename's extension is png
CERVER_EXPORT bool files_image_extension_is_png (
	const char *filename
);

#pragma endregion

#pragma region send

struct _FileHeader {

	char filename[FILENAME_DEFAULT_SIZE];
	size_t len;

};

typedef struct _FileHeader FileHeader;

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
CERVER_PUBLIC ssize_t file_send_by_fd (
	struct _Cerver *cerver,
	struct _Client *client, struct _Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

#pragma endregion

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