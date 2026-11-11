#ifndef _CERVER_HTTP_METHOD_H_
#define _CERVER_HTTP_METHOD_H_

#include <stdbool.h>

#include "cerver/config.h"

// the size of the tables indexed by a request method (the highest value + 1)
#define REQUEST_METHOD_SIZE				47

#ifdef __cplusplus
extern "C" {
#endif

// the only table of the methods, with the values of the parser (llhttp):
// the HTTP methods that llhttp accepts in a request, all of them can be registered in a route,
// the values 34 to 45 are PRI (the preface of HTTP/2) and the RTSP methods, the parser rejects them
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

typedef enum RequestMethod {

	#define XX(num, name, string) REQUEST_METHOD_##name = num,
	REQUEST_METHOD_MAP(XX)
	#undef XX

	// any method outside REQUEST_METHOD_MAP, the method of a request that has not been parsed
	REQUEST_METHOD_UNDEFINED = 255

} RequestMethod;

// returns a string version of the HTTP request method,
// "Undefined" for a method outside REQUEST_METHOD_MAP
CERVER_PUBLIC const char *http_request_method_str (
	const RequestMethod request_method
);

// returns true if the method is one of REQUEST_METHOD_MAP
CERVER_PUBLIC bool http_request_method_is_valid (
	const RequestMethod request_method
);

#ifdef __cplusplus
}
#endif

#endif
