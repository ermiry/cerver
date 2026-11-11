#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cerver/config.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <time.h>
#include <unistd.h>

#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/cerver.h"
#include "cerver/client.h"
#include "cerver/errors.h"
#include "cerver/files.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/socket.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

bool file_exists (const char *filename);

static ssize_t file_send_actual (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
);

static int file_send_open (
	const char *filename,
	struct stat *filestatus,
	const char **actual_filename
);

static u8 file_cerver_receive (
	Cerver *cerver, Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

u8 file_receive_actual (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
);

#pragma region cerver

static FileCerverStats *file_cerver_stats_new (void) {

	FileCerverStats *stats = (FileCerverStats *) malloc (sizeof (FileCerverStats));
	if (stats) {
		stats->n_files_requests = 0;
		stats->n_success_files_requests = 0;
		stats->n_bad_files_requests = 0;
		stats->n_files_sent = 0;
		stats->n_bad_files_sent = 0;
		stats->n_bytes_sent = 0;

		stats->n_files_upload_requests = 0;
		stats->n_success_files_uploaded = 0;
		stats->n_bad_files_upload_requests = 0;
		stats->n_bad_files_received = 0;
		stats->n_bytes_received = 0;
	}

	return stats;

}

static void file_cerver_stats_delete (FileCerverStats *stats) {

	if (stats) free (stats);

}

FileCerver *file_cerver_new (void) {

	FileCerver *file_cerver = (FileCerver *) malloc (sizeof (FileCerver));
	if (file_cerver) {
		file_cerver->cerver = NULL;

		file_cerver->n_paths = 0;
		for (unsigned int i = 0; i < FILE_CERVER_MAX_PATHS; i++)
			file_cerver->paths[i] = NULL;

		file_cerver->uploads_path = NULL;

		file_cerver->file_upload_handler = file_cerver_receive;

		file_cerver->file_upload_cb = NULL;

		file_cerver->stats = NULL;
	}

	return file_cerver;

}

void file_cerver_delete (void *file_cerver_ptr) {

	if (file_cerver_ptr) {
		FileCerver *file_cerver = (FileCerver *) file_cerver_ptr;

		for (unsigned int i = 0; i < FILE_CERVER_MAX_PATHS; i++) {
			if (file_cerver->paths[i]) str_delete (file_cerver->paths[i]);
		}

		str_delete (file_cerver->uploads_path);

		file_cerver_stats_delete (file_cerver->stats);

		free (file_cerver_ptr);
	}

}

FileCerver *file_cerver_create (Cerver *cerver) {

	FileCerver *file_cerver = file_cerver_new ();
	if (file_cerver) {
		file_cerver->cerver = cerver;

		file_cerver->stats = file_cerver_stats_new ();
	}

	return file_cerver;

}

// adds a new file path to take into account when a client request for a file
// returns 0 on success, 1 on error
u8 file_cerver_add_path (
	FileCerver *file_cerver, const char *path
) {

	u8 retval = 1;

	if (file_cerver && path) {
		if (file_cerver->n_paths < FILE_CERVER_MAX_PATHS) {
			file_cerver->paths[file_cerver->n_paths] = str_new (path);
			file_cerver->n_paths += 1;

			// the path was added, it always returned an error before
			retval = 0;
		}
	}

	return retval;

}

// sets the default uploads path to be used when a client sends a file
void file_cerver_set_uploads_path (
	FileCerver *file_cerver, const char *uploads_path
) {

	if (file_cerver && uploads_path) {
		str_delete (file_cerver->uploads_path);
		file_cerver->uploads_path = str_new (uploads_path);
	}

}

// sets a custom method to bed used to handle a file upload
// in this method, file contents must be consumed from the sock fd
// and return 0 on success and 1 on error
void file_cerver_set_file_upload_handler (
	FileCerver *file_cerver,
	u8 (*file_upload_handler) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		struct FileHeader *,
		const char *file_data, size_t file_data_len,
		char **saved_filename
	)
) {

	if (file_cerver) {
		file_cerver->file_upload_handler = file_upload_handler;
	}

}

