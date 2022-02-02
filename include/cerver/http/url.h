#ifndef _CERVER_HTTP_URL_H_
#define _CERVER_HTTP_URL_H_

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

// returns a newly allocated url-encoded version of str
// that should be deleted after use
CERVER_PUBLIC char *http_url_encode (const char *str);

// returns a newly allocated url-decoded version of str
// that should be deleted after use
CERVER_PUBLIC char *http_url_decode (const char *str);

#ifdef __cplusplus
}
#endif

#endif