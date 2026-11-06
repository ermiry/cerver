#ifndef _CERVER_HTTP_CUSTOM_H_
#define _CERVER_HTTP_CUSTOM_H_

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"

// the limits of a custom header, a bigger one is answered with 431
#define HTTP_CUSTOM_HEADER_SIZE					256
#define HTTP_CUSTOM_HEADER_VALUE_SIZE			512

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

// a header that is not in the HTTP headers map,
// it lives in the arena of its request
struct _HttpCustomHeader {

	String *header;
	String *value;

	struct _HttpCustomHeader *next;

};

typedef struct _HttpCustomHeader HttpCustomHeader;

// creates a custom header with an empty value in the arena
CERVER_PRIVATE HttpCustomHeader *http_custom_header_create (
	struct Arena *arena, const char *header, const size_t header_len
);

// sets the value of the custom header, a new string in the arena
// returns 0 on success, 1 on error
CERVER_PRIVATE unsigned int http_custom_header_set_value (
	struct Arena *arena, HttpCustomHeader *custom_header,
	const char *value, const size_t value_len
);

#ifdef __cplusplus
}
#endif

#endif
