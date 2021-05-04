#include <stdlib.h>
#include <string.h>

#include <sys/sendfile.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/pool.h"

#include "cerver/version.h"
#include "cerver/cerver.h"
#include "cerver/files.h"

#include "cerver/http/content.h"
#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/response.h"
#include "cerver/http/status.h"

#include "cerver/http/json/json.h"

#include "cerver/utils/utils.h"

typedef struct HttpResponseProducer {

	Pool *pool;
	const HttpCerver *http_cerver;

} HttpResponseProducer;

static HttpResponseProducer *producer = NULL;

static HttpResponseProducer *http_response_producer_new (
	const HttpCerver *http_cerver
) {

	HttpResponseProducer *producer = (HttpResponseProducer *) malloc (
		sizeof (HttpResponseProducer)
	);

	if (producer) {
		producer->pool = NULL;
		producer->http_cerver = http_cerver;
	}

	return producer;

}

static void http_response_producer_delete (
	HttpResponseProducer *producer
) {

	if (producer) {
		pool_delete (producer->pool);
		producer->pool = NULL;

		producer->http_cerver = NULL;

		free (producer);
	}

}

#pragma region responses

void *http_response_new (void) {

	HttpResponse *res = (HttpResponse *) malloc (sizeof (HttpResponse));
	if (res) {
		res->status = HTTP_STATUS_OK;

		res->n_headers = 0;
		for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++)
			res->headers[i] = NULL;

		res->header = NULL;
		res->header_len = 0;

		res->data = NULL;
		res->data_len = 0;
		res->data_ref = false;

		res->res = NULL;
		res->res_len = 0;
	} 

	return res;

}

void http_response_reset (HttpResponse *response) {

	response->status = HTTP_STATUS_OK;

	response->n_headers = 0;
	for (u8 i = 0; i < HTTP_HEADERS_SIZE; i++) {
		str_delete (response->headers[i]);
		response->headers[i] = NULL;
	}

	response->header_len = 0;
	if (response->header) {
		free (response->header);
		response->header = NULL;
	}

	if (!response->data_ref) {
		if (response->data) free (response->data);
	}

	response->data = NULL;
	response->data_len = 0;
	response->data_ref = false;

	response->res_len = 0;
	if (response->res) {
		free (response->res);
		response->res = NULL;
	}

}

void http_response_delete (void *res_ptr) {

	if (res_ptr) {
		http_response_reset ((HttpResponse *) res_ptr);

		free (res_ptr);
	}

}

// get a new HTTP response ready to be used
HttpResponse *http_response_get (void) {

	return (HttpResponse *) pool_pop (producer->pool);

}

// correctly disposes a HTTP response
void http_response_return (HttpResponse *response) {

	if (response) {
		http_response_reset (response);

		(void) pool_push (producer->pool, response);
	}

}

// sets the HTTP response's status code to be used in the header
void http_response_set_status (
	HttpResponse *res, const http_status status
) {

	if (res) res->status = status;

}

