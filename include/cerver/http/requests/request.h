/**
 * @file cerver/http/requests/request.h
 * @brief The HttpRequest: what a handler gets of a request, its url, its params, its headers, its body and its parts.
 */

#ifndef _CERVER_HTTP_REQUEST_H_
#define _CERVER_HTTP_REQUEST_H_

#include <stdbool.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/network/network.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/utils.h"
#include "cerver/http/headers/header.h"
#include "cerver/http/requests/content.h"
#include "cerver/http/requests/multipart.h"

/**
 * @defgroup http_request Request
 * @ingroup http_requests
 * @brief A request and its getters.
 *
 * The getters take a request that is not NULL unless they say otherwise; what they return belongs to the request.
 * @{
 */

/** @brief The size of the directory of the uploads of a request, with its terminator. */
#define REQUEST_DIRNAME_SIZE			512

/**
 * @brief The value of a method that is not one of #REQUEST_METHOD_MAP.
 *
 * The values of the methods are the ones of http_parser (cerver/http/requests/method.h).
 */
#define REQUEST_METHOD_UNDEFINED		34

/** @brief The size of the tables indexed by a request method: the highest value of #REQUEST_METHOD_MAP + 1. */
#define REQUEST_METHOD_SIZE				29

/** @brief The size of the chunks of the arena of a request that was not created in the arena of its thread. */
#define REQUEST_ARENA_CHUNK_SIZE		(1 << 10)

/** @brief The longest X-Request-Id that is accepted; a longer one is replaced with a new id. */
#define HTTP_REQUEST_ID_LENGTH			64
/** @brief The size of the id of a request, with its terminator. */
#define HTTP_REQUEST_ID_SIZE			(HTTP_REQUEST_ID_LENGTH + 1)

/** @brief The length of a UUID v4, "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx". */
#define HTTP_REQUEST_ID_UUID_LENGTH		36

/** @brief The size of the longest address of a client (an IPv6 with its zone) and its terminator, the same as HTTP_CLIENT_IP_SIZE. */
#define HTTP_REQUEST_CLIENT_IP_SIZE		NETWORK_ADDRESS_STRING_SIZE

