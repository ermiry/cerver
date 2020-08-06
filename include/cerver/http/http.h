#ifndef _CERVER_HTTP_H_
#define _CERVER_HTTP_H_

#include "cerver/collections/dlist.h"

#include "cerver/config.h"
#include "cerver/handler.h"

#include "cerver/http/route.h"
#include "cerver/http/request.h"
#include "cerver/http/jwt/alg.h"

struct _Cerver;

typedef struct KeyValuePair { char *key, *value; } KeyValuePair;

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

CERVER_PRIVATE HttpCerver *http_cerver_new (void);

CERVER_PRIVATE void http_cerver_delete (void *http_cerver_ptr);

CERVER_PRIVATE HttpCerver *http_cerver_create (struct _Cerver *cerver);

CERVER_PRIVATE void http_cerver_init (HttpCerver *http_cerver);

#pragma endregion

#pragma region routes

// register a new http to the http cerver
CERVER_EXPORT void http_cerver_route_register (HttpCerver *http_cerver, HttpRoute *route);

// set a route to catch any requet that didn't match any registered route
CERVER_EXPORT void http_cerver_set_catch_all_route (HttpCerver *http_cerver, 
	void (*catch_all_route)(CerverReceive *cr, HttpRequest *request));

#pragma endregion

#pragma region auth

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256
CERVER_EXPORT void http_cerver_auth_set_jwt_algorithm (HttpCerver *http_cerver, jwt_alg_t jwt_alg);

// sets the filename from where the jwt private key will be loaded
CERVER_EXPORT void http_cerver_auth_set_jwt_priv_key_filename (HttpCerver *http_cerver, const char *filename);

// sets the filename from where the jwt public key will be loaded
CERVER_EXPORT void http_cerver_auth_set_jwt_pub_key_filename (HttpCerver *http_cerver, const char *filename);

#pragma endregion

#pragma region parser

// parses a url query into key value pairs for better manipulation
CERVER_PUBLIC DoubleList *http_parse_query_into_pairs (const char *first, const char *last);

// gets the matching value for the requested key from a list of pairs
CERVER_PUBLIC const char *http_query_pairs_get_value (DoubleList *pairs, const char *key);

#pragma endregion

#pragma region handler

CERVER_PRIVATE void http_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer);

#pragma endregion

#endif