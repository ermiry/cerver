#include <stdlib.h>
#include <string.h>

#include <sys/sendfile.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/version.h"
#include "cerver/cerver.h"
#include "cerver/files.h"

#include "cerver/http/http.h"
#include "cerver/http/status.h"
#include "cerver/http/response.h"
#include "cerver/http/json/json.h"

#include "cerver/utils/utils.h"

const char *http_response_header_str (ResponseHeader header) {

	switch (header) {
		#define XX(num, name, string) case RESPONSE_HEADER_##name: return #string;
		RESPONSE_HEADER_MAP(XX)
		#undef XX

		default: return "<unknown>";
	}

}

#pragma region main

HttpResponse *http_response_new (void) {

	HttpResponse *res = (HttpResponse *) malloc (sizeof (HttpResponse));
	if (res) {
		res->status = HTTP_STATUS_OK;

		res->n_headers = 0;
		for (u8 i = 0; i < RESPONSE_HEADERS_SIZE; i++)
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

void http_respponse_delete (HttpResponse *res) {

	if (res) {
		for (u8 i = 0; i < RESPONSE_HEADERS_SIZE; i++)
			str_delete (res->headers[i]);

		if (res->header) free (res->header);

		if (!res->data_ref) {
			if (res->data) free (res->data);
		}

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

// adds a new header to the response, the headers will be handled when calling 
// http_response_compile () to generate a continuos header buffer
// returns 0 on success, 1 on error
u8 http_response_add_header (HttpResponse *res, ResponseHeader type, const char *actual_header) {

	u8 retval = 1;

	if (res && actual_header && (type < RESPONSE_HEADERS_SIZE)) {
		if (res->headers[type]) str_delete (res->headers[type]);
		else res->n_headers += 1;
		
		res->headers[type] = str_create ("%s: %s\n", http_response_header_str (type), actual_header);
	}

	return retval;

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

// sets a reference to a data buffer to send
// data will not be copied into the response and will not be freed after use
// this method is similar to packet_set_data_ref ()
// returns 0 on success, 1 on error
u8 http_response_set_data_ref (HttpResponse *res, void *data, size_t data_size) {

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
HttpResponse *http_response_create (unsigned int status, const void *data, size_t data_len) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = (http_status) status;

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

// uses the exiting response's values to correctly create a HTTP header in a continuos buffer
// ready to be sent from the request
void http_response_compile_header (HttpResponse *res) {

	if (res->n_headers) {
		char *main_header = c_string_create (
			"HTTP/1.1 %d %s\r\nServer: Cerver/%s\r\n", 
			res->status, http_status_str (res->status),
			CERVER_VERSION
		);

		size_t main_header_len = strlen (main_header);
		res->header_len = main_header_len;

		u8 i = 0;
		for (; i < RESPONSE_HEADERS_SIZE; i++) {
			if (res->headers[i]) res->header_len += res->headers[i]->len;
		}

		res->header_len += 1;	// \n

		res->header = calloc (res->header_len, sizeof (char));

		char *end = (char *) res->header;
		memcpy (end, main_header, main_header_len);
		end += main_header_len;
		for (i = 0; i < RESPONSE_HEADERS_SIZE; i++) {
			if (res->headers[i]) {
				memcpy (end, res->headers[i]->str, res->headers[i]->len);
				end += res->headers[i]->len;
			}
		}

		// append header end
		*end = '\n';
		// end += 1;
		// *end = '\0';

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
u8 http_response_send (HttpResponse *res, Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (res && connection) {
		if (res->res) {
			if (!http_response_send_actual (
				connection->socket,
				(char *) res->res, res->res_len
			)) {
				if (cerver) cerver->stats->total_bytes_sent += res->res_len;
				connection->stats->total_bytes_sent += res->res_len; 

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
u8 http_response_send_split (HttpResponse *res, Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	if (res && connection) {
		if (res->header && res->data) {
			if (!http_response_send_actual (
				connection->socket,
				(char *) res->header, res->header_len
			)) {
				if (!http_response_send_actual (
					connection->socket,
					(char *) res->data, res->data_len
				)) {
					size_t total_size = res->header_len + res->data_len;
					if (cerver) cerver->stats->total_bytes_sent += total_size;
					connection->stats->total_bytes_sent += total_size;

					retval = 0;
				}
			}
		}
	}

	return retval;

}

// creates & sends a response to the connection's socket
// returns 0 on success, 1 on error
u8 http_response_create_and_send (unsigned int status, const void *data, size_t data_len,
	Cerver *cerver, Connection *connection) {

	u8 retval = 1;

	HttpResponse *res = http_response_create (status, data, data_len);
	if (res) {
		if (!http_response_compile (res)) {
			retval = http_response_send (res, cerver, connection);
		}
		
		http_respponse_delete (res);
	}

	return retval;

}

// sends a file directly to the connection
// this method is used when serving files from static paths & by  http_response_render_file ()
// returns 0 on success, 1 on error
u8 http_response_send_file (CerverReceive *cr, int file, const char *filename, struct stat *filestatus) {
	
	u8 retval = 1;

	char *ext = files_get_file_extension (filename);
	if (ext) {
		const char *content_type = http_content_type_by_extension (ext);

		// prepare & send the header
		char *header = c_string_create (
			"HTTP/1.1 200 %s\r\nServer: Cerver/%s\r\nContent-Type: %s\r\nContent-Length: %ld\r\n\r\n", 
			http_status_str ((enum http_status) 200),
			CERVER_VERSION,
			content_type,
			filestatus->st_size
		);

		if (header) {
			// printf ("\n%s\n", header);

			if (!http_response_send_actual (
				cr->connection->socket,
				header, strlen (header)
			)) {
				// send the actual file
				sendfile (
					cr->connection->socket->sock_fd, 
					file, 
					0, filestatus->st_size
				);

				retval = 0;
			}

			free (header);
		}

		free (ext);
	}

	return retval;

}

void http_response_print (HttpResponse *res) {

	if (res) {
		if (res->res) {
			printf ("\n%.*s\n\n", (int) res->res_len, (char *) res->res);
		}
	}

}

#pragma endregion

#pragma region render

static u8 http_response_render_send (
	CerverReceive *cr,
	const char *header, const size_t header_len,
	const char *data, const size_t data_len
) {

	u8 retval = 1;

	if (!http_response_send_actual (
		cr->connection->socket,
		header, header_len
	)) {
		if (!http_response_send_actual (
			cr->connection->socket,
			data, data_len
		)) {
			size_t total_size = header_len + data_len;
			if (cr->cerver) cr->cerver->stats->total_bytes_sent += total_size;
			cr->connection->stats->total_bytes_sent += total_size;

			retval = 0;
		}
	}

	return retval;

}

// sends the selected text back to the user
// this methods takes care of generating a repsonse with text/html content type
// returns 0 on success, 1 on error
u8 http_response_render_text (CerverReceive *cr, const char *text, const size_t text_len) {

	u8 retval = 1;

	if (cr && text) {
		char *header = c_string_create (
			"HTTP/1.1 200 %s\r\nServer: Cerver/%s\r\nContent-Type: text/html; charset=UTF-8\r\nContent-Length: %ld\r\n\r\n", 
			http_status_str ((enum http_status) 200),
			CERVER_VERSION,
			text_len
		);

		if (header) {
			printf ("\n%s\n", header);

			retval = http_response_render_send (
				cr,
				header, strlen (header),
				text, text_len
			);

			free (header);
		}
	}

	return retval;

}

// sends the selected json back to the user
// this methods takes care of generating a repsonse with application/json content type
// returns 0 on success, 1 on error
u8 http_response_render_json (CerverReceive *cr, const char *json, const size_t json_len) {

	u8 retval = 1;

	if (cr && json) {
		char *header = c_string_create (
			"HTTP/1.1 200 %s\r\nServer: Cerver/%s\r\nContent-Type: application/json\r\nContent-Length: %ld\r\n\r\n", 
			http_status_str ((enum http_status) 200),
			CERVER_VERSION,
			json_len
		);

		if (header) {
			printf ("\n%s\n", header);

			retval = http_response_render_send (
				cr,
				header, strlen (header),
				json, json_len
			);

			free (header);
		}
	}

	return retval;

}

// opens the selected file and sends it back to the user
// this method takes care of generating the header based on the file values
// returns 0 on success, 1 on error
u8 http_response_render_file (CerverReceive *cr, const char *filename) {

	u8 retval = 1;

	if (cr && filename) {
		// check if file exists
		struct stat filestatus = { 0 };
		int file = file_open_as_fd (filename, &filestatus, O_RDONLY);
		if (file > 0) {
			retval = http_response_send_file (cr, file, filename, &filestatus);

			close (file);
		}
	}

	return retval;

}

#pragma endregion

#pragma region json

static HttpResponse *http_response_json_internal (http_status status, const char *key, const char *value) {

	HttpResponse *res = http_response_new ();
	if (res) {
		res->status = status;

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
HttpResponse *http_response_json_msg (http_status status, const char *msg) {

	return msg ? http_response_json_internal (status, "msg", msg) : NULL;

}

// creates and sends a http json message response with the defined status code & message
// returns 0 on success, 1 on error
u8 http_response_json_msg_send (CerverReceive *cr, unsigned int status, const char *msg) {

	u8 retval = 1;

	HttpResponse *res = http_response_json_msg ((http_status) status, msg);
	if (res) {
		// http_response_print (res);
		retval = http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

	return retval;

}

// creates a http response with the defined status code ready to be sent 
// and a data (body) with a json message of type { error: "your error message" }
HttpResponse *http_response_json_error (http_status status, const char *error_msg) {

	return error_msg ? http_response_json_internal (status, "error", error_msg) : NULL;

}

// creates and sends a http json error response with the defined status code & message
// returns 0 on success, 1 on error
u8 http_response_json_error_send (CerverReceive *cr, unsigned int status, const char *error_msg) {

	u8 retval = 1;

	HttpResponse *res = http_response_json_error ((http_status) status, error_msg);
	if (res) {
		// http_response_print (res);
		retval = http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

	return retval;

}

#pragma endregion