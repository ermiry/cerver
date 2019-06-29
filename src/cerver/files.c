#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define _XOPEN_SOURCE 700
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#include <unistd.h>

#include "cerver/network.h"
#include "cerver/packets.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

// opens a file and returns it as a FILE
FILE *file_open_as_file (const char *filename, const char *modes, struct stat *filestatus) {

    FILE *fp = NULL;

    if (filename) {
        memset (filestatus, 0, sizeof (struct stat));
        if (!stat (filename, filestatus)) 
            fp = fopen (filename, modes);

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_FILE, 
                c_string_create ("File %s not found!", filename));
            #endif
        } 
    }

    return fp;

}

// opens and reads a file into a buffer
// sets file size to the amount of bytes read
char *file_read (const char *filename, int *file_size) {

    char *file_contents = NULL;

    if (filename) {
        struct stat filestatus;
        FILE *fp = file_open_as_file (filename, "rt", &filestatus);
        if (fp) {
            *file_size = filestatus.st_size;
            file_contents = (char *) malloc (filestatus.st_size);

            // read the entire file into the buffer
            if (fread (file_contents, filestatus.st_size, 1, fp) != 1) {
                #ifdef CERVER_DEBUG
                cerver_log_msg (stderr, LOG_ERROR, LOG_FILE, 
                    c_string_create ("Failed to read file (%s) contents!"));
                #endif

                free (file_contents);
            }

            fclose (fp);
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_FILE, 
                c_string_create ("Unable to open file %s.", filename));
            #endif
        }
    }

    return file_contents;

}

// opens a file
// returns fd on success, -1 on error
int file_open_as_fd (const char *filename, struct stat *filestatus) {

    if (filename) {
        memset (filestatus, 0, sizeof (struct stat));
        if (!stat (filename, filestatus)) 
            return open (filename, 0);

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_FILE, 
                c_string_create ("File %s not found!", filename));
            #endif
        } 
    }

    return -1;      // error

}

// sends a file to the sock fd
// returns 0 on success, 1 on error
int file_send (const char *filename, int sock_fd) {

    int retval = 1;

    if (filename) {
        // try to open the file
        struct stat filestatus;
        int fd = file_open_as_fd (filename, &filestatus);
        if (fd >= 0) {
            // send a first packet with file info
            // TODO:
        }

        else {
            #ifdef CERVER_DEBUG
            cerver_log_msg (stderr, LOG_ERROR, LOG_FILE, 
                c_string_create ("Failed to open file %s.", filename));
            #endif
        }        
    }

    return retval;

}