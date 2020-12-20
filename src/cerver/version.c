#include "cerver/version.h"

#include "cerver/utils/log.h"

// print full erver version information
void cerver_version_print_full (void) {

	cerver_log_msg ("\nCerver Version: %s", CERVER_VERSION_NAME);
	cerver_log_msg ("Release Date & time: %s - %s", CERVER_VERSION_DATE, CERVER_VERSION_TIME);
	cerver_log_msg ("Author: %s\n", CERVER_VERSION_AUTHOR);

}

// print the version id
void cerver_version_print_version_id (void) {

	cerver_log_msg ("\nCerver Version ID: %s\n", CERVER_VERSION);

}

// print the version name
void cerver_version_print_version_name (void) {

	cerver_log_msg ("\nCerver Version: %s\n", CERVER_VERSION_NAME);

}