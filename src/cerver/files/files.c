#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/client/client.h"
#include "cerver/core/cerver.h"
#include "cerver/files/files.h"
#include "cerver/files/internal.h"
#include "cerver/handler/errors.h"
#include "cerver/network/network.h"
#include "cerver/network/socket.h"
#include "cerver/packets/packets.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

bool file_exists (const char *filename);

#pragma region main

// sanitizes a filename to correctly be used to save a file
// removes every character & whitespaces except for
// alphabet, numbers, '-', '_' and  '.'
void files_sanitize_filename (char *filename) {

	if (filename) {
		for (int i = 0, j; filename[i] != '\0'; ++i) {
			while (
				!(filename[i] >= 'a' && filename[i] <= 'z') && !(filename[i] >= 'A' && filename[i] <= 'Z')	// alphabet
				&& !(filename[i] >= 48 && filename[i] <= 57)												// numbers
				&& !(filename[i] == '-') && !(filename[i] == '_') && !(filename[i] == '.')					// clean characters
				&& !(filename[i] == '\0')
			) {
				for (j = i; filename[j] != '\0'; ++j) {
					filename[j] = filename[j + 1];
				}

				filename[j] = '\0';
			}
		}

		c_string_remove_spaces (filename);
	}

}

// works like files_sanitize_filename ()
// but also keeps '/' characters
void files_sanitize_complete_filename (char *filename) {

	if (filename) {
		for (int i = 0, j; filename[i] != '\0'; ++i) {
			while (
				!(filename[i] >= 'a' && filename[i] <= 'z') && !(filename[i] >= 'A' && filename[i] <= 'Z')	// alphabet
				&& !(filename[i] >= 48 && filename[i] <= 57)												// numbers
				&& !(filename[i] == '/')																	// path related
				&& !(filename[i] == '-') && !(filename[i] == '_') && !(filename[i] == '.')					// clean characters
				&& !(filename[i] == '\0')
			) {
				for (j = i; filename[j] != '\0'; ++j) {
					filename[j] = filename[j + 1];
				}

				filename[j] = '\0';
			}
		}

		c_string_remove_spaces (filename);
	}

}

// check if a directory already exists, and if not, creates it
// returns 0 on success, 1 on error
unsigned int files_create_dir (
	const char *dir_path, mode_t mode
) {

	unsigned int retval = 1;

	if (dir_path) {
		struct stat st = CERVER_ZERO_INITIALIZER;
		int ret = stat (dir_path, &st);
		switch (ret) {
			case -1: {
				if (!mkdir (dir_path, mode)) {
					retval = 0;		// success
				}

				else {
					cerver_log_error (
						"Failed to create dir %s!", dir_path
					);
				}
			} break;
			case 0: {
				cerver_log_warning (
					"Dir %s already exists!", dir_path
				);
			} break;

			default: break;
		}
	}

	return retval;

}

// a directory that exists is a success, even when another thread or process
// creates it between the stat () and the mkdir ()
// returns 0 on success, 1 on error
static unsigned int files_create_recursive_dir_single (
	const char *dir_path, mode_t mode
) {

	struct stat sb = CERVER_ZERO_INITIALIZER;

	if (!stat (dir_path, &sb)) return S_ISDIR (sb.st_mode) ? 0 : 1;

	if (!mkdir (dir_path, mode)) return 0;

	return ((errno == EEXIST) && !stat (dir_path, &sb) && S_ISDIR (sb.st_mode)) ? 0 : 1;

}

// creates each directory of the path that does not exist yet,
// stops in the first one that can not be created
// returns 0 on success, 1 on error
static unsigned int files_create_recursive_dir_path (
	char *path, mode_t mode
) {

	unsigned int retval = 0;

	for (char *p = path + 1; !retval && *p; p++) {
		if (*p == '/') {
			*p = '\0';
			retval = files_create_recursive_dir_single (path, mode);
			*p = '/';
		}
	}

	return retval ? retval : files_create_recursive_dir_single (path, mode);

}

