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

void http_request_headers_print (HttpRequest *http_request) {

	if (http_request) {
		const char *null = "NULL";
		String *header = NULL;
		for (u8 i = 0; i < REQUEST_HEADERS_SIZE; i++) {
			header = http_request->headers[i];

			switch (i) {
				case REQUEST_HEADER_ACCEPT							: printf ("Accept: %s\n", header ? header->str : null); break;
				case REQUEST_HEADER_ACCEPT_CHARSET					: printf ("Accept-Charset: %s\n", header ? header->str : null); break;
				case REQUEST_HEADER_ACCEPT_ENCODING					: printf ("Accept-Encoding: %s\n", header ? header->str : null); break;
				case REQUEST_HEADER_ACCEPT_LANGUAGE					: printf ("Accept-Language: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_ACCESS_CONTROL_REQUEST_HEADERS	: printf ("Access-Control-Request-Headers: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_AUTHORIZATION					: printf ("Authorization: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_CACHE_CONTROL					: printf ("Cache-Control: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_CONNECTION						: printf ("Connection: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_CONTENT_LENGTH					: printf ("Content-Length: %s\n", header ? header->str : null); break;
				case REQUEST_HEADER_CONTENT_TYPE					: printf ("Content-Type: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_COOKIE							: printf ("Cookie: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_DATE							: printf ("Date: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_EXPECT							: printf ("Expect: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_HOST							: printf ("Host: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_ORIGIN							: printf ("Origin: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_PROXY_AUTHORIZATION				: printf ("Proxy-Authorization: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_UPGRADE							: printf ("Upgrade: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_USER_AGENT						: printf ("User-Agent: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_WEB_SOCKET_KEY					: printf ("Sec-WebSocket-Key: %s\n", header ? header->str : null); break;
				case REQUEST_HEADER_WEB_SOCKET_VERSION				: printf ("Sec-WebSocket-Version: %s\n", header ? header->str : null); break;
			}
		}
	}

}

// searches the request's multi parts values for a file with matching key
// returns a constant Stirng that should not be deleted if found, NULL if not match
const String *http_request_multi_parts_get_file (HttpRequest *http_request, const char *key) {

	if (http_request && key) {
		MultiPart *mpart = NULL;
		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			mpart = (MultiPart *) le->data;

			if (mpart->filename) {
				if (!strcmp (mpart->filename->str, key)) {
					return mpart->filename;
				}
			}
		}
	}

	return NULL;

}

// searches the request's multi parts values for a value with matching key
// returns a constant Stirng that should not be deleted if found, NULL if not match
const String *http_request_multi_parts_get_value (HttpRequest *http_request, const char *key) {

	if (http_request && key) {
		MultiPart *mpart = NULL;
		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			mpart = (MultiPart *) le->data;

			if (mpart->value) {
				if (!strcmp (mpart->name->str, key)) {
					return mpart->value;
				}
			}
		}
	}

	return NULL;

}

// returns a dlist with constant strings values (that should not be deleted) with all the filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
DoubleList *http_request_multi_parts_get_all_filenames (HttpRequest *http_request) {

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

// returns a dlist with constant strings values (that should not be deleted) with all the saved filenames from the request
// the dlist must be deleted using http_request_multi_parts_all_filenames_delete ()
DoubleList *http_request_multi_parts_get_all_saved_filenames (HttpRequest *http_request) {

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
void http_request_multi_parts_all_filenames_delete (DoubleList *all_filenames) {

	if (all_filenames) {
		dlist_clear (all_filenames);
		dlist_delete (all_filenames);
	}

}

void http_request_multi_parts_print (HttpRequest *http_request) {

	if (http_request) {
		printf ("\nHTTP request multi part values: \n");
		printf ("n files: %d\n", http_request->n_files);
		printf ("n values: %d\n", http_request->n_values);

		for (ListElement *le = dlist_start (http_request->multi_parts); le; le = le->next) {
			http_multi_part_print ((MultiPart *) le->data);
		}

		printf ("\n");
	}

}

// search request's body values for matching value by key
// returns a constant String that should not be deleted if match, NULL if not found
const String *http_request_body_get_value (HttpRequest *http_request, const char *key) {

	return key_value_pairs_get_value (http_request->body_values, key);

}