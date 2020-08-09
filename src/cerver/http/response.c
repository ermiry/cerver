#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/cerver.h"

#include "cerver/http/status.h"
#include "cerver/http/response.h"
#include "cerver/http/json/json.h"

#include "cerver/utils/utils.h"

HttpResponse *http_response_new (void) {

	HttpResponse *res = (HttpResponse *) malloc (sizeof (HttpResponse));
	if (res) {
		res->status = HTTP_STATUS_OK;

		res->header = NULL;
		res->header_len = 0;

		res->data = NULL;
		res->data_len = 0;

		res->res = NULL;
		res->res_len = 0;
	} 

	return res;

}

void http_respponse_delete (HttpResponse *res) {

	if (res) {
		if (res->header) free (res->header);
		if (res->data) free (res->data);
		if (res->res) free (res->res);

		free (res);
	}

}

// sets the http response's status code to be set in the header when compilling
void http_response_set_status (HttpResponse *res, http_status status) {

	if (res) res->status = status;

}

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_header (HttpResponse *res, void *header, size_t header_len) {

	if (res) {
		if (res->header) {
			free (res->header);
			res->header = NULL;
		} 

		if (header) {
			res->header = header;
			res->header_len = header_len;
		}
	}

}

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_data (HttpResponse *res, void *data, size_t data_len) {

	if (res) {
		if (res->data) {
			free (res->data);
			res->data = NULL;
		}

		if (data) {
			res->data = data;
			res->data_len = data_len;
		}
	}

}

// creates a new http response with the specified status code
// ability to set the response's data (body); it will be copied to the response
// and the original data can be safely deleted 
HttpResponse *http_response_create (unsigned int status, const void *data, size_t data_len) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

		if (data) {
			res->data = malloc (data_len);
			if (res->data) {
				memcpy (res->data, data, data_len);
				res->data_len = data_len;
			}
		}
	}

	return res;

}

// merge the response header and the data into the final response
// returns 0 on success, 1 on error
u8 http_response_compile (HttpResponse *res) {

	u8 retval = 1;

	if (res) {
		if (!res->header) {
			res->header = c_string_create (
				"HTTP/1.1 %d %s\r\n\n", 
				res->status, http_status_str (res->status)
			);

			res->header_len = strlen ((const char *) res->header);
		}

		// compile into a continous buffer
		if (res->res) {
			free (res->res);
			res->res = NULL;
			res->res_len = 0;
		}

		res->res_len = res->header_len + res->data_len;
		res->res = malloc (res->res_len);
		if (res->res) {
			char *end = res->res;
			memcpy (end, res->header, res->header_len);

			if (res->data) {
				end += res->header_len;
				memcpy (end, res->data, res->data_len);
			}

			retval = 0;
		}
	}

	return retval;

}

static u8 http_response_send_actual (
    Socket *socket, 
    char *data, size_t data_size
) {

    u8 retval = 0;

    ssize_t sent = 0;
    char *p = data;
    while (data_size > 0) {
        sent = send (socket->sock_fd, p, data_size, 0);
        if (sent < 0) {
            retval = 1;
            break;
        }

        p += sent;
        // *actual_sent += (size_t) sent;
        data_size -= (size_t) sent;
    }

    return retval;

}

// sends a response to the connection's socket
// returns 0 on success, 1 on error
u8 http_response_send (HttpResponse *res, Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (res && connection) {
		if (res->res) {
			if (!http_response_send_actual (
				connection->socket,
				res->res, res->res_len
			)) {
				cerver->stats->total_bytes_sent += res->res_len;
				connection->stats->total_bytes_sent += res->res_len; 
			}
		}
	}

	return retval;

}

static HttpResponse *http_response_json_internal (http_status status, const char *key, const char *value) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

		res->header = c_string_create (
			"HTTP/1.1 %d %s\r\n\n", 
			res->status, http_status_str (res->status)
		);

		res->header_len = strlen ((const char *) res->header);

		// body
		json_t *json = json_pack ("{s:s}", key, value);
		if (json) {
			char *json_str = json_dumps (json, 0);
			if (json_str) {
				res->data = json_str;
				res->data_len = strlen (json_str);
			}

			json_delete (json);
		}

		(void) http_response_compile (res);
	}

	return res;

}

// creates an http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { msg: "your message" }
HttpResponse *http_response_json_msg (http_status status, const char *msg) {

	return msg ? http_response_json_internal (status, "msg", msg) : NULL;

}

// creates an http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { error: "your error message" }
HttpResponse *http_response_json_error (http_status status, const char *error_msg) {

	return error_msg ? http_response_json_internal (status, "error", error_msg) : NULL;

}

void http_response_print (HttpResponse *res) {

	if (res) {
		if (res->res) {
			printf ("\nResponse: %.*s\n\n", (int) res->res_len, (char *) res->res);
		}
	}

}