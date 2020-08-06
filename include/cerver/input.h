#ifndef _CERVER_INPUT_H_
#define _CERVER_INPUT_H_

#include "cerver/config.h"

CERVER_EXPORT void input_clean_stdin (void);

// returns a newly allocated c string
CERVER_EXPORT char *input_get_line (void);

CERVER_EXPORT unsigned int input_password (char *password);

#endif