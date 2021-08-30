#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include <sys/stat.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/handler.h"

#include "cerver/http/content.h"
#include "cerver/http/headers.h"
#include "cerver/http/origin.h"
#include "cerver/http/status.h"
#include "cerver/http/utils.h"

#define HTTP_RESPONSE_POOL_INIT					32

#define HTTP_RESPONSE_MAIN_HEADER_SIZE			128

#define HTTP_RESPONSE_CONTENT_LENGTH_SIZE		16

#define HTTP_RESPONSE_SEND_FILE_HEADER_SIZE		256
#define HTTP_RESPONSE_RENDER_TEXT_HEADER_SIZE	256
#define HTTP_RESPONSE_RENDER_JSON_HEADER_SIZE	256

#define HTTP_RESPONSE_FILE_CHUNK_SIZE			2097152
#define HTTP_RESPONSE_VIDEO_CHUNK_SIZE			2097152

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpReceive;

#pragma region responses

struct _HttpResponse {

	http_status status;

	u8 n_headers;
	HttpHeader headers[HTTP_HEADERS_SIZE];

	void *header;
	size_t header_len;

	void *data;
	size_t data_len;
	bool data_ref;

	void *res;
	size_t res_len;

};

typedef struct _HttpResponse HttpResponse;

CERVER_PRIVATE void *http_response_new (void);

CERVER_PRIVATE void http_response_reset (HttpResponse *response);

// correctly deletes the response and all of its data
CERVER_PRIVATE void http_response_delete (void *res_ptr);

// get a new HTTP response ready to be used
CERVER_EXPORT HttpResponse *http_response_get (void);

// correctly disposes a HTTP response
CERVER_EXPORT void http_response_return (HttpResponse *response);

// sets the HTTP response's status code to be set in the header
// when compilling
CERVER_EXPORT void http_response_set_status (
	HttpResponse *response, const http_status status
);

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
CERVER_EXPORT void http_response_set_header (
	HttpResponse *response, const void *header, const size_t header_len
);

// adds a new header to the response
// the headers will be handled when calling 
// http_response_compile () to generate a continuos header buffer
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_header (
	HttpResponse *response,
	const http_header type, const char *actual_header
);

// works like http_response_add_header ()
// but generates the header values in the fly
CERVER_EXPORT u8 http_response_add_custom_header (
	HttpResponse *response,
	const http_header type, const char *format, ...
);

// adds a "Content-Type" header to the response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_content_type_header (
	HttpResponse *response, const ContentType type
);

// adds a "Content-Length" header to the response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_content_length_header (
	HttpResponse *response, const size_t length
);

// adds a "Content-Type" with value "application/json"
// adds a "Content-Length" header to the response
CERVER_EXPORT void http_response_add_json_headers (
	HttpResponse *response, const size_t json_len
);

// adds an "Access-Control-Allow-Origin" header to the response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_cors_header (
	HttpResponse *response, const char *origin
);

// works like http_response_add_cors_header ()
// but takes a HttpOrigin instead of a c string
CERVER_EXPORT u8 http_response_add_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
);

// works like http_response_add_cors_header () but first
// checks if the domain matches any entry in the whitelist
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_whitelist_cors_header (
	HttpResponse *response, const char *domain
);

// works like http_response_add_whitelist_cors_header ()
// but takes a HttpOrigin instead of a c string
CERVER_EXPORT u8 http_response_add_whitelist_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
);

// checks if the HTTP request's "Origin" header value
// matches any domain in the whitelist
// then adds an "Access-Control-Allow-Origin" header to the response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_whitelist_cors_header_from_request (
	const struct _HttpReceive *http_receive,
	HttpResponse *response
);

// sets CORS related header "Access-Control-Allow-Credentials"
// this header is needed when a CORS request has an "Authorization" header
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_cors_allow_credentials_header (
	HttpResponse *response
);

// sets CORS related header "Access-Control-Allow-Methods"
// to be a list of available methods like "GET, HEAD, OPTIONS"
// this header is needed in preflight OPTIONS request's responses
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_cors_allow_methods_header (
	HttpResponse *response, const char *methods
);

// adds a "Content-Range: bytes ${start}-${end}/${file_size}"
// header to the response
CERVER_PUBLIC u8 http_response_add_content_range_header (
	HttpResponse *response, const BytesRange *bytes_range
);

// adds an "Accept-Ranges" with value "bytes"
// adds a "Content-Type" with content type value
// adds a "Content-Length" with value of chunk_size
// adds a "Content-Range: bytes ${start}-${end}/${file_size}"
CERVER_PUBLIC void http_response_add_video_headers (
	HttpResponse *response,
	const ContentType content_type, const BytesRange *bytes_range
);

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
CERVER_EXPORT void http_response_set_data (
	HttpResponse *response, const void *data, const size_t data_len
);

// sets a reference to a data buffer to send
// data will not be copied into the response and will not be freed after use
// this method is similar to packet_set_data_ref ()
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_set_data_ref (
	HttpResponse *response, const void *data, const size_t data_size
);

// creates a new http response with the specified status code
// ability to set the response's data (body); it will be copied to the response
// and the original data can be safely deleted 
CERVER_EXPORT HttpResponse *http_response_create (
	const http_status status, const void *data, size_t data_len
);

// uses the exiting response's values to correctly
// create a HTTP header in a continuos buffer
// ready to be sent by the response
CERVER_PUBLIC void http_response_compile_header (
	HttpResponse *response
);

