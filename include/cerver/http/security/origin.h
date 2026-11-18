/**
 * @file cerver/http/security/origin.h
 * @brief An origin of the whitelist of a CerverHttp, for the CORS headers.
 */

#ifndef _CERVER_HTTP_ORIGIN_H_
#define _CERVER_HTTP_ORIGIN_H_

#include "cerver/config.h"

/**
 * @defgroup http_security Security
 * @ingroup http
 * @brief The origins, the trusted proxies and the authentication of the requests.
 */

/**
 * @defgroup http_origins Origins
 * @ingroup http_security
 * @brief The origins that a CerverHttp accepts.
 * @{
 */

/** @brief The most origins of the whitelist of a CerverHttp. */
#define HTTP_ORIGINS_SIZE				16

/** @brief The size of the value of an origin, with its terminator. */
#define HTTP_ORIGIN_VALUE_SIZE			256

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An origin, like "https://cerver.ermiry.com".
 */
struct _HttpOrigin {

	/** @brief The length of the value. */
	int len;
	/** @brief The value. */
	char value[HTTP_ORIGIN_VALUE_SIZE];

};

/** @brief An origin, see struct _HttpOrigin. */
typedef struct _HttpOrigin HttpOrigin;

/**
 * @brief Allocates an empty origin.
 *
 * @return the new origin, released with cerver_http_origin_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC HttpOrigin *cerver_http_origin_new (void);

/**
 * @brief Releases an origin.
 *
 * @param[in] origin_ptr the origin, it can be NULL
 */
CERVER_PUBLIC void cerver_http_origin_delete (void *origin_ptr);

/**
 * @brief Returns the length of the value of an origin.
 *
 * @param[in] origin the origin, it must not be NULL
 * @return the length
 */
CERVER_PUBLIC const int cerver_http_origin_get_len (
	const HttpOrigin *origin
);

/**
 * @brief Returns the value of an origin.
 *
 * @param[in] origin the origin, it must not be NULL
 * @return the value, that belongs to the origin
 */
CERVER_PUBLIC const char *cerver_http_origin_get_value (
	const HttpOrigin *origin
);

/**
 * @brief Sets the value of an origin.
 *
 * @param[out] origin the origin, NULL is ignored
 * @param[in] value the value, truncated to HTTP_ORIGIN_VALUE_SIZE - 1 characters; NULL keeps the current one
 */
CERVER_PUBLIC void cerver_http_origin_init (
	HttpOrigin *origin, const char *value
);

/**
 * @brief Empties an origin.
 *
 * @param[out] origin the origin, NULL is ignored
 */
CERVER_PUBLIC void cerver_http_origin_reset (HttpOrigin *origin);

/**
 * @brief Prints the length and the value of an origin to stdout.
 *
 * @param[in] origin the origin, NULL prints nothing
 */
CERVER_PUBLIC void cerver_http_origin_print (
	const HttpOrigin *origin
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