// sets the response's header, it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_header (
	HttpResponse *res, void *header, size_t header_len
) {

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

// adds a new header to the response, the headers will be handled when calling 
// http_response_compile () to generate a continuos header buffer
// returns 0 on success, 1 on error
u8 http_response_add_header (
	HttpResponse *res, const HttpHeader type, const char *actual_header
) {

	u8 retval = 1;

	if (res && actual_header && (type < HTTP_HEADERS_SIZE)) {
		if (res->headers[type]) {
			str_delete (res->headers[type]);
		}

		else {
			res->n_headers += 1;
		}
		
		res->headers[type] = str_create (
			"%s: %s\r\n",
			http_header_string (type), actual_header
		);

		retval = 0;
	}

	return retval;

}

// adds a HTTP_HEADER_CONTENT_TYPE header to the response
// returns 0 on success, 1 on error
u8 http_response_add_content_type_header (
	HttpResponse *res, const ContentType type
) {

	return http_response_add_header (
		res, HTTP_HEADER_CONTENT_TYPE, http_content_type_mime (type)
	);

}

// adds a HTTP_HEADER_CONTENT_LENGTH header to the response
// returns 0 on success, 1 on error
u8 http_response_add_content_length_header (
	HttpResponse *res, const size_t length
) {

	char buffer[HTTP_RESPONSE_CONTENT_LENGTH_SIZE] = { 0 };
	(void) snprintf (
		buffer, HTTP_RESPONSE_CONTENT_LENGTH_SIZE - 1,
		"%lu", length
	);

	return http_response_add_header (
		res, HTTP_HEADER_CONTENT_LENGTH, buffer
	);

}

// sets the response's data (body), it will replace the existing one
// the data will be deleted when the response gets deleted
void http_response_set_data (
	HttpResponse *res, void *data, size_t data_len
) {

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

// sets a reference to a data buffer to send
// data will not be copied into the response and will not be freed after use
// this method is similar to packet_set_data_ref ()
// returns 0 on success, 1 on error
u8 http_response_set_data_ref (
	HttpResponse *res, void *data, size_t data_size
) {

	u8 retval = 1;

	if (res && data) {
		if (!res->data_ref) {
			if (res->data) free (res->data);
		}

		res->data = data;
		res->data_len = data_size;
		res->data_ref = true;

		retval = 0;
	}

	return retval;

}

// creates a new http response with the specified status code
// ability to set the response's data (body); it will be copied to the response
// and the original data can be safely deleted 
HttpResponse *http_response_create (
	const http_status status, const void *data, size_t data_len
) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

		if (data) {
			res->data = malloc (data_len);
			if (res->data) {
				(void) memcpy (res->data, data, data_len);
				res->data_len = data_len;
			}
		}
	}

	return res;

}

// uses the exiting response's values to correctly create a HTTP header in a continuos buffer
// ready to be sent from the request
void http_response_compile_header (HttpResponse *res) {

	if (res->n_headers || producer->http_cerver->n_response_headers) {
		char *main_header = c_string_create (
			"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\n", 
			res->status, http_status_str (res->status),
			CERVER_VERSION
		);

		size_t main_header_len = strlen (main_header);
		res->header_len = main_header_len;

		u8 i = 0;
		for (; i < HTTP_HEADERS_SIZE; i++) {
			if (res->headers[i]) {
				res->header_len += res->headers[i]->len;
			}

			else if (producer->http_cerver->response_headers[i]) {
				res->header_len += producer->http_cerver->response_headers[i]->len;
			}
		}

		res->header_len += 2;	// \r\n

		res->header = calloc (res->header_len, sizeof (char));

		char *end = (char *) res->header;
		(void) memcpy (end, main_header, main_header_len);
		end += main_header_len;
		for (i = 0; i < HTTP_HEADERS_SIZE; i++) {
			if (res->headers[i]) {
				(void) memcpy (end, res->headers[i]->str, res->headers[i]->len);
				end += res->headers[i]->len;
			}

			else if (producer->http_cerver->response_headers[i]) {
				(void) memcpy (
					end,
					producer->http_cerver->response_headers[i]->str,
					producer->http_cerver->response_headers[i]->len
				);
				
				end += producer->http_cerver->response_headers[i]->len;
			}
		}

		// append header end
		*end = '\r';
		end += 1;
		*end = '\n';

		free (main_header);
	}

	// create the default header
	else {
		res->header = c_string_create (
			"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\n\r\n", 
			res->status, http_status_str (res->status),
			CERVER_VERSION
		);

		res->header_len = strlen ((const char *) res->header);
	}

}

// merge the response header and the data into the final response
// returns 0 on success, 1 on error
u8 http_response_compile (HttpResponse *res) {

	u8 retval = 1;

	if (res) {
		if (!res->header) {
			// res->header = c_string_create (
			// 	"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\n\r\n", 
			// 	res->status, http_status_str (res->status),
			// 	CERVER_VERSION
			// );

			// res->header_len = strlen ((const char *) res->header);

			http_response_compile_header (res);
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
			char *end = (char *) res->res;
			(void) memcpy (end, res->header, res->header_len);

			if (res->data) {
				end += res->header_len;
				(void) memcpy (end, res->data, res->data_len);
			}

			retval = 0;
		}
	}

	return retval;

}

