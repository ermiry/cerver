#ifndef _CERVER_UTILS_BASE64_H_
#define _CERVER_UTILS_BASE64_H_

#include <stddef.h>

extern char *base64_encode (size_t* enclen, size_t len, unsigned char* data);

extern unsigned char *base64_decode (size_t* declen, size_t len, char* data);

#endif
