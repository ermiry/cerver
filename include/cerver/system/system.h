/**
 * @file cerver/system/system.h
 * @brief The resources of the host and of the process: the file systems, the memory, the load, the cpu, the name
 * of the host and the process.
 */

#ifndef _CERVER_SYSTEM_H_
#define _CERVER_SYSTEM_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup system System
 * @brief The resources of the host and of the process, the timers and the input of the terminal.
 */

/**
 * @defgroup system_sizes Sizes
 * @ingroup system
 * @brief The sizes of the system values.
 * @{
 */

/** @brief The bytes of a megabyte. */
#define MB		1048576

/** @brief The bytes of a gigabyte. */
#define GB		1073741824

/** @brief The load averages of the host: of 1, 5 and 15 minutes. */
#define SYSTEM_LOADS					3

/** @brief A buffer for the name of the host, with its NUL. */
#define SYSTEM_HOSTNAME_SIZE			256

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup system_file_system File system
 * @ingroup system
 * @brief The space of a file system, with statvfs ().
 * @{
 */

/**
 * @brief The space of a file system, in gigabytes.
 */
typedef struct FileSystemStats {

	/** @brief The size of the file system. */
	double total;
	/** @brief The free space, with the blocks reserved for root. */
	double available;
	/** @brief The used space. */
	double used;
	/** @brief The used space over the size, from 0 to 100. */
	double used_percentage;

} FileSystemStats;

/**
 * @brief Gets the space in bytes that an unprivileged user can use in the file system of a path.
 *
 * @param[in] path a path of the file system
 * @param[out] available_space the bytes, it must not be NULL
 * @return true on success, false if statvfs () failed
 */
CERVER_PUBLIC bool system_get_available_space (
	const char *path, unsigned long *available_space
);

/**
 * @brief Gets the used space in bytes of the file system of a path: its size minus its free blocks.
 *
 * @param[in] path a path of the file system
 * @param[out] used_space the bytes, it must not be NULL
 * @return true on success, false if statvfs () failed
 */
CERVER_PUBLIC bool system_get_used_space (
	const char *path, unsigned long *used_space
);

/**
 * @brief Fills the stats of the file system of a path, in gigabytes.
 *
 * @param[in] path a path of the file system
 * @param[out] stats the stats, it must not be NULL
 * @return true on success, false if statvfs () failed
 */
CERVER_PUBLIC bool system_get_stats (
	const char *path, FileSystemStats *stats
);

/**
 * @brief Prints the stats of a file system to stdout.
 *
 * @param[in] stats the stats, NULL prints nothing
 */
CERVER_PUBLIC void system_print_stats (
	const FileSystemStats *stats
);

/** @} */

/**
 * @defgroup system_memory Memory
 * @ingroup system
 * @brief The memory of the host, from /proc/meminfo.
 * @{
 */

/**
 * @brief The memory of the host in bytes, from /proc/meminfo.
 */
typedef struct SystemMemory {

	/** @brief The total memory. */
	u64 total;
	/** @brief The free memory. */
	u64 free;
	/** @brief The memory that can be used without swapping. */
	u64 available;
	/** @brief The total swap. */
	u64 swap_total;
	/** @brief The free swap. */
	u64 swap_free;

} SystemMemory;

/**
 * @brief Fills the memory of the host.
 *
 * @param[out] memory the memory
 * @return true on success, false for a NULL argument or if /proc/meminfo could not be read
 */
CERVER_PUBLIC bool system_get_memory (SystemMemory *memory);

/**
 * @brief Prints the memory of the host to stdout.
 *
 * @param[in] memory the memory, NULL prints nothing
 */
CERVER_PUBLIC void system_print_memory (const SystemMemory *memory);

/** @} */

/**
 * @defgroup system_load Load
 * @ingroup system
 * @brief The load of the host, from sysinfo ().
 * @{
 */

/**
 * @brief The load of the host, from sysinfo ().
 */
typedef struct SystemLoad {

	/** @brief The load averages of 1, 5 and 15 minutes. */
	double loads[SYSTEM_LOADS];
	/** @brief The seconds since the host started. */
	u64 uptime;
	/** @brief The processes of the host. */
	unsigned int processes;
	/** @brief The cpus that are online. */
	unsigned int cpus;

} SystemLoad;

/**
 * @brief Fills the load of the host.
 *
 * @param[out] load the load
 * @return true on success, false for a NULL argument or if sysinfo () failed
 */
CERVER_PUBLIC bool system_get_load (SystemLoad *load);

/**
 * @brief Prints the load of the host to stdout.
 *
 * @param[in] load the load, NULL prints nothing
 */
CERVER_PUBLIC void system_print_load (const SystemLoad *load);

/** @} */

/**
 * @defgroup system_cpu Cpu
 * @ingroup system
 * @brief The usage of the cpu of the host, from /proc/stat.
 * @{
 */

/**
 * @brief The cpu times of the host since it started, from /proc/stat; the usage between two samples is the busy
 * time over the total time.
 */
typedef struct SystemCpuSample {

	/** @brief The busy time: user, nice, system, interrupts, soft interrupts and steal. */
	u64 busy;
	/** @brief The busy time plus the idle and the wait times. */
	u64 total;

} SystemCpuSample;

/**
 * @brief Takes a sample of the cpu times of the host.
 *
 * @param[out] sample the sample
 * @return true on success, false for a NULL argument or if /proc/stat could not be read
 */
CERVER_PUBLIC bool system_cpu_sample (SystemCpuSample *sample);

/**
 * @brief Returns the percentage of the cpu that was used between two samples.
 *
 * @param[in] first the first sample
 * @param[in] second the second sample
 * @return from 0 to 100; 0 for a NULL sample, equal samples or a second one that is older
 */
CERVER_PUBLIC double system_cpu_usage (
	const SystemCpuSample *first, const SystemCpuSample *second
);

/** @} */

/**
 * @defgroup system_host Host
 * @ingroup system
 * @brief The name of the host.
 * @{
 */

/**
 * @brief Copies the name of the host.
 *
 * @param[out] hostname the buffer, see #SYSTEM_HOSTNAME_SIZE
 * @param[in] size the size of the buffer, with the NUL
 * @return true on success; false for a NULL buffer, a size of 0, or a name that does not fit, leaving the buffer
 * empty
 */
CERVER_PUBLIC bool system_get_hostname (char *hostname, const size_t size);

/** @} */

/**
 * @defgroup system_process Process
 * @ingroup system
 * @brief The resources of this process, from /proc/self.
 * @{
 */

/**
 * @brief The resources of this process, from /proc/self.
 */
typedef struct SystemProcess {

	/** @brief The resident memory in bytes. */
	u64 resident;
	/** @brief The threads of the process. */
	unsigned int threads;
	/** @brief The open file descriptors. */
	unsigned int open_files;

} SystemProcess;

/**
 * @brief Fills the resources of this process.
 *
 * @param[out] process the resources
 * @return true on success, false for a NULL argument or if /proc/self could not be read
 */
CERVER_PUBLIC bool system_get_process (SystemProcess *process);

/**
 * @brief Prints the resources of this process to stdout.
 *
 * @param[in] process the resources, NULL prints nothing
 */
CERVER_PUBLIC void system_print_process (const SystemProcess *process);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
