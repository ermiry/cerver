#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <dirent.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>
#include <unistd.h>

#include "cerver/types/types.h"

#include "cerver/system/system.h"

#define SYSTEM_MEMINFO_PATH				"/proc/meminfo"
#define SYSTEM_STAT_PATH				"/proc/stat"
#define SYSTEM_PROCESS_STATUS_PATH		"/proc/self/status"
#define SYSTEM_PROCESS_FILES_PATH		"/proc/self/fd"

#define SYSTEM_LINE_SIZE				256

#define SYSTEM_KILOBYTE					1024
#define SYSTEM_UNIT						1

#define SYSTEM_CPU_FIELDS				8
#define SYSTEM_PERCENTAGE				100.0

#define SYSTEM_MEMORY_VALUES			5
#define SYSTEM_PROCESS_VALUES			2

// a "Name: value" line of a file of /proc
typedef struct SystemValue {

	const char *name;
	u64 *value;
	u64 unit;
	bool found;

} SystemValue;

#pragma region file system

// get available space (bytes) in a given filesystem
// returns true on success, false on error
bool system_get_available_space (
	const char *path, unsigned long *available_space
) {

	bool retval = false;

	struct statvfs stat = CERVER_ZERO_INITIALIZER;

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

	struct statvfs stat = CERVER_ZERO_INITIALIZER;

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

	struct statvfs buffer = CERVER_ZERO_INITIALIZER;

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

#pragma endregion

#pragma region values

// returns 1 if the line has one of the values that were not found yet, 0 if not
static unsigned int system_line_value (
	const char *line, SystemValue *values, const unsigned int n_values
) {

	unsigned int found = 0;

	for (unsigned int index = 0; (index < n_values) && !found; index++) {
		SystemValue *value = &values[index];
		const size_t length = strlen (value->name);

		unsigned long long number = 0;
		if (
			!value->found
			&& !strncmp (line, value->name, length) && (line[length] == ':')
			&& (sscanf (line + length + 1, "%llu", &number) == 1)
		) {
			*value->value = (u64) number * value->unit;
			value->found = true;
			found = 1;
		}
	}

	return found;

}

// reads the "Name: value" lines of a file of /proc
// returns true when every value was found, false if not
static bool system_read_values (
	const char *path, SystemValue *values, const unsigned int n_values
) {

	unsigned int found = 0;

	FILE *file = fopen (path, "r");
	if (file) {
		char line[SYSTEM_LINE_SIZE] = { 0 };
		while ((found < n_values) && fgets (line, SYSTEM_LINE_SIZE, file)) {
			found += system_line_value (line, values, n_values);
		}

		(void) fclose (file);
	}

	return (found == n_values);

}

#pragma endregion

#pragma region memory

// fills memory with the values of the host
// returns true on success, false on error
bool system_get_memory (SystemMemory *memory) {

	bool retval = false;

	if (memory) {
		(void) memset (memory, 0, sizeof (SystemMemory));

		SystemValue values[SYSTEM_MEMORY_VALUES] = {
			{ "MemTotal", &memory->total, SYSTEM_KILOBYTE, false },
			{ "MemFree", &memory->free, SYSTEM_KILOBYTE, false },
			{ "MemAvailable", &memory->available, SYSTEM_KILOBYTE, false },
			{ "SwapTotal", &memory->swap_total, SYSTEM_KILOBYTE, false },
			{ "SwapFree", &memory->swap_free, SYSTEM_KILOBYTE, false }
		};

		retval = system_read_values (SYSTEM_MEMINFO_PATH, values, SYSTEM_MEMORY_VALUES);
	}

	return retval;

}

void system_print_memory (const SystemMemory *memory) {

	if (memory) {
		(void) printf ("Memory total: %lu\n", memory->total);
		(void) printf ("Memory free: %lu\n", memory->free);
		(void) printf ("Memory available: %lu\n", memory->available);
		(void) printf ("Swap total: %lu\n", memory->swap_total);
		(void) printf ("Swap free: %lu\n", memory->swap_free);
	}

}

#pragma endregion

#pragma region load

// fills load with the values of the host
// returns true on success, false on error
bool system_get_load (SystemLoad *load) {

	bool retval = false;

	struct sysinfo info;
	(void) memset (&info, 0, sizeof (struct sysinfo));

	if (load && !sysinfo (&info)) {
		for (unsigned int index = 0; index < SYSTEM_LOADS; index++) {
			load->loads[index] = (double) info.loads[index] / (double) (1 << SI_LOAD_SHIFT);
		}

		load->uptime = (u64) info.uptime;
		load->processes = (unsigned int) info.procs;

		const long cpus = sysconf (_SC_NPROCESSORS_ONLN);
		load->cpus = (cpus > 0) ? (unsigned int) cpus : 0;

		retval = true;
	}

	return retval;

}

void system_print_load (const SystemLoad *load) {

	if (load) {
		(void) printf ("Load average:");
		for (unsigned int index = 0; index < SYSTEM_LOADS; index++) {
			(void) printf (" %.2f", load->loads[index]);
		}

		(void) printf ("\nUptime: %lu\n", load->uptime);
		(void) printf ("Processes: %u\n", load->processes);
		(void) printf ("Cpus: %u\n", load->cpus);
	}

}

#pragma endregion

#pragma region cpu

// takes a sample of the cpu times of the host
// returns true on success, false on error
bool system_cpu_sample (SystemCpuSample *sample) {

	bool retval = false;

	FILE *file = sample ? fopen (SYSTEM_STAT_PATH, "r") : NULL;
	if (file) {
		unsigned long long user = 0, nice = 0, kernel = 0, idle = 0;
		unsigned long long wait = 0, interrupts = 0, soft_interrupts = 0, steal = 0;

		if (fscanf (
			file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
			&user, &nice, &kernel, &idle, &wait, &interrupts, &soft_interrupts, &steal
		) == SYSTEM_CPU_FIELDS) {
			sample->busy = (u64) (user + nice + kernel + interrupts + soft_interrupts + steal);
			sample->total = sample->busy + (u64) (idle + wait);

			retval = true;
		}

		(void) fclose (file);
	}

	return retval;

}

// returns the percentage (0 to 100) of the cpu that was used between two samples,
// 0 if the samples are equal or the second one is older
double system_cpu_usage (
	const SystemCpuSample *first, const SystemCpuSample *second
) {

	double usage = 0;

	if (
		first && second
		&& (second->total > first->total) && (second->busy >= first->busy)
	) {
		const u64 busy = second->busy - first->busy;
		const u64 total = second->total - first->total;

		usage = (busy < total)
			? ((double) busy / (double) total) * SYSTEM_PERCENTAGE
			: SYSTEM_PERCENTAGE;
	}

	return usage;

}

#pragma endregion

#pragma region host

// copies the name of the host in hostname (up to size bytes, with the NUL)
// returns true on success, false on error
bool system_get_hostname (char *hostname, const size_t size) {

	bool retval = false;

	if (hostname && size) {
		if (!gethostname (hostname, size)) {
			hostname[size - 1] = '\0';
			retval = true;
		}

		// a name that does not fit is an error, and the buffer is left empty
		else {
			hostname[0] = '\0';
		}
	}

	return retval;

}

#pragma endregion

#pragma region process

// counts the open file descriptors of this process,
// without the one that reads its directory
static bool system_count_open_files (unsigned int *open_files) {

	bool retval = false;

	DIR *directory = opendir (SYSTEM_PROCESS_FILES_PATH);
	if (directory) {
		unsigned int count = 0;

		const struct dirent *entry = NULL;
		while ((entry = readdir (directory))) {
			if (entry->d_name[0] != '.') count += 1;
		}

		*open_files = count ? (count - 1) : 0;

		(void) closedir (directory);

		retval = true;
	}

	return retval;

}

// fills process with the values of this process
// returns true on success, false on error
bool system_get_process (SystemProcess *process) {

	bool retval = false;

	if (process) {
		(void) memset (process, 0, sizeof (SystemProcess));

		u64 threads = 0;
		SystemValue values[SYSTEM_PROCESS_VALUES] = {
			{ "VmRSS", &process->resident, SYSTEM_KILOBYTE, false },
			{ "Threads", &threads, SYSTEM_UNIT, false }
		};

		if (system_read_values (SYSTEM_PROCESS_STATUS_PATH, values, SYSTEM_PROCESS_VALUES)) {
			process->threads = (unsigned int) threads;

			retval = system_count_open_files (&process->open_files);
		}
	}

	return retval;

}

void system_print_process (const SystemProcess *process) {

	if (process) {
		(void) printf ("Process resident memory: %lu\n", process->resident);
		(void) printf ("Process threads: %u\n", process->threads);
		(void) printf ("Process open files: %u\n", process->open_files);
	}

}

#pragma endregion
