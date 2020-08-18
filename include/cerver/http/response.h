#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/http/status.h"

#define RESPONSE_HEADERS_SIZE			4

#define RESPONSE_HEADER_MAP(XX)									\
	XX(0,  CONTENT_ENCODING,      Content-Encoding)       		\
	XX(1,  CONTENT_LANGUAGE,      Content-Language)          	\
	XX(2,  CONTENT_LENGTH,        Content-Length)         		\
	XX(3,  CONTENT_TYPE,		  Content-Type)         		\

typedef enum ResponseHeader {

	#define XX(num, name, string) RESPONSE_HEADER_##name = num,
	RESPONSE_HEADER_MAP(XX)
	#undef XX

} ResponseHeader;

CERVER_PUBLIC const char *http_response_header_str (ResponseHeader header);

typedef struct HttpResponse {

	http_status status;

	u8 n_headers;
	String *headers[RESPONSE_HEADERS_SIZE];

	void *header;
	size_t header_len;

	void *data;
	size_t data_len;

	void *res;
	size_t res_len;

} HttpResponse;

CERVER_PUBLIC HttpResponse *http_response_new (void);

// correctly deletes the response and all of its data
CERVER_PUBLIC void http_respponse_delete (HttpResponse *res);

// sets the http response's status code to be set in the header when compilling
CERVER_EXPORT void http_response_set_status (HttpResponse *res, http_status status);

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
CERVER_EXPORT void http_response_set_header (HttpResponse *res, void *header, size_t header_len);

// adds a new header to the response, the headers will be handled when calling 
// http_response_compile () to generate a continuos header buffer
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_add_header (HttpResponse *res, ResponseHeader type, const char *actual_header);

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
CERVER_EXPORT void http_response_set_data (HttpResponse *res, void *data, size_t data_len);

// creates a new http response with the specified status code
// ability to set the response's data (body); it will be copied to the response
// and the original data can be safely deleted 
CERVER_EXPORT HttpResponse *http_response_create (http_status status, const void *data, size_t data_len);

// merge the response header and the data into the final response
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_compile (HttpResponse *res);

// sends a response to the connection's socket
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_send (HttpResponse *res, struct _Cerver *cerver, struct _Connection *connection);

// creates & sends a response to the connection's socket
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_create_and_send (unsigned int status, const void *data, size_t data_len,
	struct _Cerver *cerver, struct _Connection *connection);

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { msg: "your message" }
CERVER_EXPORT HttpResponse *http_response_json_msg (http_status status, const char *msg);

// creates and sends a http json message response with the defined status code & message
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_msg_send (CerverReceive *cr, unsigned int status, const char *msg);

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { error: "your error message" }
CERVER_EXPORT HttpResponse *http_response_json_error (http_status status, const char *error_msg);

// creates and sends a http json error response with the defined status code & message
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_json_error_send (CerverReceive *cr, unsigned int status, const char *error_msg);

CERVER_PUBLIC void http_response_print (HttpResponse *res);

#endif