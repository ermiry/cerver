#include <stdlib.h>
#include <stdio.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/config.h"

#include "cerver/http/http.h"
#include "cerver/http/request.h"

static const char *request_method_strings[] = {

	#define XX(num, name, string) #string,
	REQUEST_METHOD_MAP(XX)
	#undef XX

};

const char *http_request_method_str (RequestMethod m) {

  return ELEM_AT (request_method_strings, m, "Unknown");

}

HttpRequest *http_request_new (void) {

	HttpRequest *http_request = (HttpRequest *) malloc (sizeof (HttpRequest));
	if (http_request) {
		http_request->method = REQUEST_METHOD_GET;

		http_request->url = NULL;
		http_request->query = NULL;

		http_request->query_params = NULL;

		http_request->n_params = 0;

		for (u8 i = 0; i < REQUEST_PARAMS_SIZE; i++)
			http_request->params[i] = NULL;

		http_request->next_header = REQUEST_HEADER_INVALID;

		for (u8 i = 0; i < REQUEST_HEADERS_SIZE; i++)
			http_request->headers[i] = NULL;

		http_request->decoded_data = NULL;
		http_request->delete_decoded_data = NULL;
		
		http_request->body = NULL;

		http_request->current_part = NULL;
		http_request->multi_parts = NULL;
		http_request->n_files = 0;
		http_request->n_values = 0;
		http_request->dirname = NULL;
		
		http_request->body_values = NULL;
	}

	return http_request;
	
}

void http_request_delete (HttpRequest *http_request) {

	if (http_request) {
		str_delete (http_request->url);
		str_delete (http_request->query);

		dlist_delete (http_request->query_params);

		for (u8 i = 0; i < REQUEST_PARAMS_SIZE; i++)
			str_delete (http_request->params[i]);

		for (u8 i = 0; i < REQUEST_HEADERS_SIZE; i++)
			str_delete (http_request->headers[i]);

		if (http_request->decoded_data) {
			if (http_request->delete_decoded_data) 
				http_request->delete_decoded_data (http_request->decoded_data);
		}

		str_delete (http_request->body);

		dlist_delete (http_request->multi_parts);
		str_delete (http_request->dirname);

		dlist_delete (http_request->body_values);

		free (http_request);
	}

}

HttpRequest *http_request_create (void) {

	return http_request_new ();

}

void http_request_headers_print (const HttpRequest *http_request) {

	if (http_request) {
		const char *null = "NULL";
		String *header = NULL;
		for (u8 i = 0; i < REQUEST_HEADERS_SIZE; i++) {
			header = http_request->headers[i];

			switch (i) {
				case REQUEST_HEADER_ACCEPT							: cerver_log_msg ("Accept: %s", header ? header->str : null); break;
				case REQUEST_HEADER_ACCEPT_CHARSET					: cerver_log_msg ("Accept-Charset: %s", header ? header->str : null); break;
				case REQUEST_HEADER_ACCEPT_ENCODING					: cerver_log_msg ("Accept-Encoding: %s", header ? header->str : null); break;
				case REQUEST_HEADER_ACCEPT_LANGUAGE					: cerver_log_msg ("Accept-Language: %s", header ? header->str : null); break;

				case REQUEST_HEADER_ACCESS_CONTROL_REQUEST_HEADERS	: cerver_log_msg ("Access-Control-Request-Headers: %s", header ? header->str : null); break;

				case REQUEST_HEADER_AUTHORIZATION					: cerver_log_msg ("Authorization: %s", header ? header->str : null); break;

				case REQUEST_HEADER_CACHE_CONTROL					: cerver_log_msg ("Cache-Control: %s", header ? header->str : null); break;

				case REQUEST_HEADER_CONNECTION						: cerver_log_msg ("Connection: %s", header ? header->str : null); break;

				case REQUEST_HEADER_CONTENT_LENGTH					: cerver_log_msg ("Content-Length: %s", header ? header->str : null); break;
				case REQUEST_HEADER_CONTENT_TYPE					: cerver_log_msg ("Content-Type: %s", header ? header->str : null); break;

				case REQUEST_HEADER_COOKIE							: cerver_log_msg ("Cookie: %s", header ? header->str : null); break;

				case REQUEST_HEADER_DATE							: cerver_log_msg ("Date: %s", header ? header->str : null); break;

				case REQUEST_HEADER_EXPECT							: cerver_log_msg ("Expect: %s", header ? header->str : null); break;

				case REQUEST_HEADER_HOST							: cerver_log_msg ("Host: %s", header ? header->str : null); break;

				case REQUEST_HEADER_ORIGIN							: cerver_log_msg ("Origin: %s", header ? header->str : null); break;

				case REQUEST_HEADER_PROXY_AUTHORIZATION				: cerver_log_msg ("Proxy-Authorization: %s", header ? header->str : null); break;

				case REQUEST_HEADER_UPGRADE							: cerver_log_msg ("Upgrade: %s", header ? header->str : null); break;

				case REQUEST_HEADER_USER_AGENT						: cerver_log_msg ("User-Agent: %s", header ? header->str : null); break;

				case REQUEST_HEADER_WEB_SOCKET_KEY					: cerver_log_msg ("Sec-WebSocket-Key: %s", header ? header->str : null); break;
				case REQUEST_HEADER_WEB_SOCKET_VERSION				: cerver_log_msg ("Sec-WebSocket-Version: %s", header ? header->str : null); break;

				default: break;
			}
		}
	}

}

