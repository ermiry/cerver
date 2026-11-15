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
 * The limit of its value is the one of its cerver (http_cerver_set_max_custom_header_size ()).
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

/**
 * @brief Creates a custom header with an empty value in an arena.
 *
 * @param[in] arena the arena of the request
 * @param[in] header the name of the header
 * @param[in] header_len the length of the name
 * @return the new custom header, NULL on error
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpCustomHeader *http_custom_header_create (
	struct Arena *arena, const char *header, const size_t header_len
);

/**
 * @brief Sets the value of a custom header, a new string in the arena.
 *
 * @param[in] arena the arena of the request
 * @param[in,out] custom_header the custom header
 * @param[in] value the value
 * @param[in] value_len the length of the value
 * @retval 0 the value was set
 * @retval 1 the string could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int http_custom_header_set_value (
	struct Arena *arena, HttpCustomHeader *custom_header,
	const char *value, const size_t value_len
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
