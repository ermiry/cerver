/**
 * @file cerver/utils/log.h
 * @brief The log of cerver: its types, its configuration and the functions that print the messages.
 */

#ifndef _CERVER_UTILS_LOG_H_
#define _CERVER_UTILS_LOG_H_

#include <stdbool.h>
#include <stdio.h>

#include "cerver/config.h"

/**
 * @defgroup utils_log Log
 * @ingroup utils
 * @brief The messages of cerver and of the application, to stdout, stderr and a log file.
 *
 * The log is created by cerver_init () (cerver_log_init ()) and released by cerver_end (): a message printed before
 * it, or after it, is not printed.
 * @{
 */

/** @brief The default directory of the log files. */
#define LOG_DEFAULT_PATH		"/var/log/cerver"

/** @brief The mode of the directory that cerver_log_set_path () creates. */
#define LOG_PATH_MODE			0755

/** @brief The size of the path of a log file. */
#define LOG_FILENAME_SIZE		1024

/** @brief The messages that the pool of the log has at the start, it grows when it is empty. */
#define LOG_POOL_INIT			32

/** @brief The size of the date and time of a message. */
#define LOG_DATETIME_SIZE		32

/** @brief The size of the header of a message, with its two types. */
#define LOG_HEADER_SIZE			32

/** @brief The size of each half of the header. */
#define LOG_HEADER_HALF_SIZE	LOG_HEADER_SIZE / 2

/** @brief The size of a message; a longer one is truncated. */
#define LOG_MESSAGE_SIZE		4096

/** @brief The red of the error messages. */
#define LOG_COLOR_RED       "\x1b[31m"
/** @brief The green of the success messages. */
#define LOG_COLOR_GREEN     "\x1b[32m"
/** @brief The yellow of the warning messages. */
#define LOG_COLOR_YELLOW    "\x1b[33m"
/** @brief The blue of the cerver messages. */
#define LOG_COLOR_BLUE      "\x1b[34m"
/** @brief The magenta of the debug and event messages. */
#define LOG_COLOR_MAGENTA   "\x1b[35m"
/** @brief The cyan of the test messages. */
#define LOG_COLOR_CYAN      "\x1b[36m"
/** @brief Resets the color. */
#define LOG_COLOR_RESET     "\x1b[0m"

/** @brief The default seconds between two flushes of the log file. */
#define LOG_DEFAULT_UPDATE_INTERVAL			1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The X-macro table of the types of a message: the number, the name and the text of the header.
 */
#define LOG_TYPE_MAP(XX)						\
	XX(0, 	NONE, 		[NONE])					\
	XX(1, 	ERROR, 		[ERROR])				\
	XX(2, 	WARNING, 	[WARNING])				\
	XX(3, 	SUCCESS, 	[SUCCESS])				\
	XX(4, 	DEBUG, 		[DEBUG])				\
	XX(5, 	TEST, 		[TEST])					\
	XX(6, 	CERVER, 	[CERVER])				\
	XX(7, 	CLIENT, 	[CLIENT])				\
	XX(8, 	CONNECTION, [CONNECTION])			\
	XX(9, 	HANDLER, 	[HANDLER])				\
	XX(10, 	ADMIN, 		[ADMIN])				\
	XX(11, 	EVENT, 		[EVENT])				\
	XX(12, 	PACKET, 	[PACKET])				\
	XX(13, 	REQ, 		[REQ])					\
	XX(14, 	FILE, 		[FILE])					\
	XX(15, 	HTTP, 		[HTTP])					\
	XX(16, 	GAME, 		[GAME])					\
	XX(17, 	PLAYER, 	[PLAYER])

/**
 * @brief The type of a message, one `LOG_TYPE_<name>` for each row of #LOG_TYPE_MAP.
 *
 * The first type of a message sets its stream (stderr for the errors and the warnings) and its color.
 */