void http_response_print (const HttpResponse *res) {

	if (res) {
		if (res->res) {
			(void) printf (
				"\n%.*s\n\n",
				(int) res->res_len, (char *) res->res
			);
		}
	}

}

#pragma endregion

#pragma region send

static u8 http_response_send_actual (
	Socket *socket, 
	const char *data, size_t data_size
) {

	u8 retval = 0;

	ssize_t sent = 0;
	char *p = (char *) data;
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
u8 http_response_send (
	HttpResponse *res,
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	if (res && http_receive->cr->connection) {
		if (res->res) {
			if (!http_response_send_actual (
				http_receive->cr->connection->socket,
				(char *) res->res, res->res_len
			)) {
				if (http_receive->cr->cerver)
					http_receive->cr->cerver->stats->total_bytes_sent += res->res_len;

				http_receive->cr->connection->stats->total_bytes_sent += res->res_len;

				((HttpReceive *) http_receive)->status = res->status;
				((HttpReceive *) http_receive)->sent = res->res_len;

				retval = 0;
			}
		}
	}

	return retval;

}

// expects a response with an already created header and data
// as this method will send both parts withput the need of a continuos response buffer
// use this for maximun efficiency
// returns 0 on success, 1 on error
u8 http_response_send_split (
	HttpResponse *res,
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	if (res && http_receive->cr->connection) {
		if (res->header && res->data) {
			if (!http_response_send_actual (
				http_receive->cr->connection->socket,
				(char *) res->header, res->header_len
			)) {
				if (!http_response_send_actual (
					http_receive->cr->connection->socket,
					(char *) res->data, res->data_len
				)) {
					size_t total_size = res->header_len + res->data_len;

					if (http_receive->cr->cerver)
						http_receive->cr->cerver->stats->total_bytes_sent += total_size;

					http_receive->cr->connection->stats->total_bytes_sent += total_size;

					((HttpReceive *) http_receive)->status = res->status;
					((HttpReceive *) http_receive)->sent = total_size;

					retval = 0;
				}
			}
		}
	}

	return retval;

}

// creates & sends a response to the connection's socket
// returns 0 on success, 1 on error
u8 http_response_create_and_send (
	const http_status status, const void *data, size_t data_len,
	const HttpReceive *http_receive
) {

	u8 retval = 1;

	HttpResponse *res = http_response_create (status, data, data_len);
	if (res) {
		if (!http_response_compile (res)) {
			retval = http_response_send (res, http_receive);
		}
		
		http_response_delete (res);
	}

	return retval;

}

// sends a file directly to the connection
// this method is used when serving files from static paths & by  http_response_render_file ()
// returns 0 on success, 1 on error
u8 http_response_send_file (
	const HttpReceive *http_receive,
	const http_status status,
	int file, const char *filename,
	struct stat *filestatus
) {
	
	u8 retval = 1;

	unsigned int ext_len = 0;
	const char *ext = files_get_file_extension_reference (
		filename, &ext_len
	);

	if (ext) {
		const char *content_type = http_content_type_mime_by_extension (ext);

		// prepare & send the header
		char header[HTTP_RESPONSE_SEND_FILE_HEADER_SIZE] = { 0 };
		size_t header_len = (size_t) snprintf (
			header, HTTP_RESPONSE_SEND_FILE_HEADER_SIZE - 1,
			"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n", 
			status,
			http_status_str (status),
			CERVER_VERSION,
			content_type,
			filestatus->st_size
		);

		#ifdef HTTP_RESPONSE_DEBUG
		cerver_log_msg ("\n%s\n", header);
		#endif

		if (!http_response_send_actual (
			http_receive->cr->connection->socket,
			header, header_len
		)) {
			size_t total_size = header_len;

			if (http_receive->cr->cerver)
				http_receive->cr->cerver->stats->total_bytes_sent += total_size;

			http_receive->cr->connection->stats->total_bytes_sent += total_size;

			// send the actual file
			if (!sendfile (
				http_receive->cr->connection->socket->sock_fd, 
				file, 
				0, filestatus->st_size
			)) {
				total_size += (size_t) filestatus->st_size;
			}

			((HttpReceive *) http_receive)->status = status;
			((HttpReceive *) http_receive)->sent = total_size;

			retval = 0;
		}
	}

	return retval;

}

#pragma endregion

#pragma region render

static u8 http_response_render_send (
	const HttpReceive *http_receive,
	const http_status status,
	const char *header, const size_t header_len,
	const char *data, const size_t data_len
) {

	u8 retval = 1;

	if (!http_response_send_actual (
		http_receive->cr->connection->socket,
		header, header_len
	)) {
		if (!http_response_send_actual (
			http_receive->cr->connection->socket,
			data, data_len
		)) {
			size_t total_size = header_len + data_len;
			if (http_receive->cr->cerver)
				http_receive->cr->cerver->stats->total_bytes_sent += total_size;

			http_receive->cr->connection->stats->total_bytes_sent += total_size;

			((HttpReceive *) http_receive)->status = status;
			((HttpReceive *) http_receive)->sent = total_size;

			retval = 0;
		}
	}

	return retval;

}

// sends the selected text back to the user
// this methods takes care of generating a repsonse with text/html content type
// returns 0 on success, 1 on error
u8 http_response_render_text (
	const HttpReceive *http_receive,
	const http_status status,
	const char *text, const size_t text_len
) {

	u8 retval = 1;

	if (http_receive && text) {
		char header[HTTP_RESPONSE_RENDER_TEXT_HEADER_SIZE] = { 0 };
		size_t header_len = (size_t) snprintf (
			header, HTTP_RESPONSE_RENDER_TEXT_HEADER_SIZE - 1,
			"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %lu\r\n\r\n", 
			status,
			http_status_str (status),
			CERVER_VERSION,
			text_len
		);

		#ifdef HTTP_RESPONSE_DEBUG
		cerver_log_msg ("\n%s\n", header);
		#endif

		retval = http_response_render_send (
			http_receive,
			status,
			header, header_len,
			text, text_len
		);
	}

	return retval;

}

// sends the selected json back to the user
// this methods takes care of generating a repsonse with application/json content type
// returns 0 on success, 1 on error
u8 http_response_render_json (
	const HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
) {

	u8 retval = 1;

	if (http_receive && json) {
		char header[HTTP_RESPONSE_RENDER_JSON_HEADER_SIZE] = { 0 };
		size_t header_len = (size_t) snprintf (
			header, HTTP_RESPONSE_RENDER_JSON_HEADER_SIZE - 1,
			"HTTP/1.1 %u %s\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\nContent-Length: %lu\r\n\r\n", 
			status,
			http_status_str (status),
			CERVER_VERSION,
			json_len
		);

		#ifdef HTTP_RESPONSE_DEBUG
		cerver_log_msg ("\n%s\n", header);
		#endif

		retval = http_response_render_send (
			http_receive,
			status,
			header, header_len,
			json, json_len
		);
	}

	return retval;

}

// opens the selected file and sends it back to the user
// this method takes care of generating the header based on the file values
// returns 0 on success, 1 on error
u8 http_response_render_file (
	const HttpReceive *http_receive,
	const http_status status,
	const char *filename
) {

	u8 retval = 1;

	if (http_receive && filename) {
		// check if file exists
		struct stat filestatus = { 0 };
		int file = file_open_as_fd (filename, &filestatus, O_RDONLY);
		if (file > 0) {
			retval = http_response_send_file (
				http_receive, status,
				file, filename, &filestatus
			);

			(void) close (file);
		}
	}

	return retval;

}

#pragma endregion

#pragma region json

static void http_response_create_json_common (
	HttpResponse *res, const http_status status,
	const char *json, const size_t json_len
) {

	res->status = status;

	// add headers
	http_response_add_content_type_header (res, HTTP_CONTENT_TYPE_JSON);
	http_response_add_content_length_header (res, json_len);

	// add body
	res->data = (char *) json;
	res->data_len = json_len;
	res->data_ref = true;

	// generate response
	(void) http_response_compile (res);

}

HttpResponse *http_response_create_json (
	const http_status status, const char *json, const size_t json_len
) {

	HttpResponse *res = NULL;

	if (json) {
		res = http_response_new ();
		if (res) {
			http_response_create_json_common (
				res, status,
				json, json_len
			);
		}
	}

	return res;

}

HttpResponse *http_response_create_json_key_value (
	const http_status status, const char *key, const char *value
) {
	
	HttpResponse *res = NULL;

	if (key && value) {
		res = http_response_new ();
		if (res) {
			char *json = c_string_create ("{\"%s\": \"%s\"}", key, value);
			if (json) {
				http_response_create_json_common (
					res, status,
					json, strlen (json)
				);

				free (json);
			}
		}
	}

	return res;

}

static HttpResponse *http_response_json_internal (
	const http_status status, const char *key, const char *value
) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

		// body
		char *json = c_string_create ("{\"%s\": \"%s\"}", key, value);
		if (json) {
			res->data = json;
			res->data_len = strlen (json);
		}

		// header
		// http_response_add_header (res, RESPONSE_HEADER_CONTENT_TYPE, "application/json");

		// char json_len[16] = { 0 };
		// snprintf (json_len, 16, "%ld", res->data_len);
		// http_response_add_header (res, RESPONSE_HEADER_CONTENT_LENGTH, json_len);

		res->header = c_string_create (
			"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n", 
			res->status, http_status_str (res->status),
			CERVER_VERSION,
			res->data_len
		);

		res->header_len = strlen ((const char *) res->header);

		(void) http_response_compile (res);
	}

	return res;

}

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { msg: "your message" }
HttpResponse *http_response_json_msg (
	const http_status status, const char *msg
) {

	return msg ? http_response_json_internal (status, "msg", msg) : NULL;

}

