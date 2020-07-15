#ifndef _CERVER_HTTP_REQUEST_H_
#define _CERVER_HTTP_REQUEST_H_

#include "cerver/types/estring.h"

#include "cerver/collections/htab.h"

typedef struct HttpRequest {

	estring *url;
	Htab *headers;
	estring *body;  

} HttpRequest;

#endif