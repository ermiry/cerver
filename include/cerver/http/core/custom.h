/**
 * @file cerver/http/core/custom.h
 * @brief The custom headers of a request: the ones that are not in the map of the HTTP headers.
 */

#ifndef _CERVER_HTTP_CUSTOM_H_
#define _CERVER_HTTP_CUSTOM_H_

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup http_custom Custom headers
 * @ingroup http_core
 * @brief The headers of a request that are not in the map of the HTTP headers.
 * @{
 */

/**
 * @brief The limit of the name of a custom header, a bigger one is answered with 431.
 *
 * The limit of its value is the one of its cerver (cerver_http_set_max_custom_header_size ()).
 */
#define HTTP_CUSTOM_HEADER_SIZE					256

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

/**
 * @brief A header that is not in the map of the HTTP headers, it lives in the arena of its request.
 */
struct _HttpCustomHeader {

	/** @brief The name of the header. */
	String *header;
	/** @brief The value of the header. */
	String *value;

	/** @brief The next custom header of the request, NULL for the last one. */
	struct _HttpCustomHeader *next;

};

/** @brief A custom header, see struct _HttpCustomHeader. */
typedef struct _HttpCustomHeader HttpCustomHeader;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
