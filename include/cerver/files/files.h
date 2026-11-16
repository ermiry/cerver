/**
 * @file cerver/files/files.h
 * @brief The files methods of cerver: the names, the directories, the copies, the moves and the reads.
 */

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

/**
 * @defgroup files Files
 * @brief The files of cerver: the files methods, the files cerver that sends and receives files with its clients,
 * and the image types.
 */

/**
 * @defgroup files_main Files
 * @ingroup files
 * @brief The methods of the names, the directories and the contents of the files.
 * @{
 */

/** @brief The size of a directory path that the files methods build, with its terminator. */
#define DIRNAME_DEFAULT_SIZE			1024

/** @brief The size of a filename, with its terminator. */
#define FILENAME_DEFAULT_SIZE			1024

/** @brief The size of the paths that the move, copy and remove methods build, with the NUL. */
#define FILES_MOVE_SIZE					2048

#ifdef __cplusplus
extern "C" {
#endif

struct _Cerver;
struct _Client;
struct _Connection;
struct _Packet;

struct FileHeader;

/**
 * @brief Sanitizes a filename in place so it can be used to save a file: it keeps only the letters, the numbers,
 * '-', '_' and '.', and removes the whitespaces.
 *
 * @param[in,out] filename the filename, it can be NULL
 */
CERVER_EXPORT void files_sanitize_filename (char *filename);

/**
 * @brief Works like files_sanitize_filename (), but also keeps the '/' characters.
 *
 * @param[in,out] filename the filename, it can be NULL
 */
CERVER_EXPORT void files_sanitize_complete_filename (char *filename);

/**
 * @brief Creates a directory if it does not exist.
 *
 * A directory that already exists is a success, also when another thread or process creates it at the same time.
 *
 * @param[in] dir_path the path of the directory
 * @param[in] mode the permissions of the new directory
 * @retval 0 the directory was created or it already existed
 * @retval 1 a NULL path, a path that exists and is not a directory, or mkdir () failed
 */
CERVER_EXPORT unsigned int files_create_dir (
	const char *dir_path, mode_t mode
);

/**
 * @brief Creates every directory of a path, like `mkdir -p`.
 *
 * @param[in] dir_path the path, a trailing '/' is ignored
 * @param[in] mode the permissions of the new directories
 * @retval 0 every directory exists, a path that already is a directory is a success
 * @retval 1 a NULL or empty path, one that does not fit in #DIRNAME_DEFAULT_SIZE, or a directory could not be
 * created
 */
CERVER_EXPORT unsigned int files_create_recursive_dir (
	const char *dir_path, mode_t mode
);

/**
 * @brief Moves a file or a directory to saved_path, or inside it if it is a directory that exists.
 *
 * It uses rename (), and between file systems a copy that is removed after it.
 *
 * @param[in] actual_path the path of the file
 * @param[in] saved_path the new path, or the directory to move it into
 * @retval 0 the file was moved
 * @retval 1 a NULL path, a destination that does not fit in #FILES_MOVE_SIZE, or the move failed
 */
CERVER_EXPORT int file_move_to (
	const char *actual_path, const char *saved_path
);

/**
 * @brief Copies a regular file with its permissions, replacing the destination if it exists.
 *
 * @param[in] source the path of the file
 * @param[in] destination the path of the copy, removed if the copy fails
 * @retval 0 the file was copied
 * @retval 1 a NULL path, a source that is not a regular file, or the copy failed
 */
CERVER_EXPORT unsigned int files_copy (
	const char *source, const char *destination
);

/**
 * @brief Copies a file, a symbolic link (without following it) or a directory with everything in it.
 *
 * The directories of the destination can exist.
 *
 * @param[in] source the path to copy
 * @param[in] destination the path of the copy, it can not be inside a source directory
 * @retval 0 everything was copied
 * @retval 1 a NULL path, a source that does not exist or of another type, a destination inside the source, or a
 * copy failed
 */
CERVER_EXPORT unsigned int files_copy_recursive (
	const char *source, const char *destination
);

/**
 * @brief Removes a file, a symbolic link (without following it) or a directory with everything in it.
 *
 * @param[in] path the path to remove
 * @retval 0 everything was removed
 * @retval 1 a NULL path or something could not be removed
 */
CERVER_EXPORT unsigned int files_remove_recursive (const char *path);

/**
 * @brief Returns a copy of the extension of a filename, what follows its last '.'.
 *
 * @param[in] filename the filename
 * @return the extension, allocated with calloc () and released with free (); an empty string for a filename that
 * ends in '.', NULL for a NULL filename or one without a '.'
 */
CERVER_EXPORT char *files_get_file_extension (const char *filename);

/**
 * @brief Returns a reference to the extension of a filename, what follows its last '.'.
 *
 * @param[in] filename the filename
 * @param[out] ext_len the length of the extension, 0 without one; it can be NULL
 * @return a pointer inside the filename, NULL for a NULL filename, one without a '.' or one that ends in '.'
 */
CERVER_EXPORT const char *files_get_file_extension_reference (
	const char *filename, unsigned int *ext_len
);

/**
 * @brief Returns the paths of the entries of a directory, without "." and "..".
 *
 * @param[in] dir the directory
 * @return a list of String with "dir/name" for each entry, empty if the directory can not be opened; NULL for a
 * NULL directory
 */
CERVER_EXPORT DoubleList *files_get_from_dir (const char *dir);

/**
 * @brief Reads each line of a file into a String, without its line break.
 *
 * @param[in] filename the path of the file
 * @param[in] buffer_size the size of the buffer of each read, a longer line is split in many Strings
 * @return the list of lines, NULL for a NULL filename or if the file can not be opened
 */
CERVER_EXPORT DoubleList *file_get_lines (
	const char *filename, const size_t buffer_size
);

/**
 * @brief Returns whether a path exists, with stat ().
 *
 * @param[in] filename the path
 * @return true if it exists, false for a NULL path
 */
CERVER_EXPORT bool file_exists (const char *filename);

/**
 * @brief Opens a file with fopen () after getting its status.
 *
 * @param[in] filename the path of the file
 * @param[in] modes the modes of fopen ()
 * @param[out] filestatus the status of the file, it must not be NULL
 * @return the file, NULL for a NULL filename, a file that does not exist or that fopen () could not open
 */
CERVER_EXPORT FILE *file_open_as_file (
	const char *filename,
	const char *modes, struct stat *filestatus
);

/**
 * @brief Reads a whole file into a buffer terminated with a NUL.
 *
 * @param[in] filename the path of the file
 * @param[out] file_size the bytes that were read, it can be NULL
 * @return the buffer, allocated with calloc () and released with free (); NULL for a NULL filename or a file that
 * does not exist or can not be opened
 */
CERVER_EXPORT char *file_read (
	const char *filename, size_t *file_size
);

/**
 * @brief Reads at most n bytes of a file into a buffer terminated with a NUL.
 *
 * @param[in] filename the path of the file
 * @param[in] n_bytes the bytes to read, at most the size of the file
 * @param[out] n_read the bytes that were read, it can be NULL
 * @return the buffer, allocated with calloc () and released with free (); NULL for a NULL filename or a file that
 * does not exist or can not be opened
 */
CERVER_EXPORT char *file_n_read (
	const char *filename, const size_t n_bytes, size_t *n_read
);

/**
 * @brief Opens a file with open () after getting its status.
 *
 * @param[in] filename the path of the file
 * @param[out] filestatus the status of the file, it must not be NULL
 * @param[in] flags the flags of open ()
 * @return the fd, -1 for a NULL filename, a file that does not exist or on error
 */
CERVER_EXPORT int file_open_as_fd (
	const char *filename, struct stat *filestatus, int flags
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