// creates and sends a http json message response with the defined status code & message
// returns 0 on success, 1 on error
u8 http_response_json_msg_send (
	const HttpReceive *http_receive,
	const http_status status, const char *msg
) {

	u8 retval = 1;

	HttpResponse *res = http_response_json_msg (status, msg);
	if (res) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (res);
		#endif
		retval = http_response_send (res, http_receive);
		http_response_delete (res);
	}

	return retval;

}

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { error: "your error message" }
HttpResponse *http_response_json_error (
	const http_status status, const char *error_msg
) {

	return error_msg ? http_response_json_internal (status, "error", error_msg) : NULL;

}

// creates and sends a http json error response with the defined status code & message
// returns 0 on success, 1 on error
u8 http_response_json_error_send (
	const HttpReceive *http_receive,
	const http_status status, const char *error_msg
) {

	u8 retval = 1;

	HttpResponse *res = http_response_json_error (status, error_msg);
	if (res) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (res);
		#endif
		retval = http_response_send (res, http_receive);
		http_response_delete (res);
	}

	return retval;

}

// creates a http response with the defined status code ready to be sent
// and a data (body) with a json meesage of type { key: value }
HttpResponse *http_response_json_key_value (
	const http_status status, const char *key, const char *value
) {

	return (key && value) ? http_response_json_internal (status, key, value) : NULL;

}

