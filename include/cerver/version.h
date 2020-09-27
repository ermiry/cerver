#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#include "cerver/config.h"

#define CERVER_VERSION                  "1.6.1rc-1"
#define CERVER_VERSION_NAME             "Release 1.6.1rc-1"
#define CERVER_VERSION_DATE			    "26/10/2020"
#define CERVER_VERSION_TIME			    "23:34 CST"
#define CERVER_VERSION_AUTHOR			"Erick Salas"

// print full cerver version information 
CERVER_EXPORT void cerver_version_print_full (void);

// print the version id
CERVER_EXPORT void cerver_version_print_version_id (void);

// print the version name
CERVER_EXPORT void cerver_version_print_version_name (void);

#endif