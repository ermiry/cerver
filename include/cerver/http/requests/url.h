#ifndef _CERVER_HTTP_URL_H_
#define _CERVER_HTTP_URL_H_

#include <stddef.h>

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

// percent-decodes length bytes of a path segment into destination,
// that has room for length bytes, a '+' is kept (it is a space only in a query)
// and an incomplete or invalid escape is copied as it is
// returns the length of the decoded value
CERVER_PUBLIC size_t http_url_decode_path (
	char *destination, const char *source, const size_t length
);

#ifdef __cplusplus
}
#endif

#endif
