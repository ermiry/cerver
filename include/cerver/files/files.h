#ifndef _CERVER_FILES_FILES_H_
#define _CERVER_FILES_FILES_H_

#include <stdbool.h>
#include <stdio.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/packets/serializer.h"

#define DIRNAME_DEFAULT_SIZE			1024
#define FILENAME_DEFAULT_SIZE			1024

// the paths that the move, copy and remove methods build, with the NUL
#define FILES_MOVE_SIZE					2048

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;

struct FileHeader;

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

// recursively creates all directories in dir path,
// a path that already is a directory is a success,
// an empty path or one that does not fit in DIRNAME_DEFAULT_SIZE is an error
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_create_recursive_dir (
	const char *dir_path, mode_t mode
);

// moves a file or a directory to saved_path, or inside it if it is a directory that exists,
// with rename (), and between file systems with a copy that is removed after it
// returns 0 on success, 1 on error
CERVER_EXPORT int file_move_to (
	const char *actual_path, const char *saved_path
);

// copies a regular file with its permissions, replacing the destination if it exists,
// a copy that fails is removed
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_copy (
	const char *source, const char *destination
);

// copies a file, a symbolic link (without following it) or a directory with everything in it,
// the directories of the destination can exist
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_copy_recursive (
	const char *source, const char *destination
);

// removes a file, a symbolic link (without following it) or a directory with everything in it
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int files_remove_recursive (const char *path);

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

// opens and reads a file into a buffer, terminated with a NUL,
// allocated with calloc () and released with free ()
// sets file size (if not NULL) to the amount of bytes read
CERVER_EXPORT char *file_read (
	const char *filename, size_t *file_size
);

// opens and reads n bytes from a file into a buffer
CERVER_EXPORT char *file_n_read (
	const char *filename, const size_t n_bytes, size_t *n_read
);

// opens a file with the required flags
// returns fd on success, -1 on error
CERVER_EXPORT int file_open_as_fd (
	const char *filename, struct stat *filestatus, int flags
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
