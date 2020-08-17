#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>

#include "cerver/types/String.h"

#include "cerver/http/response.h"
#include "cerver/http/json.h"

// TODO: get the size fo this when we start the server!!
const char *default_header = "HTTP/1.1 200 OK\r\n\n";

HttpResponse *http_response_new (void) {

    HttpResponse *res = (HttpResponse *) malloc (sizeof (HttpResponse));
    if (res) {
        memset (res, 0, sizeof (HttpResponse));
        res->data = NULL;
        res->header = NULL;
        res->res = NULL;
    } 

    return res;

}

HttpResponse *http_response_create (unsigned int status, const char *header, size_t header_len, 
    const char *data, size_t data_len) {

    HttpResponse *res = NULL;
    if (data) {
        res = (HttpResponse *) malloc (sizeof (HttpResponse));
        if (res) {
            memset (res, 0, sizeof (HttpResponse));

            res->status = status;
            res->data = (char *) calloc (data_len, sizeof (char));
            memcpy (res->data, data, data_len);
            res->data_len = data_len;

            if (header) {
                res->header_len = header_len;
                res->header = (char *) calloc (header_len, sizeof (char));
                memcpy (res->header, header, header_len);
            } 

            else {
                res->header_len = strlen (default_header);
                res->header = (char *) calloc (res->header_len, sizeof (default_header));
                memcpy (res->header, default_header, res->header_len);
            } 
        }
    }

    return res;

}

void http_respponse_delete (HttpResponse *res) {

    if (res) {
        if (res->data) free (res->data);
        if (res->header) free (res->header);
        if (res->res) free (res->res);

        free (res);
    }

}

// merge the response header and the data into the final response
void http_response_compile (HttpResponse *res) {

    if (res) {
        if (res->header) {
            res->res_len = res->header_len;
            if (res->data) res->res_len += res->data_len;

            res->res = (char *) calloc (res->res_len, sizeof (char));
            memcpy (res->res, res->header, res->res_len);

            if (res->data) strcat (res->res, res->data);
        }
    }

}

int http_response_send_to_socket (const HttpResponse *res, const int socket_fd) {

    int retval = 1;

    if (res && res->res) {
        retval = send (socket_fd, res->res, res->res_len, 0) <= 0 ? 1 : 0;
    } 

    return retval;

}

HttpResponse *http_response_json_error (const char *error_msg) {

    HttpResponse *res = NULL;

    if (error_msg) {
        String *error = str_new (error_msg);
        JsonKeyValue *jkvp = json_key_value_create ("error", error, VALUE_TYPE_STRING);
        size_t json_len;
        char *json = json_create_with_one_pair (jkvp, &json_len);
        json_key_value_delete (jkvp);
        res = http_response_create (200, NULL, 0, json, json_len);
        free (json);        // we copy the data into the response
    }

    return res;

}