#ifndef _CERVER_HTTP_CORE_ADMIN_H_
#define _CERVER_HTTP_CORE_ADMIN_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region admin

// enables the ability to have admin routes
// to fetch cerver's HTTP stats
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN
CERVER_EXPORT void http_cerver_enable_admin_routes (
	HttpCerver *http_cerver, const bool enable
);

// enables the ability to have an admin info route
// to fetch cerver's information
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO
CERVER_EXPORT void http_cerver_enable_admin_info_route (
	HttpCerver *http_cerver, const bool enable
);

// enables HTTP admin routes to handle HEAD requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS
CERVER_EXPORT void http_cerver_enable_admin_head_handlers (
	HttpCerver *http_cerver, const bool enable
);

// enables HTTP admin routes to handle OPTIONS requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS
CERVER_EXPORT void http_cerver_enable_admin_options_handlers (
	HttpCerver *http_cerver, const bool enable
);

// enables authentication in admin routes
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH
CERVER_EXPORT void http_cerver_enable_admin_routes_authentication (
	HttpCerver *http_cerver, const HttpRouteAuthType auth_type
);

// sets the method to be used to decode incoming data from JWT
// and sets a method to delete it after use
// if no delete method is set, data won't be freed
CERVER_EXPORT void http_cerver_admin_routes_auth_set_decode_data (
	HttpCerver *http_cerver,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
);

// works like http_cerver_enable_admin_routes_authentication ()
// but sets a method to decode data from a JWT into a json string
CERVER_EXPORT void http_cerver_admin_routes_auth_decode_to_json (
	HttpCerver *http_cerver
);

// sets a method to be used to handle auth in admin routes
// HTTP cerver must had been configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// method must return 0 on success and 1 on error
CERVER_EXPORT void http_cerver_admin_routes_set_authentication_handler (
	HttpCerver *http_cerver,
	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
);

// sets a custom data reference to be set in all admin routes
CERVER_EXPORT void http_cerver_admin_routes_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
);

// sets how the custom data will be disposed when the service gets destroyed
CERVER_EXPORT void http_cerver_admin_routes_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
);

// enables CORS headers in admin routes responses
// always uses admin origin's value
// if there is no dedicated origin, it will dynamically
// set the header based on the origins whitelist
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS
CERVER_EXPORT void http_cerver_enable_admin_cors_headers (
	HttpCerver *http_cerver, const bool enable
);

// sets the dedicated domain that will be walways set
// in the admin responses CORS headers
CERVER_EXPORT void http_cerver_admin_set_origin (
	HttpCerver *http_cerver, const char *domain
);

// registers a new file system to be handled
// when requesting for fs stats
CERVER_EXPORT void http_cerver_register_admin_file_system (
	HttpCerver *http_cerver, const char *path
);

// registers an existing worker to be handled
// when requisting for workers states
CERVER_EXPORT void http_cerver_register_admin_worker (
	HttpCerver *http_cerver, const struct _Worker *worker
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
