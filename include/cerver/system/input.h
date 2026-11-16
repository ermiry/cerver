/**
 * @file cerver/system/input.h
 * @brief The input of the terminal: the lines and the passwords from stdin.
 */

#ifndef _CERVER_INPUT_H_
#define _CERVER_INPUT_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup system_input Input
 * @ingroup system
 * @brief Reading from stdin.
 * @{
 */

/**
 * @brief Discards what is left in stdin up to the next line break or the end of the input.
 */
CERVER_EXPORT void input_clean_stdin (void);

/**
 * @brief Reads a line from stdin, of any length, without its line break.
 *
 * @return the line, allocated with malloc () and released with free (); NULL if it could not be allocated
 */
CERVER_EXPORT char *input_get_line (void);

/** @brief The size of the buffer that input_password () fills, with its NUL. */
#define INPUT_PASSWORD_SIZE				129

/** @brief The format that reads at most INPUT_PASSWORD_SIZE - 1 characters. */
#define INPUT_PASSWORD_SCAN_FORMAT		"%128s"

/**
 * @brief Reads a password from stdin without echo, up to the first whitespace.
 *
 * @param[out] password a buffer of #INPUT_PASSWORD_SIZE bytes
 * @retval 0 the password was read
 * @retval 1 the echo of the terminal could not be turned off, or nothing was read
 */
CERVER_EXPORT unsigned int input_password (char *password);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
