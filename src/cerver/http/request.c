#include <stdlib.h>

#include "cerver/types/estring.h"

#include "cerver/collections/htab.h"

#include "cerver/http/request.h"

HttpRequest *http_request_new (void) {

	HttpRequest *http_request = (HttpRequest *) malloc (sizeof (HttpRequest));
	if (http_request) {
		http_request->url = NULL;
		http_request->headers = NULL;
		http_request->body = NULL;
	}

	return http_request;
	
}

void http_request_delete (HttpRequest *http_request) {

	if (http_request) {
		estring_delete (http_request->url);
		htab_destroy (http_request->headers);
		estring_delete (http_request->body);

		free (http_request);
	}

}