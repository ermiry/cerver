#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#define CERVER_VERSION                  "1.3"
#define CERVER_VERSION_NAME             "Release 1.3"
#define CERVER_VERSION_DATE			    "11/05/2020"
#define CERVER_VERSION_TIME			    "15:28 CST"
#define CERVER_VERSION_AUTHOR			"Erick Salas"

// print full cerver version information 
extern void cerver_version_print_full (void);

// print the version id
extern void cerver_version_print_version_id (void);

// print the version name
extern void cerver_version_print_version_name (void);

#endif