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

CERVER_PRIVATE HttpOrigin *http_origin_new (void);

CERVER_PRIVATE void http_origin_delete (void *origin_ptr);

CERVER_PRIVATE void http_origin_init (
	HttpOrigin *origin, const char *value
);

CERVER_PUBLIC void http_origin_print (
	const HttpOrigin *origin
);

#ifdef __cplusplus
}
#endif

#endif