// sets a callback to be executed after a file has been successfully uploaded by a client
void file_cerver_set_file_upload_cb (
	FileCerver *file_cerver,
	void (*file_upload_cb) (
		struct _Cerver *, struct _Client *, struct _Connection *,
		const char *saved_filename
	)
) {

	if (file_cerver) {
		file_cerver->file_upload_cb = file_upload_cb;
	}

}

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
String *file_cerver_search_file (
	FileCerver *file_cerver, const char *filename
) {

	String *retval = NULL;

	if (file_cerver && filename) {
		char filename_query[FILENAME_DEFAULT_SIZE * 2] = { 0 };
		for (unsigned int i = 0; i < file_cerver->n_paths; i++) {
			(void) snprintf (
				filename_query, FILENAME_DEFAULT_SIZE * 2,
				"%s/%s",
				file_cerver->paths[i]->str, filename
			);

			if (file_exists (filename_query)) {
				retval = str_new (filename_query);
				break;
			}
		}
	}

	return retval;

}

// opens a file and sends the content back to the client
// first the FileHeader in a regular packet, then the file contents between sockets
// if the file is not found, a CERVER_ERROR_FILE_NOT_FOUND error packet will be sent
// returns the number of bytes sent, or -1 on error
ssize_t file_cerver_send_file (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename
) {

	ssize_t retval = -1;

	if (filename && connection) {
		const char *actual_filename = NULL;
		struct stat filestatus = CERVER_ZERO_INITIALIZER;
		int file_fd = file_send_open (filename, &filestatus, &actual_filename);
		if (file_fd > -1) {
			retval = file_send_actual (
				cerver, client, connection,
				file_fd, actual_filename, filestatus.st_size
			);

			close (file_fd);
		}

		else {
			(void) error_packet_generate_and_send (
				CERVER_ERROR_FILE_NOT_FOUND, "File not found",
				cerver, client, connection
			);
		}
	}

	return retval;

}

static u8 file_cerver_receive (
	Cerver *cerver, Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
) {

	u8 retval = 1;

	FileCerver *file_cerver = (FileCerver *) cerver->cerver_data;

	// generate a custom filename taking into account the uploads path
	*saved_filename = c_string_create (
		"%s/%ld-%d-%s",
		file_cerver->uploads_path->str,
		time (NULL), random_int_in_range (0, 1000),
		file_header->filename
	);

	if (*saved_filename) {
		retval = file_receive_actual (
			client, connection,
			file_header,
			file_data, file_data_len,
			saved_filename
		);
	}

	return retval;

}

void file_cerver_stats_print (FileCerver *file_cerver) {

	if (file_cerver) {
		cerver_log_msg ("Files requests:                %lu", file_cerver->stats->n_files_requests);
		cerver_log_msg ("Success requests:              %lu", file_cerver->stats->n_success_files_requests);
		cerver_log_msg ("Bad requests:                  %lu", file_cerver->stats->n_bad_files_requests);
		cerver_log_msg ("Files sent:                    %lu", file_cerver->stats->n_files_sent);
		cerver_log_msg ("Failed files sent:             %lu", file_cerver->stats->n_bad_files_sent);
		cerver_log_msg ("Files bytes sent:              %lu\n", file_cerver->stats->n_bytes_sent);

		cerver_log_msg ("Files upload requests:         %lu", file_cerver->stats->n_files_upload_requests);
		cerver_log_msg ("Success uploads:               %lu", file_cerver->stats->n_success_files_uploaded);
		cerver_log_msg ("Bad uploads:                   %lu", file_cerver->stats->n_bad_files_upload_requests);
		cerver_log_msg ("Bad files received:            %lu", file_cerver->stats->n_bad_files_received);
		cerver_log_msg ("Files bytes received:          %lu\n", file_cerver->stats->n_bytes_received);
	}

}

#pragma endregion

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