// recursively creates all directories in dir path,
// a path that already is a directory is a success
// returns 0 on success, 1 on error
unsigned int files_create_recursive_dir (
	const char *dir_path, mode_t mode
) {

	unsigned int retval = 1;

	char path[DIRNAME_DEFAULT_SIZE] = { 0 };
	const size_t len = dir_path ? strlen (dir_path) : 0;

	// an empty path or one that does not fit is never truncated
	if (len && (len < DIRNAME_DEFAULT_SIZE)) {
		(void) memcpy (path, dir_path, len);

		// the trailing slash, except the one of the root
		if ((len > 1) && (path[len - 1] == '/')) path[len - 1] = '\0';

		retval = files_create_recursive_dir_path (path, mode);
	}

	#ifdef FILES_DEBUG
	if (retval) {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_FILE,
			"files_create_recursive_dir () - failed to create %s",
			dir_path ? dir_path : "(null)"
		);
	}
	#endif

	return retval;

}

// the bytes that copy_file_range () copies in each call
#define FILES_COPY_RANGE_SIZE			(1 << 20)

// the buffer of a copy with read () and write ()
#define FILES_COPY_BUFFER_SIZE			8192

#define FILES_PERMISSIONS_MASK			07777

// the directories that nftw () keeps open
#define FILES_REMOVE_OPEN_DIRECTORIES	16

// writes the path of an entry of a directory in path
// returns 0 on success, 1 if it does not fit
static unsigned int files_join_path (
	char path[FILES_MOVE_SIZE], const char *directory, const char *name
) {

	const int length = snprintf (path, FILES_MOVE_SIZE, "%s/%s", directory, name);

	return ((length > 0) && (length < FILES_MOVE_SIZE)) ? 0 : 1;

}

// writes every byte of the buffer
// returns 0 on success, 1 on error
static unsigned int files_write_all (
	const int destination_fd, const char *buffer, const size_t size
) {

	unsigned int errors = 0;

	size_t written = 0;
	while ((written < size) && !errors) {
		const ssize_t result = write (destination_fd, buffer + written, size - written);
		if (result > 0) written += (size_t) result;
		else if ((result == 0) || (errno != EINTR)) errors = 1;
	}

	return errors;

}

// copies until the end of the source with read () and write (), from their offsets
// returns 0 on success, 1 on error
static unsigned int files_copy_read_write (const int source_fd, const int destination_fd) {

	unsigned int errors = 0;

	char buffer[FILES_COPY_BUFFER_SIZE];

	bool done = false;
	while (!done && !errors) {
		const ssize_t read_bytes = read (source_fd, buffer, FILES_COPY_BUFFER_SIZE);
		if (read_bytes > 0) errors = files_write_all (destination_fd, buffer, (size_t) read_bytes);
		else if (read_bytes == 0) done = true;
		else if (errno != EINTR) errors = 1;
	}

	return errors;

}

// copies with copy_file_range () inside the kernel, the file systems that can not use it,
// and a source that changed size, continue with read () and write () from the offsets
// returns 0 on success, 1 on error
static unsigned int files_copy_contents (
	const int source_fd, const int destination_fd, const off_t size
) {

	off_t remaining = size;

	ssize_t copied = 1;
	while ((remaining > 0) && (copied > 0)) {
		const size_t range = (remaining < FILES_COPY_RANGE_SIZE) ? (size_t) remaining : FILES_COPY_RANGE_SIZE;

		copied = copy_file_range (source_fd, NULL, destination_fd, NULL, range, 0);
		if (copied > 0) remaining -= copied;
	}

	return files_copy_read_write (source_fd, destination_fd);

}

