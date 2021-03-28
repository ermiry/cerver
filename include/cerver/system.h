#ifndef _CERVER_SYSTEM_H_
#define _CERVER_SYSTEM_H_

#include <stdbool.h>

#include "cerver/config.h"

#define MB		1048576
#define GB		1073741824

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FileSystemStats {

	double total;
	double available;
	double used;
	double used_percentage;

} FileSystemStats;

// get available space (bytes) in a given filesystem
// returns true on success, false on error
CERVER_PUBLIC bool system_get_available_space (
	const char *path, unsigned long *available_space
);

// get used space (bytes) in a given filesystem
// returns true on success, false on error
CERVER_PUBLIC bool system_get_used_space (
	const char *path, unsigned long *used_space
);

// fills stats with common file system values
// returns true on success, false on error
CERVER_PUBLIC bool system_get_stats (
	const char *path, FileSystemStats *stats
);

CERVER_PUBLIC void system_print_stats (
	const FileSystemStats *stats
);

#ifdef __cplusplus
}
#endif

#endif