typedef enum LogType {

	#define XX(num, name, string) LOG_TYPE_##name = num,
	LOG_TYPE_MAP (XX)
	#undef XX

} LogType;

/**
 * @brief Where the messages are printed.
 */
typedef enum LogOutputType {

	/** @brief Nowhere. */
	LOG_OUTPUT_TYPE_NONE		= 0,
	/** @brief To stdout and stderr (the default). */
	LOG_OUTPUT_TYPE_STD			= 1,
	/** @brief To the log file. */
	LOG_OUTPUT_TYPE_FILE		= 2,
	/** @brief To stdout and stderr, and to the log file. */
	LOG_OUTPUT_TYPE_BOTH		= 3

} LogOutputType;

/**
 * @brief Returns where the messages are printed.
 *
 * @return the output type
 */
CERVER_EXPORT LogOutputType cerver_log_get_output_type (void);

/**
 * @brief Sets where the messages are printed.
 *
 * @param[in] type the output type, the default is LOG_OUTPUT_TYPE_STD
 * @note The log file is opened by cerver_init () when the type uses it, so set it before.
 */
CERVER_EXPORT void cerver_log_set_output_type (
	const LogOutputType type
);

/**
 * @brief Sets the directory of the log files, it is created when it does not exist.
 *
 * @param[in] pathname the directory, the default is #LOG_DEFAULT_PATH
 * @retval 0 the directory was set
 * @retval 1 a NULL path or the directory could not be created
 * @note Call it before cerver_init (), that opens the file; cerver_end () goes back to the default path. A second
 * call releases the path of the first one.
 */
CERVER_EXPORT unsigned int cerver_log_set_path (
	const char *pathname
);

/**
 * @brief Sets the seconds between two flushes of the log file to the disk, by the thread of the log.
 *
 * @param[in] interval the seconds, the default is #LOG_DEFAULT_UPDATE_INTERVAL
 */
CERVER_EXPORT void cerver_log_set_update_interval (
	const unsigned int interval
);

/**
 * @brief The X-macro table of the times of the messages: the number, the name, the string and the description.
 */
#define LOG_TIME_TYPE_MAP(XX)										\
	XX(0, 	NONE, 		None,		Logs without time)				\
	XX(1, 	TIME, 		Time,		Logs with time)					\
	XX(2, 	DATE, 		Date,		Logs with date)					\
	XX(3, 	BOTH, 		Both,		Logs with date and time)

/**
 * @brief The time that the messages have, one `LOG_TIME_TYPE_<name>` for each row of #LOG_TIME_TYPE_MAP.
 */
typedef enum LogTimeType {

	#define XX(num, name, string, description) LOG_TIME_TYPE_##name = num,
	LOG_TIME_TYPE_MAP (XX)
	#undef XX

} LogTimeType;

/**
 * @brief Returns the string of a time type.
 *
 * @param[in] type the time type
 * @return the string of its row in #LOG_TIME_TYPE_MAP, or the one of LOG_TIME_TYPE_NONE for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_log_time_type_to_string (
	const LogTimeType type
);

/**
 * @brief Returns the description of a time type.
 *
 * @param[in] type the time type
 * @return the description of its row in #LOG_TIME_TYPE_MAP, or the one of LOG_TIME_TYPE_NONE for a value out of the
 * table; a static string that must not be freed
 */
CERVER_PUBLIC const char *cerver_log_time_type_description (
	const LogTimeType type
);

/**
 * @brief Returns the time that the messages have.
 *
 * @return the time type
 */
CERVER_EXPORT LogTimeType cerver_log_get_time_config (void);

/**
 * @brief Sets the time that the messages have.
 *
 * @param[in] type LOG_TIME_TYPE_NONE (the default) without time, LOG_TIME_TYPE_TIME as `hh:mm:ss`,
 * LOG_TIME_TYPE_DATE as `dd/mm/yy`, LOG_TIME_TYPE_BOTH as `dd/mm/yy - hh:mm:ss`
 */
