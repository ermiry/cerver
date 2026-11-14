#ifndef _CERVER_HTTP_CORE_PUBLIC_H_
#define _CERVER_HTTP_CORE_PUBLIC_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region public

typedef struct HttpStaticPath {

	String *path;

	HttpRouteAuthType auth_type;

	// the data of a bearer JWT, for the request of a file
	void *(*decode_data)(void *);
	void (*delete_decoded_data)(void *);

	// the method of HTTP_ROUTE_AUTH_TYPE_CUSTOM
	HttpAuthenticationHandler authentication_handler;

} HttpStaticPath;

// sets the authentication of the files of a whole static path,
// a request is authenticated with the static path that has its file,
// or with the first one that has authentication when none has it,
// so a file that does not exist is answered like a protected one
// a request that fails it is answered with 401, before its file is looked for
CERVER_EXPORT void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
);

// sets the method that decodes the data of the bearer JWT of a static path
// (HTTP_ROUTE_AUTH_TYPE_BEARER), and the one that deletes it after the request
CERVER_EXPORT void http_static_path_set_decode_data (
	HttpStaticPath *static_path,
	void *(*decode_data)(void *),
	void (*delete_decoded_data)(void *)
);

// sets the method that authenticates the requests of a static path
// with HTTP_ROUTE_AUTH_TYPE_CUSTOM, it returns 0 on success and 1 on error
CERVER_EXPORT void http_static_path_set_authentication_handler (
	HttpStaticPath *static_path,
	HttpAuthenticationHandler authentication_handler
);

// adds a new static path where static files can be served upon request
// it is recomened to set absoulute paths
CERVER_EXPORT HttpStaticPath *http_cerver_static_path_add (
	HttpCerver *http_cerver, const char *static_path
);

// removes a path from the served public paths
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_receive_public_path_remove (
	HttpCerver *http_cerver, const char *static_path
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
