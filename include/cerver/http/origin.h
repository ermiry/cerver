#ifndef _CERVER_HTTP_ORIGIN_H_
#define _CERVER_HTTP_ORIGIN_H_

#include "cerver/config.h"

#define HTTP_ORIGINS_SIZE				16

#define HTTP_ORIGIN_VALUE_SIZE			256

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpOrigin {

	int len;
	char value[HTTP_ORIGIN_VALUE_SIZE];

};

typedef struct _HttpOrigin HttpOrigin;

CERVER_PUBLIC HttpOrigin *http_origin_new (void);

CERVER_PUBLIC void http_origin_delete (void *origin_ptr);

CERVER_PUBLIC const int http_origin_get_len (
	const HttpOrigin *origin
);

CERVER_PUBLIC const char *http_origin_get_value (
	const HttpOrigin *origin
);

CERVER_PUBLIC void http_origin_init (
	HttpOrigin *origin, const char *value
);

CERVER_PUBLIC void http_origin_reset (HttpOrigin *origin);

CERVER_PUBLIC void http_origin_print (
	const HttpOrigin *origin
);

#ifdef __cplusplus
}
#endif

#endif