#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <unistd.h>

#define _XOPEN_SOURCE 700
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/files.h"
#include "cerver/network.h"
#include "cerver/packets.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"
#include "cerver/utils/json.h"

bool file_exists (const char *filename);

#pragma region cerver

FileCerver *file_cerver_new (void) {

	FileCerver *file_cerver = (FileCerver *) malloc (sizeof (FileCerver));
	if (file_cerver) {
		file_cerver->cerver = NULL;

		file_cerver->n_paths = 0;
		for (unsigned int i = 0; i < FILE_CERVER_MAX_PATHS; i++)
			file_cerver->paths[i] = NULL;

		file_cerver->uploads_path = NULL;

		file_cerver->n_files_requests = 0;
		file_cerver->n_success_files_requests = 0;
		file_cerver->n_bad_files_requests = 0;

		file_cerver->n_files_uploaded = 0;
		file_cerver->n_success_files_uploaded = 0;
		file_cerver->n_bad_files_uploaded = 0;
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

		free (file_cerver_ptr);
	}

}

FileCerver *file_cerver_create (Cerver *cerver) {

	FileCerver *file_cerver = file_cerver_new ();
	if (file_cerver) {
		file_cerver->cerver = cerver;
	}

	return file_cerver;

}

// adds a new file path to take into account when a client request for a file
// returns 0 on success, 1 on error
u8 file_cerver_add_path (FileCerver *file_cerver, const char *path) {

	u8 retval = 1;

	if (file_cerver && path) {
		if (file_cerver->n_paths < FILE_CERVER_MAX_PATHS) {
			file_cerver->paths[file_cerver->n_paths] = str_new (path);
			file_cerver->n_paths += 1;
		}
	}

	return retval;

}

// sets the default uploads path to be used when a client sends a file
void file_cerver_set_uploads_path (FileCerver *file_cerver, const char *uploads_path) {

	if (file_cerver && uploads_path) {
		str_delete (file_cerver->uploads_path);
		file_cerver->uploads_path = str_new (uploads_path);
	}

}

// search for the requested file in the configured paths
// returns the actual filename (path + directory) where it was found, NULL on error
String *file_cerver_search_file (FileCerver *file_cerver, const char *filename) {

	String *retval = NULL;

	if (file_cerver && filename) {
		char filename_query[DEFAULT_FILENAME_LEN * 2] = { 0 };
		for (unsigned int i = 0; i < file_cerver->n_paths; i++) {
			(void) snprintf (
				filename_query, DEFAULT_FILENAME_LEN * 2,
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

void file_cerver_stats_print (FileCerver *file_cerver) {

	if (file_cerver) {
		printf ("Files requests:                %ld\n", file_cerver->n_files_requests);
		printf ("Success requests:              %ld\n", file_cerver->n_success_files_requests);
		printf ("Bad requests:                  %ld\n\n", file_cerver->n_bad_files_requests);
		printf ("Files bytes sent:              %ld\n\n", file_cerver->n_bytes_sent);

		printf ("Files uploads:                 %ld\n", file_cerver->n_files_uploaded);
		printf ("Success uploads:               %ld\n", file_cerver->n_success_files_uploaded);
		printf ("Bad uploads:                   %ld\n", file_cerver->n_bad_files_uploaded);
		printf ("Files bytes received:          %ld\n\n", file_cerver->n_bytes_received);
	}

}

#pragma endregion

#pragma region main

// check if a directory already exists, and if not, creates it
// returns 0 on success, 1 on error
unsigned int files_create_dir (const char *dir_path, mode_t mode) {

	unsigned int retval = 1;

	if (dir_path) {
		struct stat st = { 0 };
		int ret = stat (dir_path, &st);
		switch (ret) {
			case -1: {
				if (!mkdir (dir_path, mode)) {
					retval = 0;		// success
				}

				else {
					char *s = c_string_create ("Failed to create dir %s!", dir_path);
					if (s) {
						cerver_log_error (s);
						free (s);
					}
				}
			} break;
			case 0: {
				char *s = c_string_create ("Dir %s already exists!", dir_path);
				if (s) {
					cerver_log_warning (s);
					free (s);
				}
			} break;

			default: break;
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
					memcpy (retval, ptr + 1, ext_len);
					retval[ext_len] = '\0';
				}
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

					dlist_insert_after (images, dlist_end (images), file);
				}
			}

			(void) closedir (dp);
		}

		else {
			char *status = c_string_create ("Failed to open dir %s", dir);
			if (status) {
				cerver_log_error (status);
				free (status);
			}
		}
	}

	return images;

}

static String *file_get_line (FILE *file) {

	String *str = NULL;

	if (file) {
		if (!feof (file)) {
			char line[1024] = { 0 };
			if (fgets (line, 1024, file)) {
				size_t curr = strlen(line);
				if(line[curr - 1] == '\n') line[curr - 1] = '\0';

				str = str_new (line);
			}
		}
	}

	return str;

}

// reads eachone of the file's lines into a newly created string and returns them inside a dlist
DoubleList *file_get_lines (const char *filename) {

	DoubleList *lines = NULL;

	if (filename) {
		FILE *file = fopen (filename, "r");
		if (file) {
			lines = dlist_init (str_delete, str_comparator);

			String *line = NULL;
			while ((line = file_get_line (file))) {
				dlist_insert_after (lines, dlist_end (lines), line);
			}

			fclose (file);
		}

		else {
			char *status = c_string_create ("Failed to open file: %s", filename);
			if (status) {
				cerver_log_error (status);
				free (status);
			}
		}
	}

	return lines;

}

// returns true if the filename exists
bool file_exists (const char *filename) {

	bool retval = false;

	if (filename) {
		struct stat filestatus = { 0 };
		if (!stat (filename, &filestatus)) {
			retval = true;
		}
	}

	return retval;

}

// opens a file and returns it as a FILE
FILE *file_open_as_file (const char *filename, const char *modes, struct stat *filestatus) {

	FILE *fp = NULL;

	if (filename) {
		memset (filestatus, 0, sizeof (struct stat));
		if (!stat (filename, filestatus))
			fp = fopen (filename, modes);

		else {
			#ifdef CERVER_DEBUG
			char *s = c_string_create ("File %s not found!", filename);
			if (s) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_FILE, s);
				free (s);
			}
			#endif
		}
	}

	return fp;

}

