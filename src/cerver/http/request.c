#include <stdlib.h>

#include "cerver/types/types.h"
#include "cerver/types/estring.h"

#include "cerver/http/request.h"

HttpRequest *http_request_new (void) {

	HttpRequest *http_request = (HttpRequest *) malloc (sizeof (HttpRequest));
	if (http_request) {
		http_request->url = NULL;

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