// copies a regular file with its permissions, replacing the destination if it exists
// returns 0 on success, 1 on error
unsigned int files_copy (
	const char *source, const char *destination
) {

	unsigned int errors = 1;

	const int source_fd = (source && destination) ? open (source, O_RDONLY | O_CLOEXEC) : -1;
	if (source_fd > -1) {
		struct stat st;
		if (!fstat (source_fd, &st) && S_ISREG (st.st_mode)) {
			const mode_t mode = st.st_mode & FILES_PERMISSIONS_MASK;

			const int destination_fd = open (destination, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, mode);
			if (destination_fd > -1) {
				errors = files_copy_contents (source_fd, destination_fd, st.st_size);
				errors |= (fchmod (destination_fd, mode) ? 1 : 0);
				errors |= (close (destination_fd) ? 1 : 0);

				if (errors) (void) unlink (destination);
			}
		}

		(void) close (source_fd);
	}

	return errors;

}

// copies the link itself, a destination that exists and is not a directory is replaced
// returns 0 on success, 1 on error
static unsigned int files_copy_link (const char *source, const char *destination) {

	unsigned int errors = 1;

	char target[FILES_MOVE_SIZE];
	const ssize_t length = readlink (source, target, FILES_MOVE_SIZE - 1);
	if (length >= 0) {
		target[length] = '\0';

		struct stat st;
		if (!lstat (destination, &st) && !S_ISDIR (st.st_mode)) {
			(void) unlink (destination);
		}

		errors = symlink (target, destination) ? 1 : 0;
	}

	return errors;

}

// returns true if path is the directory or is inside it,
// a directory that is copied inside itself never ends
static bool files_path_inside (const char *path, const char *directory) {

	const size_t length = strlen (directory);

	return !strncmp (path, directory, length) && ((path[length] == '\0') || (path[length] == '/'));

}

// creates the destination, or uses it if it is a directory, and copies each entry of the source
// returns 0 on success, 1 on error
static unsigned int files_copy_directory (
	const char *source, const char *destination, const mode_t mode
) {

	unsigned int errors = 1;

	// mkdir () applies the umask, so the permissions are set again
	struct stat st;
	const bool created = !mkdir (destination, mode & FILES_PERMISSIONS_MASK)
		&& !chmod (destination, mode & FILES_PERMISSIONS_MASK);

	const bool exists = !created && (errno == EEXIST) && !stat (destination, &st) && S_ISDIR (st.st_mode);

	DIR *directory = (created || exists) ? opendir (source) : NULL;
	if (directory) {
		errors = 0;

		char source_path[FILES_MOVE_SIZE];
		char destination_path[FILES_MOVE_SIZE];

		const struct dirent *entry = NULL;
		while (!errors && (entry = readdir (directory))) {
			if (strcmp (entry->d_name, ".") && strcmp (entry->d_name, "..")) {
				errors = files_join_path (source_path, source, entry->d_name)
					|| files_join_path (destination_path, destination, entry->d_name)
					|| files_copy_recursive (source_path, destination_path);
			}
		}

		(void) closedir (directory);
	}

	return errors;

}

// copies a file, a symbolic link or a directory with everything in it
// returns 0 on success, 1 on error
unsigned int files_copy_recursive (
	const char *source, const char *destination
) {

	unsigned int errors = 1;

	struct stat st;
	if (source && destination && !lstat (source, &st)) {
		if (S_ISDIR (st.st_mode)) {
			errors = files_path_inside (destination, source)
				? 1 : files_copy_directory (source, destination, st.st_mode);
		}

		else if (S_ISLNK (st.st_mode)) {
			errors = files_copy_link (source, destination);
		}

		else if (S_ISREG (st.st_mode)) {
			errors = files_copy (source, destination);
		}
	}

	return errors;

}

static int files_remove_entry (
	const char *path, const struct stat *st, int flag, struct FTW *ftw
) {

	(void) st;
	(void) flag;
	(void) ftw;

	return remove (path);

}

// removes a file, a symbolic link or a directory with everything in it
// returns 0 on success, 1 on error
unsigned int files_remove_recursive (const char *path) {

	return (path && !nftw (path, files_remove_entry, FILES_REMOVE_OPEN_DIRECTORIES, FTW_DEPTH | FTW_PHYS)) ? 0 : 1;

}