// opens and reads a file into a buffer
// sets file size to the amount of bytes read
char *file_read (const char *filename, size_t *file_size) {

	char *file_contents = NULL;

	if (filename) {
		struct stat filestatus = { 0 };
		FILE *fp = file_open_as_file (filename, "rt", &filestatus);
		if (fp) {
			*file_size = filestatus.st_size;
			file_contents = (char *) malloc (filestatus.st_size);

			// read the entire file into the buffer
			if (fread (file_contents, filestatus.st_size, 1, fp) != 1) {
				#ifdef CERVER_DEBUG
				char *s = c_string_create ("Failed to read file (%s) contents!");
				if (s) {
					cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_FILE, s);
					free (s);
				}
				#endif

				free (file_contents);
			}

			fclose (fp);
		}

		else {
			#ifdef CERVER_DEBUG
			char *s = c_string_create ("Unable to open file %s.", filename);
			if (s) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_FILE, s);
				free (s);
			}
			#endif
		}
	}

	return file_contents;

}

// opens a file with the required flags
// returns fd on success, -1 on error
int file_open_as_fd (const char *filename, struct stat *filestatus, int flags) {

	int retval = -1;

	if (filename) {
		memset (filestatus, 0, sizeof (struct stat));
		if (!stat (filename, filestatus)) {
			retval = open (filename, flags);
		}
	}

	return retval;

}

json_value *file_json_parse (const char *filename) {

	json_value *value = NULL;

	if (filename) {
		size_t file_size = 0;
		char *file_contents = file_read (filename, &file_size);
		json_char *json = (json_char *) file_contents;
		value = json_parse (json, file_size);

		free (file_contents);
	}

	return value;

}

#pragma endregion

#pragma region send

// sends a file to the sock fd
// returns 0 on success, 1 on error
int file_send (const char *filename, int sock_fd) {

	int retval = 1;

	if (filename) {
		// try to open the file
		struct stat filestatus;
		int fd = file_open_as_fd (filename, &filestatus, O_RDONLY);
		if (fd >= 0) {
			// send a first packet with file info
			// TODO:
		}

		else {
			#ifdef CERVER_DEBUG
			char *s = c_string_create ("Failed to open file %s.", filename);
			if (s) {
				cerver_log_msg (stderr, LOG_TYPE_ERROR, LOG_TYPE_FILE, s);
				free (s);
			}
			#endif
		}
	}

	return retval;

}

#pragma endregion