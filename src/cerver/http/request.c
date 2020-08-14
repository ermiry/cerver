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

		dlist_delete (http_request->body_values);

		free (http_request);
	}

}

HttpRequest *http_request_create (void) {

	return http_request_new ();

}

void http_request_headers_print (HttpRequest *http_request) {

	if (http_request) {
		char *null = "NULL";
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

				case REQUEST_HEADER_PROXY_AUTHORIZATION				: printf ("Proxy-Authorization: %s\n", header ? header->str : null); break;

				case REQUEST_HEADER_USER_AGENT						: printf ("User-Agent: %s\n", header ? header->str : null); break;
			}
		}
	}

}

MultiPart *http_multi_part_new (void) {

	MultiPart *multi_part = (MultiPart *) malloc (sizeof (MultiPart));
	if (multi_part) {
		multi_part->next_header = 0;
		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++)
			multi_part->headers[i] = NULL;

		multi_part->params = dlist_init (key_value_pair_delete, NULL);

		multi_part->name = NULL;
		multi_part->filename = NULL;

		multi_part->fd = -1;
	}

	return multi_part;

}

void http_multi_part_delete (void *multi_part_ptr) {

	if (multi_part_ptr) {
		MultiPart *multi_part = (MultiPart *) multi_part_ptr;

		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++)
			str_delete (multi_part->headers[i]);

		dlist_delete (multi_part->params);

		free (multi_part_ptr);
	}

}

void http_multi_part_headers_print (MultiPart *mpart) {

	if (mpart) {
		char *null = "NULL";
		String *header = NULL;
		for (u8 i = 0; i < MULTI_PART_HEADERS_SIZE; i++) {
			header = mpart->headers[i];

			switch (i) {
				case MULTI_PART_HEADER_CONTENT_DISPOSITION		: printf ("Content-Disposition: %s\n", header ? header->str : null); break;
				case MULTI_PART_HEADER_CONTENT_LENGTH			: printf ("Content-Length: %s\n", header ? header->str : null); break;
				case MULTI_PART_HEADER_CONTENT_TYPE				: printf ("Content-Type: %s\n", header ? header->str : null); break;
			}
		}
	}

}