// the destination is saved_path, or the name of the file inside it if it is a directory
// returns 0 on success, 1 if the path does not fit
static unsigned int files_move_destination (
	const char *actual_path, const char *saved_path, char destination[FILES_MOVE_SIZE]
) {

	unsigned int errors = 0;

	struct stat st;
	if (!stat (saved_path, &st) && S_ISDIR (st.st_mode)) {
		const char *name = strrchr (actual_path, '/');
		errors = files_join_path (destination, saved_path, name ? (name + 1) : actual_path);
	}

	else {
		const int length = snprintf (destination, FILES_MOVE_SIZE, "%s", saved_path);
		errors = ((length >= 0) && (length < FILES_MOVE_SIZE)) ? 0 : 1;
	}

	return errors;

}

// rename () can not move between file systems, so the source is copied and then removed,
// a copy that fails is removed
// returns 0 on success, 1 on error
static int files_move_between_file_systems (const char *actual_path, const char *destination) {

	int retval = 1;

	if (!files_copy_recursive (actual_path, destination)) {
		retval = (int) files_remove_recursive (actual_path);
	}

	else {
		(void) files_remove_recursive (destination);
	}

	return retval;

}

// moves a file or a directory to saved_path, or inside it if it is a directory that exists
// returns 0 on success, 1 on error
int file_move_to (
	const char *actual_path, const char *saved_path
) {

	int retval = 1;

	char destination[FILES_MOVE_SIZE];
	if (actual_path && saved_path && !files_move_destination (actual_path, saved_path, destination)) {
		if (!rename (actual_path, destination)) {
			retval = 0;
		}

		else if (errno == EXDEV) {
			retval = files_move_between_file_systems (actual_path, destination);
		}
	}

	return retval;

}

// returns an allocated string with the file extension
// NULL if no file extension
char *files_get_file_extension (const char *filename) {

	char *retval = NULL;

	if (filename) {
		char *ptr = strrchr ((char *) filename, '.');
		if (ptr) {
			// *ptr++;
			size_t ext_len = 0;
			char *p = ptr;
			while (*p++) ext_len++;

			if (ext_len) {
				retval = (char *) calloc (ext_len + 1, sizeof (char));
				if (retval) {
					(void) memcpy (retval, ptr + 1, ext_len);
					retval[ext_len] = '\0';
				}
			}
		}

	}

	return retval;

}

// returns a reference to the file's extension
const char *files_get_file_extension_reference (
	const char *filename, unsigned int *ext_len
) {

	const char *retval = NULL;

	if (filename) {
		char *ptr = strrchr ((char *) filename, '.');
		if (ptr) {
			char *p = ptr + 1;
			while (*p++) *ext_len += 1;

			if (ext_len) {
				retval = ptr + 1;
			}
		}
	}

	return retval;

}

// returns a list of strings containg the names of all the files in the directory
DoubleList *files_get_from_dir (const char *dir) {

	DoubleList *images = NULL;

	if (dir) {
		images = dlist_init (str_delete, str_comparator);

		DIR *dp = opendir (dir);
		if (dp) {
			struct dirent *ep = NULL;
			String *file = NULL;
			while ((ep = readdir (dp)) != NULL) {
				if (strcmp (ep->d_name, ".") && strcmp (ep->d_name, "..")) {
					file = str_create ("%s/%s", dir, ep->d_name);

					(void) dlist_insert_at_end (images, file);
				}
			}

			(void) closedir (dp);
		}

		else {
			cerver_log_error ("Failed to open dir %s", dir);
		}
	}

	return images;

}

static String *file_get_line (
	FILE *file,
	char *buffer, const size_t buffer_size
) {

	String *str = NULL;

	if (!feof (file)) {
		if (fgets (buffer, buffer_size, file)) {
			c_string_remove_line_breaks (buffer);
			str = str_new (buffer);
		}
	}

	return str;

}

