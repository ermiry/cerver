#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/http/route.h"
#include "cerver/http/request.h"
#include "cerver/http/jwt/alg.h"

struct _Cerver;

struct _HttpCerver {

    struct _Cerver *cerver;

    // list of top level routes
    DoubleList *routes;

    // catch all route (/*)
    void (*default_handler)(CerverReceive *cr, HttpRequest *request);

    // auth
    jwt_alg_t jwt_alg;

    estring *jwt_opt_key_name;          // jwt private key filename
    estring *jwt_private_key;           // jwt actual private key

    estring *jwt_opt_pub_key_name;      // jwt public key filename
    estring *jwt_public_key;            // jwt actual public key

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

#pragma region auth

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256
extern void http_cerver_auth_set_jwt_algorithm (HttpCerver *http_cerver, jwt_alg_t jwt_alg);

// sets the filename from where the jwt private key will be loaded
extern void http_cerver_auth_set_jwt_priv_key_filename (HttpCerver *http_cerver, const char *filename);

// sets the filename from where the jwt public key will be loaded
extern void http_cerver_auth_set_jwt_pub_key_filename (HttpCerver *http_cerver, const char *filename);

#pragma endregion

#pragma region handler

extern void http_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer);

#pragma endregion

#endif