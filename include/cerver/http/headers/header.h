#ifndef _CERVER_HTTP_HEADERS_HEADER_H_
#define _CERVER_HTTP_HEADERS_HEADER_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_HEADER_VALUE_SIZE			256

#define HTTP_HEADER_TEMP_SIZE			128
#define HTTP_HEADER_ACTUAL_SIZE			128
struct _HttpHeader {

	int len;
	char value[HTTP_HEADER_VALUE_SIZE];

};

typedef struct _HttpHeader HttpHeader;

CERVER_PRIVATE void http_header_reset (HttpHeader *header);

CERVER_PRIVATE void http_header_set (
	HttpHeader *header, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

CERVER_PUBLIC void http_header_print (
	const HttpHeader *header
);

#ifdef __cplusplus
}
#endif

#endif
