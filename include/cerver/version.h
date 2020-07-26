#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#define CERVER_VERSION                  "1.5rc"
#define CERVER_VERSION_NAME             "Release 1.5rc"
#define CERVER_VERSION_DATE			    "25/07/2020"
#define CERVER_VERSION_TIME			    "17:46 CST"
#define CERVER_VERSION_AUTHOR			"Erick Salas"

// print full cerver version information 
extern void cerver_version_print_full (void);

// print the version id
extern void cerver_version_print_version_id (void);

// print the version name
extern void cerver_version_print_version_name (void);

#endif