// creates and sends a http custom json response with the defined status code & key-value
// returns 0 on success, 1 on error
u8 http_response_json_key_value_send (
	const HttpReceive *http_receive,
	const http_status status, const char *key, const char *value
) {

	u8 retval = 1;

	HttpResponse *res = http_response_json_key_value (status, key, value);
	if (res) {
		#ifdef HTTP_RESPONSE_DEBUG
		http_response_print (res);
		#endif
		retval = http_response_send (res, http_receive);
		http_response_delete (res);
	}

	return retval;

}

static HttpResponse *http_response_json_custom_internal (
	const http_status status, const char *json
) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

		// body
		res->data = strdup (json);
		res->data_len = strlen (json);

		// header
		res->header = c_string_create (
			"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n", 
			res->status, http_status_str (res->status),
			CERVER_VERSION,
			res->data_len
		);

		res->header_len = strlen ((const char *) res->header);
	}

	return res;

}

// creates a http response with the defined status code and the body with the custom json
HttpResponse *http_response_json_custom (
	const http_status status, const char *json
) {

	HttpResponse *res = NULL;
	if (json) {
		res = http_response_json_custom_internal (status, json);

		(void) http_response_compile (res);	
	}

	return res;

}

// creates and sends a http custom json response with the defined status code
// returns 0 on success, 1 on error
u8 http_response_json_custom_send (
	const HttpReceive *http_receive,
	const http_status status, const char *json
) {

	u8 retval = 1;

	if (http_receive && json) {
		HttpResponse *res = http_response_json_custom_internal (status, json);
		if (res) {
			#ifdef HTTP_RESPONSE_DEBUG
			printf ("\n%.*s", (int) res->header_len, (char *) res->header);
			printf ("\n%.*s\n\n", (int) res->data_len, (char *) res->data);
			#endif
			retval = http_response_send_split (res, http_receive);
			http_response_delete (res);
		}
	}

	return retval;

}

