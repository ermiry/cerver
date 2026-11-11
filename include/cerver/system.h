#ifndef _CERVER_SYSTEM_H_
#define _CERVER_SYSTEM_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/types/types.h"

#define MB		1048576
#define GB		1073741824

#define SYSTEM_LOADS					3
#define SYSTEM_HOSTNAME_SIZE			256

#ifdef __cplusplus
extern "C" {
#endif

#pragma region file system

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

#pragma endregion

#pragma region memory

// the memory of the host in bytes, from /proc/meminfo
typedef struct SystemMemory {

	u64 total;
	u64 free;
	u64 available;		// the memory that can be used without swapping
	u64 swap_total;
	u64 swap_free;

} SystemMemory;

// fills memory with the values of the host
// returns true on success, false on error
CERVER_PUBLIC bool system_get_memory (SystemMemory *memory);

CERVER_PUBLIC void system_print_memory (const SystemMemory *memory);

#pragma endregion

#pragma region load

// the load of the host, from sysinfo ()
typedef struct SystemLoad {

	double loads[SYSTEM_LOADS];		// the load averages of 1, 5 and 15 minutes
	u64 uptime;						// seconds since the host started
	unsigned int processes;			// the processes of the host
	unsigned int cpus;				// the cpus that are online

} SystemLoad;

// fills load with the values of the host
// returns true on success, false on error
CERVER_PUBLIC bool system_get_load (SystemLoad *load);

CERVER_PUBLIC void system_print_load (const SystemLoad *load);

#pragma endregion

#pragma region cpu

// the cpu times of the host since it started, from /proc/stat,
// the usage between two samples is the busy time over the total time
typedef struct SystemCpuSample {

	u64 busy;
	u64 total;

} SystemCpuSample;

// takes a sample of the cpu times of the host
// returns true on success, false on error
CERVER_PUBLIC bool system_cpu_sample (SystemCpuSample *sample);

// returns the percentage (0 to 100) of the cpu that was used between two samples,
// 0 if the samples are equal or the second one is older
CERVER_PUBLIC double system_cpu_usage (
	const SystemCpuSample *first, const SystemCpuSample *second
);

#pragma endregion

#pragma region host

// copies the name of the host in hostname (up to size bytes, with the NUL)
// returns true on success, false on error or if the name does not fit, leaving hostname empty
CERVER_PUBLIC bool system_get_hostname (char *hostname, const size_t size);

#pragma endregion

#pragma region process

// the resources of this process, from /proc/self
typedef struct SystemProcess {

	u64 resident;					// the resident memory in bytes
	unsigned int threads;
	unsigned int open_files;		// the open file descriptors

} SystemProcess;

// fills process with the values of this process
// returns true on success, false on error
CERVER_PUBLIC bool system_get_process (SystemProcess *process);

CERVER_PUBLIC void system_print_process (const SystemProcess *process);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
