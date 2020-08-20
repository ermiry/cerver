#ifndef _CERVER_HTTP_REQUEST_H_
#define _CERVER_HTTP_REQUEST_H_

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/multipart.h"

#define REQUEST_METHOD_MAP(XX)			\
	XX(0,  DELETE,      DELETE)       	\
	XX(1,  GET,         GET)          	\
	XX(2,  HEAD,        HEAD)         	\
	XX(3,  POST,        POST)         	\
	XX(4,  PUT,         PUT)          	\

typedef enum RequestMethod {

	#define XX(num, name, string) REQUEST_METHOD_##name = num,
	REQUEST_METHOD_MAP(XX)
	#undef XX

} RequestMethod;

// returns a string version of the HTTP request method
CERVER_PUBLIC const char *http_request_method_str (RequestMethod m);

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

#define REQUEST_PARAMS_SIZE				8

typedef struct HttpRequest {

	RequestMethod method;

	String *url;
	String *query;

	// parsed query params (key-value pairs)
	DoubleList *query_params;

	unsigned int n_params;
	String *params[REQUEST_PARAMS_SIZE];

	RequestHeader next_header;
	String *headers[REQUEST_HEADERS_SIZE];

	// decoded data from jwt
	void *decoded_data;
	void (*delete_decoded_data)(void *);
	
	String *body;

	MultiPart *current_part;
	DoubleList *multi_parts;
	u8 n_files;
	u8 n_values;
	
	// body key-value pairs parsed from x-www-form-urlencoded data
	DoubleList *body_values;

} HttpRequest;

CERVER_PUBLIC HttpRequest *http_request_new (void);

CERVER_PUBLIC void http_request_delete (HttpRequest *http_request);

CERVER_PUBLIC HttpRequest *http_request_create (void);

CERVER_PUBLIC void http_request_headers_print (HttpRequest *http_request);

// searches the request's multi parts values for a file with matching key
// returns a constant Stirng that should not be deleted if found, NULL if not match
CERVER_EXPORT const String *http_request_multi_parts_get_file (HttpRequest *http_request, const char *key);

// searches the request's multi parts values for a value with matching key
// returns a constant Stirng that should not be deleted if found, NULL if not match
CERVER_EXPORT const String *http_request_multi_parts_get_value (HttpRequest *http_request, const char *key);

CERVER_PUBLIC void http_request_multi_parts_print (HttpRequest *http_request);

// search request's body values for matching value by key
// returns a constant String that should not be deleted if match, NULL if not found
CERVER_EXPORT const String *http_request_body_get_value (HttpRequest *http_request, const char *key);

#endif