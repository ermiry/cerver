#ifndef _CERVER_INPUT_H_
#define _CERVER_INPUT_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

CERVER_EXPORT void input_clean_stdin (void);

// returns a newly allocated c string
CERVER_EXPORT char *input_get_line (void);

// the size of the buffer that input_password () fills, with its NUL
#define INPUT_PASSWORD_SIZE				129
#define INPUT_PASSWORD_SCAN_FORMAT		"%128s"

// reads a password without echo into a buffer of INPUT_PASSWORD_SIZE bytes
// returns 0 on success, 1 on error
CERVER_EXPORT unsigned int input_password (char *password);

#ifdef __cplusplus
}
#endif

#endif