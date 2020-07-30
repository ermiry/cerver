#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/route.h"
#include "cerver/http/request.h"

struct _Cerver;

struct _HttpCerver {

    struct _Cerver *cerver;

    // list of top level routes
    DoubleList *routes;

    // catch all route (/*)
    void (*default_handler)(CerverReceive *cr, HttpRequest *request);

};

typedef struct _HttpCerver HttpCerver;

#pragma region main

extern HttpCerver *http_cerver_new (void);

extern void http_cerver_delete (void *http_cerver_ptr);

extern HttpCerver *http_cerver_create (struct _Cerver *cerver);

extern void http_cerver_init (HttpCerver *http_cerver);

#pragma endregion

#pragma region routes

extern void http_cerver_route_register (HttpCerver *http_cerver, HttpRoute *route);

extern void http_cerver_set_catch_all_route (HttpCerver *http_cerver, 
	void (*catch_all_route)(CerverReceive *cr, HttpRequest *request));

#pragma endregion

#pragma region handler

extern void http_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer);

#pragma endregion

#endif