#ifndef _CERVER_HTTP_CORE_SETTINGS_H_
#define _CERVER_HTTP_CORE_SETTINGS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region headers

// adds the Date header to every response
// the default value is HTTP_CERVER_DEFAULT_DATE_HEADER
CERVER_EXPORT void http_cerver_set_date_header (
	HttpCerver *http_cerver, const bool date_header
);

// identifies every request with the X-Request-Id that it has
// (up to HTTP_REQUEST_ID_LENGTH characters of [A-Za-z0-9._-]) or with a new UUID v4,
// the id is sent back in the X-Request-Id of the response and it is logged with the request
// the default value is HTTP_CERVER_DEFAULT_REQUEST_ID
CERVER_EXPORT void http_cerver_set_request_id (
	HttpCerver *http_cerver, const bool request_id
);

// the smallest body that is compressed
// the default value is HTTP_COMPRESSION_DEFAULT_MIN_SIZE
CERVER_EXPORT void http_cerver_set_compression_min_size (
	HttpCerver *http_cerver, const size_t min_size
);

// the zlib level, from HTTP_COMPRESSION_MIN_LEVEL to HTTP_COMPRESSION_MAX_LEVEL
// the default value is HTTP_COMPRESSION_DEFAULT_LEVEL
CERVER_EXPORT void http_cerver_set_compression_level (
	HttpCerver *http_cerver, const int level
);

#pragma endregion

#pragma region proxies

// sets from which peers the X-Forwarded-For and X-Real-IP headers are used
// to find the client's address (http_request_get_client_ip ())
// the default value is HTTP_CERVER_DEFAULT_TRUSTED_PROXIES
CERVER_EXPORT void http_cerver_set_trusted_proxies (
	HttpCerver *http_cerver, const HttpTrustedProxies trusted_proxies
);

// adds a network in CIDR notation ("172.16.0.0/12", "fc00::/7" or a single address)
// to the trusted proxies of HTTP_TRUSTED_PROXIES_LIST
// an invalid network or more than HTTP_TRUSTED_PROXIES_SIZE stop the cerver when it starts
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_add_trusted_proxy (
	HttpCerver *http_cerver, const char *cidr
);

#pragma endregion

#pragma region origins

// adds a new domain to the HTTP cerver's origins whitelist
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_cerver_add_origin_to_whitelist (
	HttpCerver *http_cerver, const char *domain
);

CERVER_EXPORT void http_cerver_print_origins_whitelist (
	const HttpCerver *http_cerver
);

#pragma endregion

#pragma region data

// gets the HTTP cerver's custom data
// this data can be safely set by the user and accessed at any time
CERVER_EXPORT const void *http_cerver_get_custom_data (
	const HttpCerver *http_cerver
);

// sets the HTTP cerver's custom data
CERVER_EXPORT void http_cerver_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
);

// sets a custom method to delete the HTTP cerver's custom data
CERVER_EXPORT void http_cerver_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
);

// sets free () as the method to delete the HTTP cerver's custom data
CERVER_EXPORT void http_cerver_set_default_delete_custom_data (
	HttpCerver *http_cerver
);

#pragma endregion

#pragma region responses

CERVER_PUBLIC struct _HttpResponse *oki_doki;
CERVER_PUBLIC struct _HttpResponse *bad_request_error;
CERVER_PUBLIC struct _HttpResponse *bad_user_error;
CERVER_PUBLIC struct _HttpResponse *bad_auth_error;
CERVER_PUBLIC struct _HttpResponse *not_found_error;
CERVER_PUBLIC struct _HttpResponse *server_error;

// adds a new global responses header
// this header will be added to all the responses
// if the response has the same header type,
// it will be used instead of the global header
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 http_cerver_add_responses_header (
	HttpCerver *http_cerver,
	const http_header type, const char *actual_header
);

// works like http_cerver_add_responses_header () with the name of any header,
// it replaces the ones with the same name
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 http_cerver_add_responses_header_with_name (
	HttpCerver *http_cerver, const char *name, const char *value
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
