#include <stdio.h>

#include "cerver/version.h"

// print full erver version information 
void cerver_version_print_full (void) {

    printf ("\nCerver Version: %s\n", CERVER_VERSION_NAME);
    printf ("Release Date & time: %s - %s\n", CERVER_VERSION_DATE, CERVER_VERSION_TIME);
    printf ("Author: %s\n\n", CERVER_VERSION_AUTHOR);

}

// print the version id
void cerver_version_print_version_id (void) {

    printf ("\nCerver Version ID: %s\n", CERVER_VERSION);

}

// print the version name
void cerver_version_print_version_name (void) {

    printf ("\nCerver Version: %s\n", CERVER_VERSION_NAME);

}