// returns the matching query param value for the specified key
// returns NULL if no match or no query params
const String *http_request_query_params_get_value (
	const HttpRequest *http_request, const char *key
) {

	return http_request ?
		key_value_pairs_get_value (http_request->query_params, key) : NULL;

}

static MultiPart *http_request_multi_parts_get_internal (
	const HttpRequest *http_request, const char *key
) {

	MultiPart *mpart = NULL;
	for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
		mpart = (MultiPart *) le->data;

		if (mpart->name) {
			if (!strcmp (mpart->name->str, key)) {
				return mpart;
			}
		}
	}

	return NULL;

}

// searches the request's multi parts values for one with matching key & type
// returns a constant Stirng that should not be deleted if found, NULL if not match
const MultiPart *http_request_multi_parts_get (
	const HttpRequest *http_request, const char *key
) {

	return (http_request && key) ?
		http_request_multi_parts_get_internal (http_request, key) : NULL;

}

// searches the request's multi parts values for a value with matching key
// returns a constant String that should not be deleted if found, NULL if not match
const String *http_request_multi_parts_get_value (
	const HttpRequest *http_request, const char *key
) {

	if (http_request && key) {
		MultiPart *mpart = http_request_multi_parts_get_internal (http_request, key);
		if (mpart) return mpart->value;
	}

	return NULL;

}

// searches the request's multi parts values for a filename with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
const char *http_request_multi_parts_get_filename (
	const HttpRequest *http_request, const char *key
) {

	if (http_request && key) {
		MultiPart *mpart = http_request_multi_parts_get_internal (http_request, key);
		if (mpart) return mpart->filename;
	}

	return NULL;

}

// searches the request's multi parts values for a saved filename with matching key
// returns a constant c string that should not be deleted if found, NULL if not match
const char *http_request_multi_parts_get_saved_filename (
	const HttpRequest *http_request, const char *key
) {

	if (http_request && key) {
		MultiPart *mpart = http_request_multi_parts_get_internal (http_request, key);
		if (mpart) return mpart->saved_filename;
	}

	return NULL;

}

// returns a dlist with constant c strings values (that should not be deleted) with all the filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
DoubleList *http_request_multi_parts_get_all_filenames (
	const HttpRequest *http_request
) {

	DoubleList *all = NULL;

	if (http_request) {
		all = dlist_init (NULL, NULL);

		MultiPart *mpart = NULL;
		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			mpart = (MultiPart *) le->data;

			if (mpart->filename) {
				dlist_insert_after (all, dlist_end (all), mpart->filename);
			}
		}
	}

	return all;

}

// returns a dlist with constant c strings values (that should not be deleted) with all the saved filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
DoubleList *http_request_multi_parts_get_all_saved_filenames (
	const HttpRequest *http_request
) {

	DoubleList *all = NULL;

	if (http_request) {
		all = dlist_init (NULL, NULL);

		MultiPart *mpart = NULL;
		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			mpart = (MultiPart *) le->data;

			if (mpart->saved_filename) {
				dlist_insert_after (all, dlist_end (all), mpart->saved_filename);
			}
		}
	}

	return all;

}

// used to safely delete a dlist generated by 
// http_request_multi_parts_get_all_filenames () or http_request_multi_parts_get_all_saved_filenames ()
void http_request_multi_parts_all_filenames_delete (
	DoubleList *all_filenames
) {

	if (all_filenames) {
		dlist_clear (all_filenames);
		dlist_delete (all_filenames);
	}

}

// discards all the saved files from the multipart request
void http_request_multi_part_discard_files (
	const HttpRequest *http_request
) {

	if (http_request) {
		MultiPart *mpart = NULL;
		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			mpart = (MultiPart *) le->data;

			if (mpart->saved_filename) {
				(void) remove (mpart->saved_filename);
			}
		}
	}

}

void http_request_multi_parts_print (
	const HttpRequest *http_request
) {

	if (http_request) {
		(void) printf ("\nHTTP request multi part values: \n");
		(void) printf ("n files: %d\n", http_request->n_files);
		(void) printf ("n values: %d\n", http_request->n_values);

		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			http_multi_part_print ((MultiPart *) le->data);
		}

		(void) printf ("\n");
	}

}

// search request's body values for matching value by key
// returns a constant String that should not be deleted if match, NULL if not found
const String *http_request_body_get_value (
	const HttpRequest *http_request, const char *key
) {

	return key_value_pairs_get_value (http_request->body_values, key);

}