// recursively creates all directories in dir path
// returns 0 on success, 1 on error
unsigned int files_create_recursive_dir (
	const char *dir_path, mode_t mode
) {

	unsigned int retval = 1;

	char tmp[DIRNAME_DEFAULT_SIZE] = { 0 };
	char *p = NULL;
	struct stat sb = CERVER_ZERO_INITIALIZER;

	// check if the path already exists
	if (stat (dir_path, &sb)) {
		size_t len = strlen (dir_path);
		(void) strncpy (tmp, dir_path, DIRNAME_DEFAULT_SIZE - 1);

		// remove trailing slash
		if (tmp[len - 1] == '/') {
			tmp[len - 1] = '\0';
		}

		// recursive mkdir
		for (p = tmp + 1; *p; p++) {
			if (*p == '/') {
				*p = 0;

				if (stat (tmp, &sb)) {
					// create directory
					if (mkdir (tmp, mode)) {
						break;
					}
				}

				else if (!S_ISDIR (sb.st_mode)) {
					// not a directory
					break;
				}

				*p = '/';
			}
		}

		// create final dir
		if (stat (tmp, &sb)) {
			// create directory
			if (!mkdir (tmp, mode)) {
				retval = 0;
			}
		}
	}

	#ifdef FILES_DEBUG
	else {
		cerver_log (
			LOG_TYPE_WARNING, LOG_TYPE_FILE,
			"files_create_recursive_dir () - dir %s exists!", dir_path
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
			*file_size = (size_t) filestatus.st_size;

			size_t n_read = 0;
			file_contents = file_read_internal (
				filename, (size_t) filestatus.st_size, &n_read
			);
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

#pragma region images

// the bytes that are read to find the type of an image
#define FILES_IMAGE_HEADER_SIZE			24

// the rest of a received file is moved in chunks of this size
#define FILES_RECEIVE_CHUNK_SIZE		4096

// how many times a recv () can time out in the middle of a file
#define FILES_RECEIVE_MAX_TIMEOUTS		4

static const char *image_bmp_extension = { "bmp" };
static const char *image_gif_extension = { "gif" };
static const char *image_png_extension = { "png" };
static const char *image_jpg_extension = { "jpg" };
static const char *image_jpeg_extension = { "jpeg" };

const char *files_image_type_to_string (const ImageType type) {

	switch (type) {
		#define XX(num, name, string, extension) case IMAGE_TYPE_##name: return #string;
		IMAGE_TYPE_MAP(XX)
		#undef XX
	}

	return files_image_type_to_string (IMAGE_TYPE_NONE);

}

const char *files_image_type_extension (const ImageType type) {

	switch (type) {
		#define XX(num, name, string, extension) case IMAGE_TYPE_##name: return #extension;
		IMAGE_TYPE_MAP(XX)
		#undef XX
	}

	return NULL;

}

ImageType files_image_get_type_from_file (const void *file_ptr) {

	FILE *file = (FILE *) file_ptr;

	ImageType type = IMAGE_TYPE_NONE;

	(void) fseek (file, 0, SEEK_END);
	long len = ftell (file);
	(void) fseek (file, 0, SEEK_SET);
	if (len > FILES_IMAGE_HEADER_SIZE) {
		unsigned char buf[FILES_IMAGE_HEADER_SIZE] = { 0 };

		// a short read leaves the header zeroed, it has no known type then
		if (fread (buf, 1, FILES_IMAGE_HEADER_SIZE, file) < FILES_IMAGE_HEADER_SIZE) {
			(void) memset (buf, 0, FILES_IMAGE_HEADER_SIZE);
		}

		if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) {
			type = IMAGE_TYPE_JPEG;
		}

		else if (
			buf[0] == 0x89 && buf[1] == 'P' && buf[2] =='N' && buf[3] =='G'
			&& buf[4] == 0x0D && buf[5] == 0x0A && buf[6] ==0x1A && buf[7] ==0x0A
			&& buf[12] == 'I' && buf[13] == 'H' && buf[14] =='D' && buf[15] =='R'
		) {
			type = IMAGE_TYPE_PNG;
		}

		else if (buf[0] =='G' && buf[1] =='I' && buf[2] =='F') {
			type = IMAGE_TYPE_GIF;
		}
	}

	return type;

}

ImageType files_image_get_type (const char *filename) {

	ImageType type = IMAGE_TYPE_NONE;

	if (!filename) return type;

	FILE *file = fopen (filename, "rb");
	if (file) {
		type = files_image_get_type_from_file (file);

		(void) fclose (file);
	}

	return type;

}

ImageType files_image_get_type_by_extension (const char *filename) {

	ImageType type = IMAGE_TYPE_NONE;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		if (!strcmp (image_png_extension, ext)) {
			type = IMAGE_TYPE_PNG;
		}

		else if (
			!strcmp (image_jpg_extension, ext)
			|| !strcmp (image_jpeg_extension, ext)
		) {
			type = IMAGE_TYPE_JPEG;
		}

		else if (!strcmp (image_bmp_extension, ext)) {
			type = IMAGE_TYPE_BMP;
		}

		else if (!strcmp (image_gif_extension, ext)) {
			type = IMAGE_TYPE_GIF;
		}
	}

	return type;

}

bool files_image_is_jpeg (const char *filename) {

	bool result = false;

	switch (files_image_get_type (filename)) {
		case IMAGE_TYPE_JPEG:
			result = true;
			break;

		default: break;
	}

	return result;

}

bool files_image_extension_is_jpeg (const char *filename) {

	bool retval = false;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		if (
			!strcmp (image_jpg_extension, ext)
			|| !strcmp (image_jpeg_extension, ext)
		) {
			retval = true;
		}
	}

	return retval;

}

