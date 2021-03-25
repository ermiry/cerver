#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#include "cerver/config.h"

#define CERVER_VERSION					"2.0b-32"
#define CERVER_VERSION_NAME				"Beta 2.0b-32"
#define CERVER_VERSION_DATE				"25/03/2021"
#define CERVER_VERSION_TIME				"13:48 CST"
#define CERVER_VERSION_AUTHOR			"Erick Salas"

#ifdef __cplusplus
extern "C" {
#endif

// print full cerver version information 
CERVER_EXPORT void cerver_version_print_full (void);

// print the version id
CERVER_EXPORT void cerver_version_print_version_id (void);

// print the version name
CERVER_EXPORT void cerver_version_print_version_name (void);

#ifdef __cplusplus
}
#endif

#endif