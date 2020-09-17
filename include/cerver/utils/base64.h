#ifndef _CERVER_UTILS_BASE64_H_
#define _CERVER_UTILS_BASE64_H_

#include <stddef.h>

#include "cerver/config.h"

CERVER_PUBLIC char *base64_encode (size_t *enclen, size_t len, unsigned char *data);

CERVER_PUBLIC unsigned char *base64_decode (size_t *declen, size_t len, char *data);

#endif
