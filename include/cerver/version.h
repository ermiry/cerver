/**
 * @file cerver/version.h
 * @brief The version of the cerver library that was compiled.
 */

#ifndef _CERVER_VERSION_H_
#define _CERVER_VERSION_H_

#include "cerver/config.h"

/**
 * @defgroup version Version
 * @brief The values of the version and the functions that print them.
 * @{
 */

/** @brief The id of the version, like `2.1b-14` or `1.8.2`. */
#define CERVER_VERSION				"2.1b-24"

/** @brief The name of the version, like `Beta 2.1b-14` or `Release 1.8.2`. */
#define CERVER_VERSION_NAME			"Beta 2.1b-24"

/** @brief The date of the version, as `dd/mm/yyyy`. */
#define CERVER_VERSION_DATE			"18/11/2026"

/** @brief The time of the version, as `hh:mm` with its time zone. */
#define CERVER_VERSION_TIME			"10:13 CST"

/** @brief The author of the version. */
#define CERVER_VERSION_AUTHOR		"Erick Salas"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prints the name, the date and the time, and the author of the version.
 *
 * It logs with cerver_log_both (), so the lines go to stdout and to the log file, ignoring the quiet flag.
 */
CERVER_EXPORT void cerver_version_print_full (void);

/**
 * @brief Prints the id of the version (#CERVER_VERSION).
 *
 * It logs with cerver_log_both (), so the line goes to stdout and to the log file, ignoring the quiet flag.
 */
CERVER_EXPORT void cerver_version_print_version_id (void);

/**
 * @brief Prints the name of the version (#CERVER_VERSION_NAME).
 *
 * It logs with cerver_log_both (), so the line goes to stdout and to the log file, ignoring the quiet flag.
 */
CERVER_EXPORT void cerver_version_print_version_name (void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
