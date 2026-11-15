/**
 * @file cerver/http/core/settings.h
 * @brief The settings of an HttpCerver: the headers that it adds, its compression, its trusted proxies, its origins,
 * its data and the common responses.
 */

#ifndef _CERVER_HTTP_CORE_SETTINGS_H_
#define _CERVER_HTTP_CORE_SETTINGS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_settings Settings
 * @ingroup http_core
 * @brief The settings of an HttpCerver.
 *
 * The setters ignore a NULL HttpCerver.
 * @{
 */

/**
 * @brief Sets whether every response has the Date header.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] date_header true to add it, the default is #HTTP_CERVER_DEFAULT_DATE_HEADER
 */
CERVER_EXPORT void http_cerver_set_date_header (
	HttpCerver *http_cerver, const bool date_header
);

/**
 * @brief Sets whether every request is identified with its X-Request-Id or with a new UUID v4.
 *
 * The X-Request-Id of a request is used when it has up to HTTP_REQUEST_ID_LENGTH characters of [A-Za-z0-9._-]; the
 * id is sent back in the X-Request-Id of the response and it is logged with the request.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] request_id true to identify them, the default is #HTTP_CERVER_DEFAULT_REQUEST_ID
 */
CERVER_EXPORT void http_cerver_set_request_id (
	HttpCerver *http_cerver, const bool request_id
);

/**
 * @brief Sets the smallest body that is compressed.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] min_size the bytes, the default is HTTP_COMPRESSION_DEFAULT_MIN_SIZE
 */
CERVER_EXPORT void http_cerver_set_compression_min_size (
	HttpCerver *http_cerver, const size_t min_size
);

/**
 * @brief Sets the level of zlib of the compression.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] level from HTTP_COMPRESSION_MIN_LEVEL to HTTP_COMPRESSION_MAX_LEVEL, another value logs a warning and
 * keeps the current one; the default is HTTP_COMPRESSION_DEFAULT_LEVEL
 */
CERVER_EXPORT void http_cerver_set_compression_level (
	HttpCerver *http_cerver, const int level
);

/**
 * @brief Sets from which peers the X-Forwarded-For and X-Real-IP headers are used to find the address of the
 * client (http_request_get_client_ip ()).
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] trusted_proxies the peers, the default is HTTP_CERVER_DEFAULT_TRUSTED_PROXIES
 */
CERVER_EXPORT void http_cerver_set_trusted_proxies (
	HttpCerver *http_cerver, const HttpTrustedProxies trusted_proxies
);

/**
 * @brief Adds a network in CIDR notation ("172.16.0.0/12", "fc00::/7" or a single address) to the trusted proxies of
 * HTTP_TRUSTED_PROXIES_LIST.
 *
 * An invalid network, or more than HTTP_TRUSTED_PROXIES_SIZE, is a registration error of the routes, so the cerver
 * does not start.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] cidr the network
 * @retval 0 the network was added
 * @retval 1 a NULL HttpCerver, an invalid network or the list is full
 */
CERVER_EXPORT u8 http_cerver_add_trusted_proxy (
	HttpCerver *http_cerver, const char *cidr
);

/**
 * @brief Adds a domain to the origins whitelist.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] domain the origin, copied
 * @retval 0 the origin was added
 * @retval 1 a NULL argument or the whitelist already has HTTP_ORIGINS_SIZE origins
 */
CERVER_EXPORT u8 http_cerver_add_origin_to_whitelist (
	HttpCerver *http_cerver, const char *domain
);

/**
 * @brief Prints the origins whitelist to stdout.
 *
 * @param[in] http_cerver the HttpCerver, NULL prints nothing
 */
CERVER_EXPORT void http_cerver_print_origins_whitelist (
	const HttpCerver *http_cerver
);

/**
 * @brief Returns the custom data of the HttpCerver, that the application can set and read at any time.
 *
 * @param[in] http_cerver the HttpCerver
 * @return the data, NULL for a NULL HttpCerver
 */
CERVER_EXPORT const void *http_cerver_get_custom_data (
	const HttpCerver *http_cerver
);

/**
 * @brief Sets the custom data of the HttpCerver.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] custom_data the data
 */
CERVER_EXPORT void http_cerver_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
);

/**
 * @brief Sets how the custom data is released when the HttpCerver is deleted.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] delete_custom_data releases the data, NULL to keep it
 */
CERVER_EXPORT void http_cerver_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
);

/**
 * @brief Releases the custom data with free () when the HttpCerver is deleted.
 *
 * @param[in,out] http_cerver the HttpCerver
 */
CERVER_EXPORT void http_cerver_set_default_delete_custom_data (
	HttpCerver *http_cerver
);

/** @brief The common 200 response, `{"oki": "doki"}`; created by the first HTTP cerver that starts. */
CERVER_PUBLIC struct _HttpResponse *oki_doki;
/** @brief The common 400 response, `{"error": "Bad request!"}`. */
CERVER_PUBLIC struct _HttpResponse *bad_request_error;
/** @brief The common 400 response of a bad user, `{"error": "Bad user!"}`. */
CERVER_PUBLIC struct _HttpResponse *bad_user_error;
/** @brief The common 401 response, `{"error": "Failed to authenticate!"}`. */
CERVER_PUBLIC struct _HttpResponse *bad_auth_error;
/** @brief The common 404 response, `{"error": "Not found!"}`. */
CERVER_PUBLIC struct _HttpResponse *not_found_error;
/** @brief The common 500 response, `{"error": "Internal error!"}`. */
CERVER_PUBLIC struct _HttpResponse *server_error;

/**
 * @brief Adds a header to every response of the HttpCerver; a response with the same header keeps its own, and a
 * second call with the same type replaces its value.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] type the header
 * @param[in] actual_header the value, copied
 * @retval 0 the header was added
 * @retval 1 a NULL argument, a type out of the map, or a line that does not fit in HTTP_HEADER_VALUE_SIZE
 */
CERVER_PUBLIC u8 http_cerver_add_responses_header (
	HttpCerver *http_cerver,
	const http_header type, const char *actual_header
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
