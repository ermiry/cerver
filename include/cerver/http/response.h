#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/handler.h"

#include "cerver/http/content.h"
#include "cerver/http/headers.h"
#include "cerver/http/status.h"

#define HTTP_RESPONSE_POOL_INIT					32

#define HTTP_RESPONSE_CONTENT_LENGTH_SIZE		16

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpReceive;

#pragma region responses

struct _HttpResponse {

	http_status status;

	u8 n_headers;
	String *headers[HTTP_REQUEST_HEADERS_SIZE];

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

// sets the http response's status code to be set in the header when compilling
CERVER_EXPORT void http_response_set_status (
	HttpResponse *res, http_status status
);

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
CERVER_EXPORT void http_response_set_header (
	HttpResponse *res, void *header, size_t header_len
);

// adds a new header to the response, the headers will be handled when calling 
// http_response_compile () to generate a continuos header buffer
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_header (
	HttpResponse *res, const HttpHeader type, const char *actual_header
);

// adds a HTTP_HEADER_CONTENT_TYPE header to the response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_content_type_header (
	HttpResponse *res, const ContentType type
);

// adds a HTTP_HEADER_CONTENT_LENGTH header to the response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_content_length_header (
	HttpResponse *res, const size_t length
);

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
CERVER_EXPORT void http_response_set_data (
	HttpResponse *res, void *data, size_t data_len
);

// sets a reference to a data buffer to send
// data will not be copied into the response and will not be freed after use
// this method is similar to packet_set_data_ref ()
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_set_data_ref (
	HttpResponse *res, void *data, size_t data_size
);

// creates a new http response with the specified status code
// ability to set the response's data (body); it will be copied to the response
// and the original data can be safely deleted 
CERVER_EXPORT HttpResponse *http_response_create (
	unsigned int status, const void *data, size_t data_len
);

// uses the exiting response's values to correctly create a HTTP header in a continuos buffer
// ready to be sent from the request
CERVER_PUBLIC void http_response_compile_header (HttpResponse *res);

// merge the response header and the data into the final response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_compile (HttpResponse *res);

CERVER_PUBLIC void http_response_print (const HttpResponse *res);

#pragma endregion

#pragma region send

// sends a response to the connection's socket
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_send (
	HttpResponse *res,
	const struct _HttpReceive *http_receive
);

// expects a response with an already created header and data
// as this method will send both parts withput the need of a continuos response buffer
// use this for maximun efficiency
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_send_split (
	HttpResponse *res,
	const struct _HttpReceive *http_receive
);

// creates & sends a response to the connection's socket
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_create_and_send (
	unsigned int status, const void *data, size_t data_len,
	const struct _HttpReceive *http_receive
);

// sends a file directly to the connection
// this method is used when serving files from static paths & by  http_response_render_file ()
// returns 0 on success, 1 on error
CERVER_PRIVATE u8 http_response_send_file (
	const struct _HttpReceive *http_receive,
	int file, const char *filename,
	struct stat *filestatus
);

#pragma endregion

#pragma region render

// sends the selected text back to the user
// this methods takes care of generating a repsonse with text/html content type
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_render_text (
	const struct _HttpReceive *http_receive,
	const char *text, const size_t text_len
);

// sends the selected json back to the user
// this methods takes care of generating a repsonse with application/json content type
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_render_json (
	const struct _HttpReceive *http_receive,
	const char *json, const size_t json_len
);

// opens the selected file and sends it back to the user
// this method takes care of generating the header based on the file values
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_render_file (
	const struct _HttpReceive *http_receive,
	const char *filename
);

#pragma endregion

#pragma region json

CERVER_EXPORT HttpResponse *http_response_create_json (
	const http_status status, const char *json, const size_t json_len
);

CERVER_EXPORT HttpResponse *http_response_create_json_key_value (
	const http_status status, const char *key, const char *value
);

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { msg: "your message" }
CERVER_EXPORT HttpResponse *http_response_json_msg (
	http_status status, const char *msg
);

// creates and sends a http json message response with the defined status code & message
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_msg_send (
	const struct _HttpReceive *http_receive,
	unsigned int status, const char *msg
);

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { error: "your error message" }
CERVER_EXPORT HttpResponse *http_response_json_error (
	http_status status, const char *error_msg
);

// creates and sends a http json error response with the defined status code & message
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_error_send (
	const struct _HttpReceive *http_receive,
	unsigned int status, const char *error_msg
);

// creates a http response with the defined status code ready to be sent
// and a data (body) with a json meesage of type { key: value }
CERVER_EXPORT HttpResponse *http_response_json_key_value (
	http_status status, const char *key, const char *value
);

// creates and sends a http custom json response with the defined status code & key-value
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_key_value_send (
	const struct _HttpReceive *http_receive,
	unsigned int status, const char *key, const char *value
);

// creates a http response with the defined status code and the body with the custom json
CERVER_EXPORT HttpResponse *http_response_json_custom (
	http_status status, const char *json
);

// creates and sends a http custom json response with the defined status code
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_custom_send (
	const struct _HttpReceive *http_receive,
	unsigned int status, const char *json
);

// creates a http response with the defined status code
// and the body with a reference to a custom json
CERVER_EXPORT HttpResponse *http_response_json_custom_reference (
	http_status status,
	const char *json, const size_t json_len
);

// creates and sends a http custom json reference response with the defined status code
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_custom_reference_send (
	const struct _HttpReceive *http_receive,
	unsigned int status,
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