bool files_image_is_png (const char *filename) {

	return (files_image_get_type (filename) == IMAGE_TYPE_PNG);

}

bool files_image_extension_is_png (const char *filename) {

	bool retval = false;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		if (!strcmp (image_png_extension, ext)) {
			retval = true;
		}
	}

	return retval;

}

#pragma endregion

#pragma region send

static const char *FILE_HEADER_CURRENT_DIRECTORY = ".";
static const char *FILE_HEADER_PARENT_DIRECTORY = "..";

SERIALIZER_DEFINE (FileHeader, file_header, FILE_HEADER_VERSION, FILE_HEADER_FIELDS)

// a FileHeader that comes from a packet is used only if its filename is NUL terminated,
// not empty, a single name (without '/') and different from "." and "..",
// so it can not read past the header or reach a file outside of the configured paths
bool file_header_is_valid (const FileHeader *file_header) {

	bool valid = false;

	if (file_header) {
		const char *end = (const char *) memchr (
			file_header->filename, '\0', FILENAME_DEFAULT_SIZE
		);

		valid = end
			&& (end != file_header->filename)
			&& !strchr (file_header->filename, '/')
			&& strcmp (file_header->filename, FILE_HEADER_CURRENT_DIRECTORY)
			&& strcmp (file_header->filename, FILE_HEADER_PARENT_DIRECTORY);
	}

	return valid;

}

// decodes the FileHeader of a packet and validates it
// returns true if the packet has a valid header
bool file_header_get (
	const Packet *packet, FileHeader *file_header
) {

	bool valid = false;

	if (packet && file_header) {
		FileHeader decoded;
		if (packet_get_object (packet, &file_header_schema, NULL, &decoded) == SERIALIZER_RESULT_NONE) {
			valid = file_header_is_valid (&decoded);
			if (valid) (void) memcpy (file_header, &decoded, sizeof (FileHeader));
		}
	}

	return valid;

}

// creates a request packet ready to be sent with the FileHeader of the file
// returns NULL on error
Packet *file_header_packet_create (
	const u32 request_type, const char *filename, const u64 len
) {

	Packet *packet = NULL;

	if (filename && (strlen (filename) < FILENAME_DEFAULT_SIZE)) {
		FileHeader file_header;
		serializer_object_init (&file_header_schema, &file_header);

		(void) strncpy (file_header.filename, filename, FILENAME_DEFAULT_SIZE - 1);
		file_header.len = len;

		packet = packet_create_serialized (
			PACKET_TYPE_REQUEST, request_type,
			&file_header_schema, &file_header
		);
	}

	return packet;

}

// sends a first packet with file info
// returns 0 on success, 1 on error
static u8 file_send_header (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename, size_t filelen
) {

	u8 retval = 1;

	Packet *packet = file_header_packet_create (
		REQUEST_PACKET_TYPE_SEND_FILE, filename, (u64) filelen
	);

	if (packet) {
		packet_set_network_values (packet, cerver, client, connection, NULL);

		retval = packet_send_unsafe (packet, 0, NULL, false);

		packet_delete (packet);
	}

	return retval;

}

static ssize_t file_send_actual (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
) {

	ssize_t retval = -1;

	(void) pthread_mutex_lock (connection->socket->write_mutex);

	// send a first packet with file info
	if (!file_send_header (
		cerver, client, connection,
		actual_filename, filelen
	)) {
		// send the actual file, a sendfile () can send only a part of it
		size_t sent = 0;
		if (!socket_send_file (
			connection->socket, file_fd, filelen,
			(time_t) connection->send_timeout,
			&sent, NULL
		)) {
			retval = (ssize_t) sent;
		}

		else {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_FILE,
				"file_send_actual () - sent %zu of %zu bytes",
				sent, filelen
			);
		}
	}

	else {
		cerver_log (
			LOG_TYPE_ERROR, LOG_TYPE_FILE,
			"file_send_actual () - failed to send file header"
		);
	}

	(void) pthread_mutex_unlock (connection->socket->write_mutex);

	return retval;

}

static int file_send_open (
	const char *filename, struct stat *filestatus,
	const char **actual_filename
) {

	int file_fd = -1;

	char *last = strrchr ((char *) filename, '/');
	*actual_filename = last ? last + 1 : NULL;
	if (actual_filename) {
		// try to open the file
		file_fd = file_open_as_fd (filename, filestatus, O_RDONLY);
		if (file_fd <= 0) {
			cerver_log (
				LOG_TYPE_ERROR, LOG_TYPE_FILE,
				"file_send () - Failed to open file %s", filename
			);
		}
	}

	else {
		cerver_log_error ("file_send () - failed to get actual filename");
	}

	return file_fd;

}

// opens a file and sends its contents
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
ssize_t file_send (
	Cerver *cerver, Client *client, Connection *connection,
	const char *filename
) {

	ssize_t retval = -1;

	if (filename && connection) {
		const char *actual_filename = NULL;
		struct stat filestatus = CERVER_ZERO_INITIALIZER;
		int file_fd = file_send_open (filename, &filestatus, &actual_filename);
		if (file_fd > -1) {
			retval = file_send_actual (
				cerver, client, connection,
				file_fd, actual_filename, filestatus.st_size
			);

			(void) close (file_fd);
		}
	}

	return retval;

}

// sends the file contents of the file referenced by a fd
// first the FileHeader in a regular packet, then the file contents between sockets
// returns the number of bytes sent, or -1 on error
ssize_t file_send_by_fd (
	Cerver *cerver, Client *client, Connection *connection,
	int file_fd, const char *actual_filename, size_t filelen
) {

	ssize_t retval = -1;

	if (client && connection && actual_filename) {
		retval = file_send_actual (
			cerver, client, connection,
			file_fd, actual_filename, filelen
		);
	}

	return retval;

}

#pragma endregion

#pragma region receive

