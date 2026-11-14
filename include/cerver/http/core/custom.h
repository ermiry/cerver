#ifndef _CERVER_HTTP_CUSTOM_H_
#define _CERVER_HTTP_CUSTOM_H_

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

// the limit of the name of a custom header, a bigger one is answered with 431
// the limit of its value is the one of its cerver (http_cerver_set_max_custom_header_size ())
#define HTTP_CUSTOM_HEADER_SIZE					256

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

#ifdef __cplusplus
}
#endif

#endif
