#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#define VERSION                 "1.2.3"
#define VERSION_NAME            "Release 1.2.3"
#define VERSION_DATE			"07/05/2020"
#define VERSION_TIME			"19:44 CST"
#define VERSION_AUTHOR			"Erick Salas"

// print full cerver version information 
extern void cerver_version_print_full (void);

// print the version id
extern void cerver_version_print_version_id (void);

// print the version name
extern void cerver_version_print_version_name (void);

#endif