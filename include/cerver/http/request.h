#ifndef _CERVER_HTTP_REQUEST_H_
#define _CERVER_HTTP_REQUEST_H_

#include "cerver/types/estring.h"

typedef enum RequestMethod {

	REQUEST_METHOD_DELETE								= 0,
	REQUEST_METHOD_GET									= 1,
	REQUEST_METHOD_HEAD									= 2,
	REQUEST_METHOD_POST									= 3,
	REQUEST_METHOD_PUT									= 4,

} RequestMethod;

typedef enum RequestHeader {

	REQUEST_HEADER_ACCEPT								= 0,
	REQUEST_HEADER_ACCEPT_CHARSET						= 1,
	REQUEST_HEADER_ACCEPT_ENCODING						= 2,
	REQUEST_HEADER_ACCEPT_LANGUAGE						= 3,

	REQUEST_HEADER_ACCESS_CONTROL_REQUEST_HEADERS		= 4,

	REQUEST_HEADER_AUTHORIZATION						= 5,

	REQUEST_HEADER_CACHE_CONTROL						= 6,

	REQUEST_HEADER_CONNECTION							= 7,

	REQUEST_HEADER_CONTENT_LENGTH						= 8,
	REQUEST_HEADER_CONTENT_TYPE							= 9,

	REQUEST_HEADER_COOKIE								= 10,

	REQUEST_HEADER_DATE									= 11,

	REQUEST_HEADER_EXPECT								= 12,

	REQUEST_HEADER_HOST									= 13,

	REQUEST_HEADER_PROXY_AUTHORIZATION					= 14,

	REQUEST_HEADER_USER_AGENT							= 15,

} RequestHeader;

#define REQUEST_HEADERS_SIZE			16
#define REQUEST_HEADER_INVALID			16

typedef struct HttpRequest {

	RequestMethod method;

	estring *url;

	RequestHeader next_header;
	estring *headers[REQUEST_HEADERS_SIZE];
	
	estring *body;

} HttpRequest;

extern HttpRequest *http_request_new (void);

extern void http_request_delete (HttpRequest *http_request);

extern void http_request_headers_print (HttpRequest *http_request);

#endif