// reads each one of the file's lines into newly created strings
// and returns them inside a dlist
DoubleList *file_get_lines (
	const char *filename, const size_t buffer_size
) {

	DoubleList *lines = NULL;

	if (filename) {
		FILE *file = fopen (filename, "r");
		if (file) {
			lines = dlist_init (str_delete, str_comparator);

			char *buffer = (char *) calloc (buffer_size, sizeof (char));
			if (buffer) {
				String *line = NULL;

				while ((line = file_get_line (file, buffer, buffer_size))) {
					(void) dlist_insert_at_end_unsafe (lines, line);
				}

				free (buffer);
			}

			(void) fclose (file);
		}

		else {
			cerver_log_error ("Failed to open file: %s", filename);
		}
	}

	return lines;

}

// returns true if the filename exists
bool file_exists (const char *filename) {

	bool retval = false;

	if (filename) {
		struct stat filestatus = CERVER_ZERO_INITIALIZER;
		if (!stat (filename, &filestatus)) {
			retval = true;
		}
	}

	return retval;

}

// opens a file and returns it as a FILE
FILE *file_open_as_file (
	const char *filename, const char *modes, struct stat *filestatus
) {

	FILE *fp = NULL;

	if (filename) {
		(void) memset (filestatus, 0, sizeof (struct stat));
		if (!stat (filename, filestatus))
			fp = fopen (filename, modes);

		#ifdef FILES_DEBUG
		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_FILE,
				"File %s not found!", filename
			);

		}
		#endif
	}

	return fp;

}

static char *file_read_internal (
	const char *filename, const size_t n_bytes, size_t *n_read
) {

	char *file_contents = NULL;

	int file_fd = open (filename, O_RDONLY);
	if (file_fd > -1) {
		file_contents = (char *) calloc (
			n_bytes + 1, sizeof (char)
		);

		if (file_contents) {
			char *end = file_contents;
			ssize_t copied = 0;
			size_t to_copy = (size_t) n_bytes;

			#ifdef FILES_DEBUG
			unsigned int count = 0;
			#endif

			// each read continues where the last one ended
			do {
				copied = read (
					file_fd,
					end,
					to_copy
				);

				if (copied > 0) {
					to_copy -= (size_t) copied;
					*n_read += (size_t) copied;
					end += copied;

					#ifdef FILES_DEBUG
					count += 1;
					#endif
				}

			} while ((copied > 0) && to_copy);

			#ifdef FILES_DEBUG
			cerver_log_debug (
				"File read took %u copies", count
			);
			#endif

			// *end = '\0';
		}
		
		(void) close (file_fd);
	}

	return file_contents;

}

// opens and reads a file into a buffer
// sets file size to the amount of bytes read
char *file_read (const char *filename, size_t *file_size) {

	char *file_contents = NULL;

	if (filename) {
		struct stat filestatus = CERVER_ZERO_INITIALIZER;
		if (!stat (filename, &filestatus)) {
			size_t n_read = 0;
			file_contents = file_read_internal (
				filename, (size_t) filestatus.st_size, &n_read
			);

			// the bytes that were read, the file can change after the stat ()
			if (file_size) *file_size = n_read;
		}
	}

	return file_contents;

}

// opens and reads n bytes from a file into a buffer
char *file_n_read (
	const char *filename, const size_t n_bytes, size_t *n_read
) {

	char *file_contents = NULL;

	if (filename) {
		struct stat filestatus = CERVER_ZERO_INITIALIZER;
		if (!stat (filename, &filestatus)) {
			size_t file_size = (size_t) filestatus.st_size;

			size_t to_read = (n_bytes > file_size) ? file_size : n_bytes;

			file_contents = file_read_internal (
				filename, to_read, n_read
			);
		}
	}

	return file_contents;

}

// opens a file with the required flags
// returns fd on success, -1 on error
int file_open_as_fd (
	const char *filename, struct stat *filestatus, int flags
) {

	int retval = -1;

	if (filename) {
		(void) memset (filestatus, 0, sizeof (struct stat));
		if (!stat (filename, filestatus)) {
			retval = open (filename, flags);
		}
	}

	return retval;

}

#pragma endregion