CERVER_EXPORT void cerver_log_set_time_config (const LogTimeType type);

/**
 * @brief Sets whether the time of the messages is the local time or UTC (the default).
 *
 * @param[in] value true for the local time
 * @note The time is taken with localtime_r () or gmtime_r (), in the struct of each message.
 */
CERVER_EXPORT void cerver_log_set_local_time (const bool value);

/**
 * @brief Sets the quiet option: only the success, warning and error messages are printed.
 *
 * cerver_log_msg (), cerver_log_debug (), cerver_log_raw () and the other types of cerver_log () are ignored;
 * cerver_log_both (), cerver_log_error (), cerver_log_warning () and cerver_log_success () are always printed.
 *
 * @param[in] value true to be quiet, the default is false
 */
CERVER_EXPORT void cerver_log_set_quiet (const bool value);

/**
 * @brief Prints a message with its two types, with the colors of the first type in stdout and stderr.
 *
 * @param[in] first_type the type that sets the stream and the color, LOG_TYPE_NONE prints the message alone
 * @param[in] second_type a second type for the header, LOG_TYPE_NONE for none
 * @param[in] format the format of the message, as in printf (); NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log (
	const LogType first_type, const LogType second_type,
	const char *format, ...
) CERVER_PRINTF_FORMAT (3, 4);

/**
 * @brief Prints a message like cerver_log (), always with its date and time.
 *
 * @param[in] first_type the type that sets the stream and the color
 * @param[in] second_type a second type for the header, LOG_TYPE_NONE for none
 * @param[in] format the format of the message, as in printf (); NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_with_date (
	const LogType first_type, const LogType second_type,
	const char *format, ...
) CERVER_PRINTF_FORMAT (3, 4);

/**
 * @brief Prints a message to stdout or stderr, by its type, and to the log file when there is one.
 *
 * It ignores the quiet option and the output type.
 *
 * @param[in] first_type the type that sets the stream and the color
 * @param[in] second_type a second type for the header, LOG_TYPE_NONE for none
 * @param[in] format the format of the message, as in printf (); NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_both (
	const LogType first_type, const LogType second_type,
	const char *format, ...
) CERVER_PRINTF_FORMAT (3, 4);

/**
 * @brief Prints a message without a type, like a printf () with a line break; ignored when quiet.
 *
 * @param[in] msg the format of the message, NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_msg (const char *msg, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Prints a red error message to stderr.
 *
 * @param[in] msg the format of the message, NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_error (const char *msg, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Prints a yellow warning message to stderr.
 *
 * @param[in] msg the format of the message, NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_warning (const char *msg, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Prints a green success message to stdout.
 *
 * @param[in] msg the format of the message, NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_success (const char *msg, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Prints a debug message to stdout; ignored when quiet.
 *
 * @param[in] msg the format of the message, NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_debug (const char *msg, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Prints a message without a type, a time or a line break; ignored when quiet.
 *
 * @param[in] msg the format of the message, NULL prints nothing
 * @param[in] ... the arguments of the format
 */
CERVER_PUBLIC void cerver_log_raw (const char *msg, ...) CERVER_PRINTF_FORMAT (1, 2);

/**
 * @brief Prints a line break, like printf ("\n"), to the outputs of the output type.
 */
CERVER_PUBLIC void cerver_log_line_break (void);

/**
 * @brief Creates the pool of the messages and the default path, and opens the log file when the output type uses
 * it, with the thread that flushes it.
 *
 * @note For the internal methods of cerver: cerver_init () calls it.
 */
CERVER_PRIVATE void cerver_log_init (void);

/**
 * @brief Stops the thread of the log file, closes it and releases the pool and the path.
 *
 * @note For the internal methods of cerver: cerver_end () calls it.
 */
CERVER_PRIVATE void cerver_log_end (void);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