#ifdef __cplusplus
extern "C" {
#endif

struct Arena;

/**
 * @brief The X-macro table of the methods that a route can handle: the number (the one of http_parser), the name and
 * the string of each one.
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
	XX(28, PATCH,       PATCH)

/**
 * @brief The method of a request, one `REQUEST_METHOD_<name>` for each row of #REQUEST_METHOD_MAP.
 */
typedef enum RequestMethod {

	#define XX(num, name, string) REQUEST_METHOD_##name = num,
	REQUEST_METHOD_MAP(XX)
	#undef XX

} RequestMethod;

/**
 * @brief Returns the string of a request method.
 *
 * @param[in] request_method the method
 * @return the string of its row in #REQUEST_METHOD_MAP, or `Undefined` for any other value; a static string that
 * must not be freed
 */
CERVER_PUBLIC const char *http_request_method_str (
	const RequestMethod request_method
);

/**
 * @brief Returns whether a value is one of the methods of #REQUEST_METHOD_MAP.
 *
 * @param[in] request_method the method
 * @return true if it is a row of #REQUEST_METHOD_MAP
 */
CERVER_PUBLIC bool http_request_method_is_valid (
	const RequestMethod request_method
);

/**
 * @brief A route param: a slice of the url of the request.
 *
 * Its percent-decoded value is created in the arena of the request when it is requested.
 */
typedef struct HttpRequestParam {

	/** @brief The param in the url, it does not end with '\0'. */
	const char *value;
	/** @brief The length of #value. */
	size_t len;

	/** @brief The percent-decoded value, NULL until it is requested. */
	String *decoded;

} HttpRequestParam;

/**
 * @brief A request.
 */
struct _HttpRequest {

	/** @brief The method. */
	RequestMethod method;

	/** @brief The path of the url. */
	String *url;
	/** @brief The query of the url. */
	String *query;

	/** @brief The query params, as key value pairs. */
	DoubleList *query_params;

	/** @brief The route params. */
	unsigned int n_params;
	/** @brief The values of the route params. */
	HttpRequestParam *params;
	/** @brief The names of the route params, the ones of the matched endpoint. */
	char *const *params_names;

	/** @brief The header whose value is being received. */
	http_header next_header;
	/** @brief The known headers, by http_header; NULL for a header that the request does not have. */
	String *headers[HTTP_HEADERS_SIZE];

	/** @brief The custom headers. */
	size_t n_custom_headers;
	/** @brief The first custom header, they live in the arena of the request. */
	HttpCustomHeader *custom_headers;
	/** @brief The custom header that is being received. */
	HttpCustomHeader *current_custom_header;

	/** @brief The decoded data of its JWT. */
	void *decoded_data;
	/** @brief Releases #decoded_data. */
	void (*delete_decoded_data)(void *);

	/** @brief The data of a custom authentication handler. */
	void *custom_data;
	/** @brief Releases #custom_data. */
	void (*delete_custom_data)(void *);

	/** @brief The body, for a body that is not multipart. */
	String *body;

	/** @brief The parts of a multipart body. */
	DoubleList *multi_parts;
	/** @brief The part that is being received. */
	MultiPart *current_part;
	/** @brief The next part of the iterator of the parts. */
	ListElement *next_part;

	/** @brief The parts that are files. */
	u8 n_files;
	/** @brief The parts that are values. */
	u8 n_values;

	/** @brief The length that the directory of its uploads needed, see http_request_set_dirname (). */
	int dirname_len;
	/** @brief The directory of its uploads, generated by the uploads dirname generator. */
	char dirname[REQUEST_DIRNAME_SIZE];

	/** @brief The values of a x-www-form-urlencoded body, as key value pairs. */
	DoubleList *body_values;

	/** @brief Its uploaded files are kept when it ends. */
	bool keep_files;

	/** @brief Its id when the cerver identifies its requests, "" if it does not. */
	char id[HTTP_REQUEST_ID_SIZE];

	/** @brief The address of the client: the peer, or the one forwarded by a trusted proxy. */
	char client_ip[HTTP_REQUEST_CLIENT_IP_SIZE];

	/**
	 * @brief The arena of its thread, when the request lives there.
	 *
	 * The request, its url, its query and its headers are released with the arena, so none of them must be used
	 * after the route handler returns.
	 */
	struct Arena *arena;

	/** @brief The arena of a request that was not created in an arena: it holds its params, its custom headers and their strings. */
	struct Arena *own_arena;

};

/** @brief A request, see struct _HttpRequest. */
typedef struct _HttpRequest HttpRequest;

/**
 * @brief Returns the arena where the params and the custom headers of a request are created.
 *
 * @param[in,out] http_request the request
 * @return the arena of its thread, or its own one, created the first time; NULL if it could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE struct Arena *http_request_get_arena (
	HttpRequest *http_request
);

/**
 * @brief Allocates an empty request.
 *
 * @return the new request, released with http_request_delete (); NULL if it could not be allocated
 */
CERVER_PUBLIC HttpRequest *http_request_new (void);

/**
 * @brief Creates an empty request in an arena, see HttpRequest::arena.
 *
 * @param[in] arena the arena
 * @return the new request, NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpRequest *http_request_new_arena (struct Arena *arena);

/**
 * @brief Releases a request: its strings, its lists, its data with their delete methods and its own arena.
 *
 * @param[in] http_request the request, it can be NULL
 */
CERVER_PUBLIC void http_request_delete (
	HttpRequest *http_request
);

/**
 * @brief Creates an empty request, like http_request_new ().
 *
 * @return the new request, NULL if it could not be allocated
 */
CERVER_PUBLIC HttpRequest *http_request_create (void);

/**
 * @brief Ends a request: its uploaded files are removed unless they are kept.
 *
 * @param[in,out] http_request the request, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_request_destroy (
	HttpRequest *http_request
);

/**
 * @brief Returns the method of a request (GET, POST, ...).
 *
 * @param[in] http_request the request
 * @return the method
 */
CERVER_EXPORT RequestMethod http_request_get_method (
	const HttpRequest *http_request
);

/**
 * @brief Returns the id of a request: its X-Request-Id or a new UUID v4.
 *
 * @param[in] http_request the request
 * @return the id when the cerver identifies its requests (http_cerver_set_request_id ()), "" otherwise
 */
CERVER_EXPORT const char *http_request_get_id (
	const HttpRequest *http_request
);

/**
 * @brief Returns the address of the client that sent a request.
 *
 * @param[in] http_request the request
 * @return the peer, or the address forwarded by a trusted proxy (http_cerver_set_trusted_proxies ())
 */
CERVER_EXPORT const char *http_request_get_client_ip (
	const HttpRequest *http_request
);

/**
 * @brief Sets the id of a request: its X-Request-Id if it is valid, or a new UUID v4.
 *
 * @param[in,out] http_request the request; its id is "" if a new one could not be generated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_request_resolve_id (
	HttpRequest *http_request
);

/**
 * @brief Returns whether a value can be the id of a request: up to #HTTP_REQUEST_ID_LENGTH characters of
 * [A-Za-z0-9._-].
 *
 * @param[in] value the value
 * @param[in] length its length
 * @return true for a valid id, false for a NULL or an empty value
 */
CERVER_PUBLIC bool http_request_id_is_valid (
	const char *value, const size_t length
);

/**
 * @brief Writes a new UUID v4 from the random bytes of OpenSSL (RAND_bytes ()).
 *
 * @param[out] id the buffer, of at least #HTTP_REQUEST_ID_UUID_LENGTH + 1 bytes
 * @retval 0 the id was written
 * @retval 1 a NULL buffer or the random bytes failed
 */
CERVER_PUBLIC u8 http_request_id_generate (char *id);

/**
 * @brief Returns the path of the url of a request.
 *
 * @param[in] http_request the request
 * @return the path
 */
CERVER_EXPORT const String *http_request_get_url (
	const HttpRequest *http_request
);

/**
 * @brief Returns the query of the url of a request.
 *
 * @param[in] http_request the request
 * @return the query, NULL if it has none
 */
CERVER_EXPORT const String *http_request_get_query (
	const HttpRequest *http_request
);

/**
 * @brief Returns the query params of a request.
 *
 * @param[in] http_request the request
 * @return the list of KeyValuePair, NULL if it has none
 */
CERVER_EXPORT const DoubleList *http_request_get_query_params (
	const HttpRequest *http_request
);

/**
 * @brief Returns how many route params a request has.
 *
 * @param[in] http_request the request
 * @return the params
 */
CERVER_EXPORT unsigned int http_request_get_n_params (
	const HttpRequest *http_request
);

/**
 * @brief Returns a route param of a request by its index, percent-decoded.
 *
 * @param[in] http_request the request
 * @param[in] idx the index
 * @return the value, that lives only while the route handler runs; NULL for an index out of range
 */
CERVER_EXPORT const String *http_request_get_param_at_idx (
	const HttpRequest *http_request, const unsigned int idx
);

/**
 * @brief Returns the route param with a name of the matched route (":id" is "id"), percent-decoded.
 *
 * @param[in] http_request the request
 * @param[in] name the name
 * @return the value, that lives only while the route handler runs; NULL for a NULL name or a missing param
 */
CERVER_EXPORT const String *http_request_get_param (
	const HttpRequest *http_request, const char *name
);

/**
 * @brief Returns the name of a route param by its index.
 *
 * @param[in] http_request the request
 * @param[in] idx the index
 * @return the name, NULL if it has no name or for an index out of range
 */
CERVER_EXPORT const char *http_request_get_param_name_at_idx (
	const HttpRequest *http_request, const unsigned int idx
);

/**
 * @brief Returns the value of a known header of a request.
 *
 * @param[in] http_request the request
 * @param[in] header the header
 * @return the value, NULL if the request does not have it or for a value out of the table
 */
CERVER_EXPORT const String *http_request_get_header (
	const HttpRequest *http_request, const http_header header
);

/**
 * @brief Adds a custom header with its name to a request, as the one whose value is being received.
 *
 * @param[in,out] http_request the request
 * @param[in] header the name
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_request_set_current_custom_header (
	HttpRequest *http_request, const char *header
);

/**
 * @brief Sets the value of the custom header that is being received.
 *
 * @param[in,out] http_request the request
 * @param[in] header_value the value
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_request_set_current_custom_header_value (
	HttpRequest *http_request, const char *header_value
);

/**
 * @brief Returns the custom header of a request with a name, compared without case.
 *
 * @param[in] http_request the request
 * @param[in] header the name
 * @return the custom header, NULL if it is not found
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpCustomHeader *http_request_custom_header_find (
	const HttpRequest *http_request, const char *header
);

/**
 * @brief Returns how many custom headers a request has.
 *
 * @param[in] http_request the request
 * @return the custom headers
 */
CERVER_EXPORT size_t http_request_get_custom_headers_count (
	const HttpRequest *http_request
);

/**
 * @brief Returns the value of a header of a request with a name, compared without case like the HTTP parser does.
 *
 * A custom header is looked for first, and then a known header with the name.
 *
 * @param[in] http_request the request
 * @param[in] header the name
 * @return the value, NULL for a NULL name or a missing header
 */
CERVER_EXPORT const char *http_request_get_custom_header (
	const HttpRequest *http_request, const char *header
);

/**
 * @brief Returns the content type of a request from its Content-Type, compared exactly with the mimes.
 *
 * @param[in] http_request the request
 * @return the content type, HTTP_CONTENT_TYPE_NONE without a Content-Type or with an unknown one
 */
CERVER_EXPORT ContentType http_request_get_content_type (
	const HttpRequest *http_request
);

/**
 * @brief Returns the Content-Type of a request.
 *
 * @param[in] http_request the request
 * @return the value, NULL if it has none
 */
CERVER_EXPORT const String *http_request_get_content_type_string (
	const HttpRequest *http_request
);

/**
 * @brief Returns whether the Content-Type of a request is "application/json", like http_content_type_is_json ().
 *
 * @param[in] http_request the request
 * @return true for json, false without a Content-Type
 */
CERVER_EXPORT bool http_request_content_type_is_json (
	const HttpRequest *http_request
);

/**
 * @brief Reads the Range of a request.
 *
 * @param[in] http_request the request
 * @param[out] bytes_range the range
 * @return true when the request has a valid bytes range, false otherwise
 */
CERVER_EXPORT bool http_request_get_bytes_range (
	const HttpRequest *http_request, BytesRange *bytes_range
);

/**
 * @brief Returns the decoded data of a request, created by the decode data method of its route
 * (http_route_set_decode_data ()).
 *
 * @param[in] http_request the request
 * @return the data, NULL if it has none
 */
CERVER_EXPORT const void *http_request_get_decoded_data (
	const HttpRequest *http_request
);

/**
 * @brief Sets the decoded data of a request.
 *
 * @param[in,out] http_request the request
 * @param[in] decoded_data the data
 */
CERVER_EXPORT void http_request_set_decoded_data (
	HttpRequest *http_request, void *decoded_data
);

/**
 * @brief Sets how the decoded data of a request is released.
 *
 * @param[in,out] http_request the request
 * @param[in] delete_decoded_data releases the data, NULL to keep it
 */
CERVER_EXPORT void http_request_set_delete_decoded_data (
	HttpRequest *http_request, void (*delete_decoded_data)(void *)
);

/**
 * @brief Releases the decoded data of a request with free ().
 *
 * @param[in,out] http_request the request
 */
CERVER_EXPORT void http_request_set_default_delete_decoded_data (
	HttpRequest *http_request
);

/**
 * @brief Returns the custom data of a request, that the application can set and read at any time.
 *
 * @param[in] http_request the request
 * @return the data, NULL if it has none
 */
CERVER_EXPORT const void *http_request_get_custom_data (
	const HttpRequest *http_request
);

/**
 * @brief Sets the custom data of a request.
 *
 * @param[in,out] http_request the request
 * @param[in] custom_data the data
 */
CERVER_EXPORT void http_request_set_custom_data (
	HttpRequest *http_request, void *custom_data
);

/**
 * @brief Sets how the custom data of a request is released.
 *
 * @param[in,out] http_request the request
 * @param[in] delete_custom_data releases the data, NULL to keep it
 */
CERVER_EXPORT void http_request_set_delete_custom_data (
	HttpRequest *http_request, void (*delete_custom_data)(void *)
);

/**
 * @brief Releases the custom data of a request with free ().
 *
 * @param[in,out] http_request the request
 */
CERVER_EXPORT void http_request_set_default_delete_custom_data (
	HttpRequest *http_request
);

/**
 * @brief Returns the body of a request that is not multipart.
 *
 * @param[in] http_request the request
 * @return the body, NULL if it has none
 */
CERVER_EXPORT const String *http_request_get_body (
	const HttpRequest *http_request
);

/**
 * @brief Returns the part that is being received, for the uploads generators.
 *
 * @param[in] http_request the request
 * @return the part, NULL if there is none
 */
CERVER_EXPORT const MultiPart *http_request_get_current_mpart (
	const HttpRequest *http_request
);

/**
 * @brief Returns how many parts of a request are files.
 *
 * @param[in] http_request the request
 * @return the files
 */
CERVER_EXPORT u8 http_request_get_n_files (
	const HttpRequest *http_request
);

/**
 * @brief Returns how many parts of a request are values.
 *
 * @param[in] http_request the request
 * @return the values
 */
CERVER_EXPORT u8 http_request_get_n_values (
	const HttpRequest *http_request
);

/**
 * @brief Returns the length that the directory of the uploads of a request needed.
 *
 * @param[in] http_request the request
 * @return the length, see http_request_set_dirname ()
 */
CERVER_EXPORT int http_request_get_dirname_len (
	const HttpRequest *http_request
);

/**
 * @brief Returns the directory of the uploads of a request, generated by the uploads dirname generator.
 *
 * @param[in] http_request the request
 * @return the directory, empty if it has none
 */
CERVER_EXPORT const char *http_request_get_dirname (
	const HttpRequest *http_request
);

/**
 * @brief Sets the directory of the uploads of a request from a format; the uploads dirname generators call it.
 *
 * @param[in] http_request the request, NULL is ignored
 * @param[in] format the printf () format of the directory, NULL is ignored
 * @param[in] ... the arguments of the format
 * @note The length is what the format needed: a directory of REQUEST_DIRNAME_SIZE - 1 characters is kept whole, and
 * the cerver does not use one whose length is #REQUEST_DIRNAME_SIZE or more (it was cut).
 */
CERVER_PUBLIC void http_request_set_dirname (
	const HttpRequest *http_request, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Returns the values of a x-www-form-urlencoded body.
 *
 * @param[in] http_request the request
 * @return the list of KeyValuePair, NULL if it has none
 */
CERVER_EXPORT const DoubleList *http_request_get_body_values (
	const HttpRequest *http_request
);

/**
 * @brief Logs the known headers that a request has.
 *
 * @param[in] http_request the request, NULL prints nothing
 */
CERVER_PUBLIC void http_request_headers_print (
	const HttpRequest *http_request
);

/**
 * @brief Logs every known header, with "NULL" for the ones that the request does not have.
 *
 * @param[in] http_request the request, NULL prints nothing
 */
CERVER_PUBLIC void http_request_headers_print_full (
	const HttpRequest *http_request
);

/**
 * @brief Prints the query params of a request to stdout.
 *
 * @param[in] http_request the request, NULL prints nothing
 */
CERVER_PUBLIC void http_request_query_params_print (
	const HttpRequest *http_request
);

/**
 * @brief Returns the value of a query param of a request.
 *
 * @param[in] http_request the request
 * @param[in] key the key
 * @return the value, NULL for a NULL argument, a request without query params or a missing key
 */
CERVER_EXPORT const String *http_request_query_params_get_value (
	const HttpRequest *http_request, const char *key
);

/**
 * @brief Returns the part of a request with a name.
 *
 * @param[in] http_request the request
 * @param[in] key the name of the part
 * @return the part, that must not be deleted; NULL for a NULL argument or a missing part
 */
CERVER_EXPORT const MultiPart *http_request_multi_parts_get (
	const HttpRequest *http_request, const char *key
);

/**
 * @brief Returns the value of the part of a request with a name.
 *
 * @param[in] http_request the request
 * @param[in] key the name of the part
 * @return the value, that must not be deleted; NULL for a NULL argument or a missing part
 */
CERVER_EXPORT const char *http_request_multi_parts_get_value (
	const HttpRequest *http_request, const char *key
);

/**
 * @brief Returns the sanitized original filename of the part of a request with a name.
 *
 * @param[in] http_request the request
 * @param[in] key the name of the part
 * @return the filename, that must not be deleted; NULL for a NULL argument or a missing part
 */
CERVER_EXPORT const char *http_request_multi_parts_get_filename (
	const HttpRequest *http_request, const char *key
);

/**
 * @brief Returns the saved filename of the part of a request with a name.
 *
 * @param[in] http_request the request
 * @param[in] key the name of the part
 * @return the saved filename, that must not be deleted; NULL for a NULL argument or a missing part
 */
CERVER_EXPORT const char *http_request_multi_parts_get_saved_filename (
	const HttpRequest *http_request, const char *key
);

/**
 * @brief Starts the iterator of the parts of a request.
 *
 * @param[in] http_request the request
 * @return true when the request has parts, false otherwise
 */
CERVER_EXPORT bool http_request_multi_parts_iter_start (
	const HttpRequest *http_request
);

/**
 * @brief Returns the next part of the iterator.
 *
 * @param[in] http_request the request
 * @return the part, NULL after the last one
 */
CERVER_EXPORT const MultiPart *http_request_multi_parts_iter_get_next (
	const HttpRequest *http_request
);

/**
 * @brief Returns the sanitized original filenames of the parts of a request.
 *
 * @param[in] http_request the request
 * @return a new list of the filenames, that belong to the parts; deleted with
 * http_request_multi_parts_all_filenames_delete (); NULL for a NULL request or one without parts
 */
CERVER_EXPORT DoubleList *http_request_multi_parts_get_all_filenames (
	const HttpRequest *http_request
);

/**
 * @brief Returns the saved filenames of the parts of a request.
 *
 * @param[in] http_request the request
 * @return a new list of the saved filenames, that belong to the parts; deleted with
 * http_request_multi_parts_all_filenames_delete (); NULL for a NULL request or one without parts
 */
CERVER_EXPORT DoubleList *http_request_multi_parts_get_all_saved_filenames (
	const HttpRequest *http_request
);

/**
 * @brief Deletes a list of http_request_multi_parts_get_all_filenames () or
 * http_request_multi_parts_get_all_saved_filenames (), without the filenames.
 *
 * @param[in] all_filenames the list, it can be NULL
 */
CERVER_EXPORT void http_request_multi_parts_all_filenames_delete (
	DoubleList *all_filenames
);

/**
 * @brief Keeps the uploaded files of a request when it ends.
 *
 * They are only deleted when the cerver deletes the uploads when they are done
 * (http_cerver_set_uploads_delete_when_done ()).
 *
 * @param[in,out] http_request the request, NULL is ignored
 */
CERVER_EXPORT void http_request_multi_part_keep_files (
	HttpRequest *http_request
);

/**
 * @brief Removes every saved file of the parts of a request.
 *
 * @param[in] http_request the request, NULL is ignored
 */
CERVER_PUBLIC void http_request_multi_part_discard_files (
	const HttpRequest *http_request
);

/**
 * @brief Prints the parts of a request to stdout.
 *
 * @param[in] http_request the request, NULL prints nothing
 */
CERVER_PUBLIC void http_request_multi_parts_print (
	const HttpRequest *http_request
);

/**
 * @brief Prints the files of the parts of a request to stdout.
 *
 * @param[in] http_request the request, NULL prints nothing
 */
CERVER_PUBLIC void http_request_multi_parts_files_print (
	const HttpRequest *http_request
);

/**
 * @brief Returns the value of a key of the x-www-form-urlencoded body of a request.
 *
 * @param[in] http_request the request
 * @param[in] key the key
 * @return the value, that must not be deleted; NULL for a missing key or a request without body values
 */
CERVER_EXPORT const String *http_request_body_get_value (
	const HttpRequest *http_request, const char *key
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
