#ifndef _CERVER_HTTP_CUSTOM_H_
#define _CERVER_HTTP_CUSTOM_H_

#include "cerver/types/types.h"

#include "cerver/config.h"

#define HTTP_CUSTOM_HEADER_SIZE					256
#define HTTP_CUSTOM_HEADER_VALUE_SIZE			512

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCustomHeader {

	unsigned int header_len;
	char header[HTTP_CUSTOM_HEADER_SIZE];

	unsigned int header_value_len;
	char header_value[HTTP_CUSTOM_HEADER_VALUE_SIZE];

};

typedef struct _HttpCustomHeader HttpCustomHeader;

CERVER_PRIVATE HttpCustomHeader *http_custom_header_new (void);

CERVER_PRIVATE void http_custom_header_delete (
	void *custom_header_ptr
);

CERVER_PRIVATE HttpCustomHeader *http_custom_header_create (
	const char *header
);

#ifdef __cplusplus
}
#endif

#endif