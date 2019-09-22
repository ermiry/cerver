#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include <stdlib.h>

typedef struct HttpResponse {

    unsigned int status;
    char *header;
    size_t header_len;
    char *data;
    size_t data_len;

    char *res;
    size_t res_len;

} HttpResponse;

extern const char *default_header;

extern HttpResponse *http_response_new (void);
extern HttpResponse *http_response_create (unsigned int status, const char *header, size_t header_len, 
    const char *data, size_t data_len);
extern void http_respponse_delete (HttpResponse *res);

// merge the response header and the data into the final response
extern void http_response_compile (HttpResponse *res);

extern int http_response_send_to_socket (const HttpResponse *res, const int socket_fd);

extern HttpResponse *http_response_json_error (const char *error_msg);

#endif