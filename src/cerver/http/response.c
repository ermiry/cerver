#include <stdlib.h>
#include <string.h>

#include "cerver/types/estring.h"

#include "cerver/http/status.h"
#include "cerver/http/response.h"
#include "cerver/http/json.h"

const char *default_header = "HTTP/1.1 200 OK\r\n\n";

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

HttpResponse *http_response_json_error (const char *error_msg) {

	HttpResponse *res = NULL;

	if (error_msg) {
		estring *error = estring_new (error_msg);
		JsonKeyValue *jkvp = json_key_value_create ("error", error, VALUE_TYPE_STRING);
		size_t json_len;
		char *json = json_create_with_one_pair (jkvp, &json_len);
		json_key_value_delete (jkvp);
		res = http_response_create (200, NULL, 0, json, json_len);
		free (json);        // we copy the data into the response
	}

	return res;

}