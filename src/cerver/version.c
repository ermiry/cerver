#include <stdio.h>

#include "cerver/version.h"

// print full erver version information 
void cerver_version_print_full (void) {

    printf ("\nCerver Version: %s\n", VERSION_NAME);
    printf ("Release Date & time: %s - %s\n", VERSION_DATE, VERSION_TIME);
    printf ("Author: %s\n\n", VERSION_AUTHOR);

}

// print the version id
void cerver_version_print_version_id (void) {

    printf ("\nCerver Version ID: %s\n", VERSION);

}

// print the version name
void cerver_version_print_version_name (void) {

    printf ("\nCerver Version: %s\n", VERSION_NAME);

}