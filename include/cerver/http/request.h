#ifndef _CERVER_HTTP_REQUEST_H_
#define _CERVER_HTTP_REQUEST_H_

#include <stdbool.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/content.h"
#include "cerver/http/headers.h"
#include "cerver/http/multipart.h"

#define REQUEST_DIRNAME_SIZE			512

#define REQUEST_METHOD_UNDEFINED		8

#ifdef __cplusplus
extern "C" {
#endif

#define REQUEST_METHOD_MAP(XX)			\
	XX(0,  DELETE,      DELETE)			\
	XX(1,  GET,         GET)			\
	XX(2,  HEAD,        HEAD)			\
	XX(3,  POST,        POST)			\
	XX(4,  PUT,         PUT)			\
	XX(5,  CONNECT,     CONNECT)		\
	XX(6,  OPTIONS,     OPTIONS)		\
	XX(7,  TRACE,       TRACE)

typedef enum RequestMethod {

	#define XX(num, name, string) REQUEST_METHOD_##name = num,
	REQUEST_METHOD_MAP(XX)
	#undef XX

} RequestMethod;

// returns a string version of the HTTP request method
CERVER_PUBLIC const char *http_request_method_str (
	const RequestMethod request_method
);

#define REQUEST_PARAMS_SIZE				8

struct _HttpRequest {

	RequestMethod method;

	String *url;
	String *query;

	// parsed query params (key-value pairs)
	DoubleList *query_params;

	unsigned int n_params;
	String *params[REQUEST_PARAMS_SIZE];

	http_header next_header;
	String *headers[HTTP_HEADERS_SIZE];

	// JWT decoded data
	void *decoded_data;
	void (*delete_decoded_data)(void *);
	
	// custom auth handler data
	void *custom_data;
	void (*delete_custom_data)(void *);

	String *body;

	DoubleList *multi_parts;
	MultiPart *current_part;
	ListElement *next_part;

	u8 n_files;
	u8 n_values;

	int dirname_len;
	char dirname[REQUEST_DIRNAME_SIZE];
	
	// body key-value pairs
	// parsed from x-www-form-urlencoded data
	DoubleList *body_values;

	bool keep_files;

};

typedef struct _HttpRequest HttpRequest;

CERVER_PUBLIC HttpRequest *http_request_new (void);

CERVER_PUBLIC void http_request_delete (
	HttpRequest *http_request
);

CERVER_PUBLIC HttpRequest *http_request_create (void);

// destroys any information related to the request
CERVER_PRIVATE void http_request_destroy (
	HttpRequest *http_request
);

// gets the HTTP request's method (GET, POST, ...)
CERVER_EXPORT const RequestMethod http_request_get_method (
	const HttpRequest *http_request
);

// gets the HTTP request's url string value
CERVER_EXPORT const String *http_request_get_url (
	const HttpRequest *http_request
);

// gets the HTTP request's query string value
CERVER_EXPORT const String *http_request_get_query (
	const HttpRequest *http_request
);

// gets the HTTP request's query params list
CERVER_EXPORT const DoubleList *http_request_get_query_params (
	const HttpRequest *http_request
);

// gets how many query params are in the HTTP request
CERVER_EXPORT const unsigned int http_request_get_n_params (
	const HttpRequest *http_request
);

// gets the HTTP request's x query param
CERVER_EXPORT const String *http_request_get_param_at_idx (
	const HttpRequest *http_request, const unsigned int idx
);

// gets the specified HTTP header value from the HTTP request
CERVER_EXPORT const String *http_request_get_header (
	const HttpRequest *http_request, const http_header header
);

// gets the HTTP request's content type value from the request's headers
CERVER_EXPORT ContentType http_request_get_content_type (
	const HttpRequest *http_request
);

// gets the HTTP request's HTTP_HEADER_CONTENT_TYPE String reference
CERVER_EXPORT const String *http_request_get_content_type_string (
	const HttpRequest *http_request
);

// checks if the HTTP request's content type is application/json
CERVER_EXPORT bool http_request_content_type_is_json (
	const HttpRequest *http_request
);

// gets the HTTP request's decoded data
// this data was created by the HTTP route's decode_data method
// configured by http_route_set_decode_data ()
CERVER_EXPORT const void *http_request_get_decoded_data (
	const HttpRequest *http_request
);

// sets the HTTP request's decoded data
CERVER_EXPORT void http_request_set_decoded_data (
	HttpRequest *http_request, void *decoded_data
);

// sets a custom method to delete the HTTP request's decoded data
CERVER_EXPORT void http_request_set_delete_decoded_data (
	HttpRequest *http_request, void (*delete_decoded_data)(void *)
);

// sets free () as the method to delete the HTTP request's decoded data
CERVER_EXPORT void http_request_set_default_delete_decoded_data (
	HttpRequest *http_request
);

// gets the HTTP request's custom data
// this data can be safely set by the user and accessed at any time
CERVER_EXPORT const void *http_request_get_custom_data (
	const HttpRequest *http_request
);

// sets the HTTP request's custom data
CERVER_EXPORT void http_request_set_custom_data (
	HttpRequest *http_request, void *custom_data
);

// sets a custom method to delete the HTTP request's custom data
CERVER_EXPORT void http_request_set_delete_custom_data (
	HttpRequest *http_request, void (*delete_custom_data)(void *)
);

// sets free () as the method to delete the HTTP request's custom data
CERVER_EXPORT void http_request_set_default_delete_custom_data (
	HttpRequest *http_request
);

// gets the HTTP request's body
CERVER_EXPORT const String *http_request_get_body (
	const HttpRequest *http_request
);

// gets the HTTP request's current multi-part structure
CERVER_EXPORT const MultiPart *http_request_get_current_mpart (
	const HttpRequest *http_request
);

// gets the HTTP request's multi-part files count
CERVER_EXPORT const u8 http_request_get_n_files (
	const HttpRequest *http_request
);

// gets the HTTP request's multi-part values count
CERVER_EXPORT const u8 http_request_get_n_values (
	const HttpRequest *http_request
);

// gets the HTTP request's multi-part files dirname length
CERVER_EXPORT const int http_request_get_dirname_len (
	const HttpRequest *http_request
);

// gets the HTTP request's multi-part files directory
// generated by uploads_dirname_generator ()
CERVER_EXPORT const char *http_request_get_dirname (
	const HttpRequest *http_request
);

// sets the HTTP request's multi-part files directory
CERVER_PUBLIC void http_request_set_dirname (
	const HttpRequest *http_request, const char *format, ...
);

// gets the HTTP request's body values
CERVER_EXPORT const DoubleList *http_request_get_body_values (
	const HttpRequest *http_request
);

// prints the HTTP request's headers values
CERVER_PUBLIC void http_request_headers_print (
	const HttpRequest *http_request
);

// prints the request's query params values
CERVER_PUBLIC void http_request_query_params_print (
	const HttpRequest *http_request
);

// returns the matching query param value for the specified key
// returns NULL if no match or no query params
CERVER_EXPORT const String *http_request_query_params_get_value (
	const HttpRequest *http_request, const char *key
);

// searches the request's multi-parts for one with matching key
// returns a reference to the multi-part instance that should not be deleted if found
// returns NULL if not match
CERVER_EXPORT const MultiPart *http_request_multi_parts_get (
	const HttpRequest *http_request, const char *key
);

// searches the request's multi parts values for a value with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
CERVER_EXPORT const char *http_request_multi_parts_get_value (
	const HttpRequest *http_request, const char *key
);

// searches the request's multi parts values for a filename with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
CERVER_EXPORT const char *http_request_multi_parts_get_filename (
	const HttpRequest *http_request, const char *key
);

// searches the request's multi parts values for a saved filename with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
CERVER_EXPORT const char *http_request_multi_parts_get_saved_filename (
	const HttpRequest *http_request, const char *key
);

// starts the HTTP request's multi-parts internal iterator
// returns true on success, false on error
CERVER_EXPORT bool http_request_multi_parts_iter_start (
	const HttpRequest *http_request
);

// gets the next request's multi-part using the iterator
// returns NULL if at the end of the list or error
CERVER_EXPORT const MultiPart *http_request_multi_parts_iter_get_next (
	const HttpRequest *http_request
);

// returns a dlist with constant c strings values (that should not be deleted)
// with all the filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
CERVER_EXPORT DoubleList *http_request_multi_parts_get_all_filenames (
	const HttpRequest *http_request
);

// returns a dlist with constant c strings values (that should not be deleted)
// with all the saved filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
CERVER_EXPORT DoubleList *http_request_multi_parts_get_all_saved_filenames (
	const HttpRequest *http_request
);

// used to safely delete a dlist generated by 
// http_request_multi_parts_get_all_filenames ()
// or http_request_multi_parts_get_all_saved_filenames ()
CERVER_EXPORT void http_request_multi_parts_all_filenames_delete (
	DoubleList *all_filenames
);

// signals that the files referenced by the request should be kept
// so they won't be deleted after the request has ended
// files only get deleted if the http cerver's uploads_delete_when_done
// is set to TRUE
CERVER_EXPORT void http_request_multi_part_keep_files (
	HttpRequest *http_request
);

// discards all the saved files from the multipart request
CERVER_PUBLIC void http_request_multi_part_discard_files (
	const HttpRequest *http_request
);

CERVER_PUBLIC void http_request_multi_parts_print (
	const HttpRequest *http_request
);

CERVER_PUBLIC void http_request_multi_parts_files_print (
	const HttpRequest *http_request
);

// search request's body values for matching value by key
// returns a constant String that should not be deleted if match
// returns NULL if not found
CERVER_EXPORT const String *http_request_body_get_value (
	const HttpRequest *http_request, const char *key
);

#ifdef __cplusplus
}
#endif

#endif