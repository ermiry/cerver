/**
 * @file cerver/http/requests/method.h
 * @brief The methods of a request.
 */

#ifndef _CERVER_HTTP_METHOD_H_
#define _CERVER_HTTP_METHOD_H_

#include <stdbool.h>

#include "cerver/config.h"

/**
 * @defgroup http_requests Requests
 * @ingroup http
 * @brief The requests that a CerverHttp receives: their methods, their urls, their contents and their bodies.
 */

/**
 * @defgroup http_methods Methods
 * @ingroup http_requests
 * @brief The methods that a request and a route can have.
 * @{
 */

/** @brief The size of the tables indexed by a request method (the highest value + 1). */
#define REQUEST_METHOD_SIZE				47

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The X-macro table of the methods, the only one, with the values of the parser (the ones of llhttp, that it
 * replaced in the 3.0b-9): the number, the name and the string of each one.
 *
 * They are the HTTP methods that the parser accepts in a request, and all of them can be registered in a route; the
 * values 34 to 45 are PRI (the preface of HTTP/2) and the RTSP methods, that the parser rejects.
 */
#define REQUEST_METHOD_MAP(XX)			\
	XX(0,  DELETE,      DELETE)			\
	XX(1,  GET,         GET)			\
	XX(2,  HEAD,        HEAD)			\
	XX(3,  POST,        POST)			\
	XX(4,  PUT,         PUT)			\
	XX(5,  CONNECT,     CONNECT)		\
	XX(6,  OPTIONS,     OPTIONS)		\
	XX(7,  TRACE,       TRACE)			\
	XX(8,  COPY,        COPY)			\
	XX(9,  LOCK,        LOCK)			\
	XX(10, MKCOL,       MKCOL)			\
	XX(11, MOVE,        MOVE)			\
	XX(12, PROPFIND,    PROPFIND)		\
	XX(13, PROPPATCH,   PROPPATCH)		\
	XX(14, SEARCH,      SEARCH)			\
	XX(15, UNLOCK,      UNLOCK)			\
	XX(16, BIND,        BIND)			\
	XX(17, REBIND,      REBIND)			\
	XX(18, UNBIND,      UNBIND)			\
	XX(19, ACL,         ACL)			\
	XX(20, REPORT,      REPORT)			\
	XX(21, MKACTIVITY,  MKACTIVITY)		\
	XX(22, CHECKOUT,    CHECKOUT)		\
	XX(23, MERGE,       MERGE)			\
	XX(24, MSEARCH,     M-SEARCH)		\
	XX(25, NOTIFY,      NOTIFY)			\
	XX(26, SUBSCRIBE,   SUBSCRIBE)		\
	XX(27, UNSUBSCRIBE, UNSUBSCRIBE)	\
	XX(28, PATCH,       PATCH)			\
	XX(29, PURGE,       PURGE)			\
	XX(30, MKCALENDAR,  MKCALENDAR)		\
	XX(31, LINK,        LINK)			\
	XX(32, UNLINK,      UNLINK)			\
	XX(33, SOURCE,      SOURCE)			\
	XX(46, QUERY,       QUERY)

/**
 * @brief The method of a request, one `REQUEST_METHOD_<name>` for each row of #REQUEST_METHOD_MAP.
 */
typedef enum CerverHttpMethod {

	#define XX(num, name, string) REQUEST_METHOD_##name = num,
	REQUEST_METHOD_MAP(XX)
	#undef XX

	/** @brief Any method outside #REQUEST_METHOD_MAP, the method of a request that has not been parsed. */
	REQUEST_METHOD_UNDEFINED = 255

} CerverHttpMethod;

/**
 * @brief Returns the string of a method.
 *
 * @param[in] request_method the method
 * @return the string of its row in #REQUEST_METHOD_MAP, "Undefined" for a method outside it; a static string that
 * must not be freed
 */
CERVER_PUBLIC const char *cerver_http_method_to_string (
	const CerverHttpMethod request_method
);

/**
 * @brief Returns whether a method is one of #REQUEST_METHOD_MAP.
 *
 * @param[in] request_method the method
 * @return true for a method of the table
 */
CERVER_PUBLIC bool cerver_http_method_is_valid (
	const CerverHttpMethod request_method
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
