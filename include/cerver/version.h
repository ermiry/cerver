#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#define VERSION                 "1.2.0"
#define VERSION_NAME            "Release 1.2.0"
#define VERSION_DATE			"14/04/2020"
#define VERSION_TIME			"20:55 CST"
#define VERSION_AUTHOR			"Erick Salas"

// print full cerver version information 
extern void version_print_full (void);

// print the version id
extern void version_print_version_id (void);

// print the version name
extern void version_print_version_name (void);

#endif