// merge the response header and the data into the final response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_compile (
	HttpResponse *response
);

CERVER_PUBLIC void http_response_print (
	const HttpResponse *response
);

#pragma endregion

#pragma region send

// sends a response to the connection's socket
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_send (
	HttpResponse *response,
	const struct _HttpReceive *http_receive
);

// expects a response with an already created header and data
// as this method will send both parts without the need of a continuos response buffer
// use this for maximun efficiency
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_send_split (
	HttpResponse *response,
	const struct _HttpReceive *http_receive
);

// creates & sends a response through the connection's socket
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_create_and_send (
	const http_status status, const void *data, size_t data_len,
	const struct _HttpReceive *http_receive
);

CERVER_PRIVATE u8 http_response_send_file_internal (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *filename,
	struct stat *filestatus
);

// opens the selected file and sends it back to the client
// takes care of generating the header based on the file values
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_send_file (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *filename
);

// works like http_response_send_file ()
// but generates filename on the fly
CERVER_EXPORT u8 http_response_send_file_generate (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *format, ...
);

#pragma endregion

#pragma region render

// sends the selected text back to the user
// this methods takes care of generating a repsonse with text/html content type
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_render_text (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *text, const size_t text_len
);

// sends the selected json back to the user
// this methods takes care of generating a repsonse with application/json content type
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_render_json (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
);

#pragma endregion

#pragma region videos

// handles the transmission of a video to the client
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_handle_video (
	const struct _HttpReceive *http_receive,
	const char *filename
);

// works like http_response_handle_video ()
// but generates filename on the fly
CERVER_EXPORT u8 http_response_handle_video_generate (
	const struct _HttpReceive *http_receive,
	const char *format, ...
);

#pragma endregion

#pragma region json

// creates a HTTP response with the defined status code
// with a custom json message body
// returns a new HTTP response instance ready to be sent
CERVER_EXPORT HttpResponse *http_response_create_json (
	const http_status status, const char *json, const size_t json_len
);

// creates a HTTP response with the defined status code and a data (body)
// with a json message of type { key: value } that is ready to be sent
// returns a new HTTP response instance
CERVER_EXPORT HttpResponse *http_response_create_json_key_value (
	const http_status status, const char *key, const char *value
);

// creates a HTTP response with the defined status code
// with a json body of type { "key": int_value }
// returns a new HTTP response instance ready to be sent
CERVER_EXPORT HttpResponse *http_response_json_int_value (
	const http_status status, const char *key, const int value
);

// sends a HTTP response with custom status code
// with a json body of type { "key": int_value }
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_int_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const int value
);

// creates a HTTP response with the defined status code
// with a json body of type { "key": large_int_value }
// returns a new HTTP response instance ready to be sent
CERVER_EXPORT HttpResponse *http_response_json_large_int_value (
	const http_status status, const char *key, const long value
);

// sends a HTTP response with custom status code
// with a json body of type { "key": large_int_value }
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_large_int_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const long value
);

// creates a HTTP response with the defined status code
// with a json body of type { "key": double_value }
// returns a new HTTP response instance ready to be sent
CERVER_EXPORT HttpResponse *http_response_json_real_value (
	const http_status status, const char *key, const double value
);

// sends a HTTP response with custom status code
// with a json body of type { "key": double_value }
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_real_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const double value
);

// creates a HTTP response with the defined status code
// with a json body of type { "key": bool_value }
// returns a new HTTP response instance ready to be sent
CERVER_EXPORT HttpResponse *http_response_json_bool_value (
	const http_status status, const char *key, const bool value
);

// sends a HTTP response with custom status code
// with a json body of type { "key": bool_value }
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_bool_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const bool value
);

// creates a HTTP response with the defined status code and a data (body)
// with a json message of type { msg: "your message" } ready to be sent
// returns a new HTTP response instance
CERVER_EXPORT HttpResponse *http_response_json_msg (
	const http_status status, const char *msg
);

// creates and sends a HTTP json message response with the defined status code & message
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_msg_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *msg
);

// creates a HTTP response with the defined status code and a data (body)
// with a json message of type { error: "your error message" } ready to be sent
// returns a new HTTP response instance
CERVER_EXPORT HttpResponse *http_response_json_error (
	const http_status status, const char *error_msg
);

// creates and sends a HTTP json error response with the defined status code & message
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_error_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *error_msg
);

// creates a HTTP response with the defined status code and a data (body)
// with a json meesage of type { key: value } ready to be sent
// returns a new HTTP response instance
CERVER_EXPORT HttpResponse *http_response_json_key_value (
	const http_status status, const char *key, const char *value
);

// creates and sends a HTTP custom json response with the defined status code & key-value
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_key_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const char *value
);

// creates a http response with the defined status code and the body with the custom json
CERVER_EXPORT HttpResponse *http_response_json_custom (
	const http_status status, const char *json
);

// creates and sends a http custom json response with the defined status code
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_custom_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *json
);

// creates a http response with the defined status code
// and the body with a reference to a custom json
CERVER_EXPORT HttpResponse *http_response_json_custom_reference (
	const http_status status,
	const char *json, const size_t json_len
);

// creates and sends a http custom json reference response with the defined status code
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_custom_reference_send (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
);

#pragma endregion

#pragma region main

CERVER_PRIVATE unsigned int http_responses_init (
	const struct _HttpCerver *http_cerver
);

CERVER_PRIVATE void http_responses_end (void);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif