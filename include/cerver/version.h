#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#include "cerver/config.h"

#define CERVER_VERSION				"1.6.4"
#define CERVER_VERSION_NAME			"Release 1.6.4"
#define CERVER_VERSION_DATE			"13/10/2021"
#define CERVER_VERSION_TIME			"10:58 CST"
#define CERVER_VERSION_AUTHOR		"Erick Salas"

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