// move from socket to pipe buffer
static inline u8 file_receive_internal_receive (
	Connection *connection, int pipefd, int buff_size,
	ssize_t *received
) {

	u8 retval = 1;

	*received = splice (
		connection->socket->sock_fd, NULL,
		pipefd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*received) {
		case -1: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_receive () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_receive () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef FILES_DEBUG
			cerver_log_debug (
				"file_receive_internal_receive () - spliced %ld bytes",
				*received
			);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// move from pipe buffer to file
static inline u8 file_receive_internal_move (
	int pipefd, int file_fd, int buff_size,
	ssize_t *moved
) {

	u8 retval = 1;

	*moved = splice (
		pipefd, NULL,
		file_fd, NULL,
		buff_size,
		SPLICE_F_MOVE | SPLICE_F_MORE
	);

	switch (*moved) {
		case -1: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_move () - splice () = -1");
			#endif
		} break;

		case 0: {
			#ifdef FILES_DEBUG
			perror ("file_receive_internal_move () - splice () = 0");
			#endif
		} break;

		default: {
			#ifdef FILES_DEBUG
			cerver_log_debug (
				"file_receive_internal_move () - spliced %ld bytes",
				*moved
			);
			#endif

			retval = 0;
		} break;
	}

	return retval;

}

// moves every byte that entered the pipe to the file,
// splice () can move less bytes than the ones in the pipe
// returns 0 on success, 1 on error
static u8 file_receive_internal_move_all (
	int pipefd, int file_fd, size_t pending
) {

	u8 retval = 0;

	ssize_t moved = 0;
	while (!retval && pending) {
		if (file_receive_internal_move (pipefd, file_fd, (int) pending, &moved)) retval = 1;
		else pending -= (size_t) moved;
	}

	return retval;

}

// a recv () timeout in the middle of the file is retried a few times
static inline bool file_receive_internal_retry (
	const ssize_t received, unsigned int *timeouts
) {

	return (received < 0)
		&& ((errno == EAGAIN) || (errno == EWOULDBLOCK) || (errno == EINTR))
		&& (++(*timeouts) < FILES_RECEIVE_MAX_TIMEOUTS);

}

// receives the rest of the file from the socket,
// it counts the bytes that were received, not the ones that were requested
static u8 file_receive_internal (
	Connection *connection, size_t filelen, int file_fd
) {

	u8 retval = 1;

	int pipefds[2] = { -1, -1 };
	if (!pipe (pipefds)) {
		size_t len = filelen;
		unsigned int timeouts = 0;
		bool failed = false;
		while (!failed && len) {
			const size_t chunk = (len < FILES_RECEIVE_CHUNK_SIZE) ? len : FILES_RECEIVE_CHUNK_SIZE;

			ssize_t received = 0;
			if (!file_receive_internal_receive (connection, pipefds[1], (int) chunk, &received)) {
				timeouts = 0;
				failed = file_receive_internal_move_all (pipefds[0], file_fd, (size_t) received);
				len -= (size_t) received;
			}

			else {
				failed = !file_receive_internal_retry (received, &timeouts);
			}
		}

		(void) close (pipefds[0]);
		(void) close (pipefds[1]);

		if (!len) retval = 0;
	}

	return retval;

}

// writes every byte, write () can write less bytes than the ones requested
// returns 0 on success, 1 on error
static u8 file_receive_write_all (
	int file_fd, const char *data, size_t size
) {

	u8 retval = 0;

	while (!retval && size) {
		const ssize_t wrote = write (file_fd, data, size);
		if (wrote > 0) {
			data += wrote;
			size -= (size_t) wrote;
		}

		else if ((wrote < 0) && (errno == EINTR)) continue;

		else retval = 1;
	}

	return retval;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

// opens the file using an already created filename
// and use the fd to receive and save the file
u8 file_receive_actual (
	Client *client, Connection *connection,
	FileHeader *file_header,
	const char *file_data, size_t file_data_len,
	char **saved_filename
) {

	u8 retval = 1;

	int file_fd = open (*saved_filename, O_CREAT | O_WRONLY | O_TRUNC, 0777);
	if (file_fd > -1) {
		// ssize_t received = splice (
		// 	connection->socket->sock_fd, NULL,
		// 	file_fd, NULL,
		// 	file_header->len,
		// 	0
		// );

		// we received some part of the file when reading packets,
		// they should be the first ones to be saved into the file
		// the bytes after the file belong to the next packets
		if (file_data_len > file_header->len) file_data_len = file_header->len;

		bool failed = false;
		if (file_data && file_data_len) {
			failed = file_receive_write_all (file_fd, file_data, file_data_len);
			if (failed) {
				cerver_log_error ("file_receive_actual () - write () has failed!");
				perror ("Error");
				cerver_log_line_break ();
			}
		}

		// the whole file was in the buffer, it was always an error before
		if (!failed && (file_data_len == file_header->len)) {
			retval = 0;
		}

		// there is still more data to be received
		else if (!failed) {
			size_t real_filelen = file_header->len - file_data_len;
			#ifdef FILES_DEBUG
			cerver_log_debug (
				"\nfilelen: %lu - file data len %zu = %zu\n\n",
				file_header->len, file_data_len, real_filelen
			);
			#endif
			if (!file_receive_internal (
				connection,
				real_filelen,
				file_fd
			)) {
				#ifdef FILES_DEBUG
				cerver_log_success ("file_receive_internal () has finished");
				#endif

				retval = 0;
			}

			else {
				free (*saved_filename);
				*saved_filename = NULL;
			}
		}

		(void) close (file_fd);

		if (failed) {
			free (*saved_filename);
			*saved_filename = NULL;
		}
	}

	else {
		cerver_log_error ("file_receive_actual () - failed to open file");

		free (*saved_filename);
		*saved_filename = NULL;
	}

	return retval;

}

#pragma GCC diagnostic pop

#pragma endregion
