#include <stdio.h>
#include <stdbool.h>

#include <sys/statvfs.h>

#include "cerver/system.h"

// get available space (bytes) in a given filesystem
// returns true on success, false on error
bool system_get_available_space (
	const char *path, unsigned long *available_space
) {

	bool retval = false;

	struct statvfs stat = { 0 };

	if (!statvfs (path, &stat)) {
		*available_space = stat.f_bsize * stat.f_bavail;
		retval = true;
	}

	return retval;

}

// get used space (bytes) in a given filesystem
// returns true on success, false on error
bool system_get_used_space (
	const char *path, unsigned long *used_space
) {

	bool retval = false;

	struct statvfs stat = { 0 };

	if (!statvfs (path, &stat)) {
		unsigned long total = stat.f_blocks * stat.f_frsize;
		unsigned long available = stat.f_bfree * stat.f_frsize;
		*used_space = total - available;

		retval = true;
	}

	return retval;

}

// fills stats with common file system values
// returns true on success, false on error
bool system_get_stats (
	const char *path, FileSystemStats *stats
) {

	bool retval = false;

	struct statvfs buffer = { 0 };

	if (!statvfs (path, &buffer)) {
		stats->total = (double) (buffer.f_blocks * buffer.f_frsize) / GB;
		stats->available = (double) (buffer.f_bfree * buffer.f_frsize) / GB;
		stats->used = stats->total - stats->available;
		stats->used_percentage = (double) (stats->used / stats->total) * (double) 100;

		retval = true;
	}

	return retval;

}

void system_print_stats (
	const FileSystemStats *stats
) {

	if (stats) {
		(void) printf ("Total: %f\n", stats->total);
		(void) printf ("Available: %f\n", stats->available);
		(void) printf ("Used: %f\n", stats->used);
		(void) printf ("Used Percentage: %f\n", stats->used_percentage);
	}

}
