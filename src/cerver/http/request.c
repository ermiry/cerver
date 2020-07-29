#include <stdlib.h>
#include <stdio.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/http/request.h"

HttpRequest *http_request_new (void) {

	HttpRequest *http_request = (HttpRequest *) malloc (sizeof (HttpRequest));
	if (http_request) {
		http_request->method = REQUEST_METHOD_GET;

		http_request->url = NULL;

		http_request->params = NULL;

		http_request->next_header = REQUEST_HEADER_INVALID;

		for (u8 i = 0; i < REQUEST_HEADERS_SIZE; i++)
			http_request->headers[i] = NULL;
		
		http_request->body = NULL;
	}

	return http_request;
	
}

void http_request_delete (HttpRequest *http_request) {

	if (http_request) {
		estring_delete (http_request->url);

		for (u8 i = 0; i < REQUEST_HEADERS_SIZE; i++)
			estring_delete (http_request->headers[i]);

		estring_delete (http_request->body);

		free (http_request);
	}

}

HttpRequest *http_request_create (void) {

	HttpRequest *request = http_request_new ();
	if (request) {
		request->params = dlist_init (estring_delete, estring_comparator);
	}

	return request;

}

void http_request_headers_print (HttpRequest *http_request) {

	if (http_request) {
		char *null = "NULL";
		estring *header = NULL;
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