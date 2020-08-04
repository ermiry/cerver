#ifndef _CERVER_UTILS_BASE64_H_
#define _CERVER_UTILS_BASE64_H_

#include <stddef.h>

extern int base64_encode (char *encoded, const char *string, int len);

extern int base64_decode (char *bufplain, const char *bufcoded);

#endif
