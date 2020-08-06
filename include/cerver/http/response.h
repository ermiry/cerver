#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include "cerver/http/status.h"

typedef struct HttpResponse {

	http_status status;

	void *header;
	size_t header_len;

	void *data;
	size_t data_len;

	void *res;
	size_t res_len;

} HttpResponse;

extern const char *default_header;

extern HttpResponse *http_response_new (void);

// correctly deletes the response and all of its data
extern void http_respponse_delete (HttpResponse *res);

// sets the http response's status code to be set in the header when compilling
extern void http_response_set_status (HttpResponse *res, http_status status);

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
extern void http_response_set_header (HttpResponse *res, void *header, size_t header_len);

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
extern void http_response_set_data (HttpResponse *res, void *data, size_t data_len);

// creates a new response model with the requested values
extern HttpResponse *http_response_create (unsigned int status, const char *header, size_t header_len, 
	const char *data, size_t data_len);

// merges the response header and the data into the final response
extern void http_response_compile (HttpResponse *res);

// sends the response directly to the socket
extern int http_response_send_to_socket (const HttpResponse *res, const int socket_fd);

// shortcut to create a response indicating an error message
extern HttpResponse *http_response_json_error (const char *error_msg);

#endif