static HttpResponse *http_response_json_custom_reference_internal (
	const http_status status,
	const char *json, const size_t json_len
) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

		// body
		res->data_ref = true;
		res->data = (char *) json;
		res->data_len = json_len;

		// header
		res->header = c_string_create (
			"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n", 
			res->status, http_status_str (res->status),
			CERVER_VERSION,
			res->data_len
		);

		res->header_len = strlen ((const char *) res->header);
	}

	return res;

}

// creates a http response with the defined status code
// and the body with a reference to a custom json
HttpResponse *http_response_json_custom_reference (
	const http_status status,
	const char *json, const size_t json_len
) {

	HttpResponse *res = NULL;
	if (json) {
		res = http_response_json_custom_reference_internal (
			status,
			json, json_len
		);
		
		(void) http_response_compile (res);
	}

	return res;

}

// creates and sends a http custom json reference response with the defined status code
// returns 0 on success, 1 on error
u8 http_response_json_custom_reference_send (
	const HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
) {

	u8 retval = 1;

	if (http_receive && json) {
		HttpResponse *res = http_response_json_custom_reference_internal (
			status,
			json, json_len
		);

		if (res) {
			#ifdef HTTP_RESPONSE_DEBUG
			printf ("\n%.*s", (int) res->header_len, (char *) res->header);
			printf ("\n%.*s\n\n", (int) res->data_len, (char *) res->data);
			#endif
			retval = http_response_send_split (res, http_receive);
			http_response_delete (res);
		}
	}

	return retval;

}

#pragma endregion

#pragma region main

static unsigned int http_responses_init_pool (void) {

	unsigned int retval = 1;

	producer->pool = pool_create (http_response_delete);
	if (producer->pool) {
		pool_set_create (producer->pool, http_response_new);
		pool_set_produce_if_empty (producer->pool, true);
		if (!pool_init (
			producer->pool, http_response_new, HTTP_RESPONSE_POOL_INIT
		)) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to init HTTP responses pool!");
		}
	}

	else {
		cerver_log_error ("Failed to create HTTP responses pool!");
	}

	return retval;

}

unsigned int http_responses_init (
	const HttpCerver *http_cerver
) {

	unsigned int retval = 1;

	producer = http_response_producer_new (http_cerver);
	if (producer) {
		retval = http_responses_init_pool ();
	}

	return retval;

}

void http_responses_end (void) {

	http_response_producer_delete (producer);
	producer = NULL;

}

#pragma endregion