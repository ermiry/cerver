/**
 * @file cerver/http/core/admin.h
 * @brief The admin routes of a CerverHttp: its info, its stats, its file systems, its workers and its metrics.
 */

#ifndef _CERVER_HTTP_CORE_ADMIN_H_
#define _CERVER_HTTP_CORE_ADMIN_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_admin Admin
 * @ingroup http_core
 * @brief The routes that report the state of a CerverHttp.
 *
 * They are the endpoints of a top level group of their own (`cerver/info`, `cerver/stats`,
 * `cerver/stats/filesystems`, `cerver/stats/workers` and `cerver/metrics` under its path): they take no setting of
 * the groups of the application, only the ones of the cerver and their own authentication.
 *
 * The setters take effect before the cerver starts, when the admin routes are added to the routes, and they ignore
 * a NULL CerverHttp.
 * @{
 */

/**
 * @brief Enables the admin routes, that report the HTTP stats of the cerver.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] enable true to enable them, the initial value is #CERVER_HTTP_DEFAULT_ENABLE_ADMIN
 */
CERVER_EXPORT void cerver_http_enable_admin_routes (
	CerverHttp *http_cerver, const bool enable
);

/**
 * @brief Sets the path of the group of the admin routes: with "/api" they are `/api/cerver/info`,
 * `/api/cerver/stats`…
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] path the path of the group, like the one of cerver_http_group_create (), copied; NULL for
 * #CERVER_HTTP_DEFAULT_ADMIN_PATH
 * @note A route of the application with the path and the method of an admin route is a registration error: the
 * cerver does not start.
 * @since 3.0b-12 the admin routes were the endpoints of the first group of the application, with its path and its
 * settings
 */
CERVER_EXPORT void cerver_http_admin_set_path (
	CerverHttp *http_cerver, const char *path
);

/**
 * @brief Enables the admin info route, that reports the info of the cerver.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] enable true to enable it, the initial value is #CERVER_HTTP_DEFAULT_ENABLE_ADMIN_INFO
 */
CERVER_EXPORT void cerver_http_enable_admin_info_route (
	CerverHttp *http_cerver, const bool enable
);

/**
 * @brief Enables the HEAD handlers of the admin routes.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] enable true to enable them, the initial value is #CERVER_HTTP_DEFAULT_ENABLE_ADMIN_HEADS
 */
CERVER_EXPORT void cerver_http_enable_admin_head_handlers (
	CerverHttp *http_cerver, const bool enable
);

/**
 * @brief Enables the OPTIONS handlers of the admin routes.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] enable true to enable them, the initial value is #CERVER_HTTP_DEFAULT_ENABLE_ADMIN_OPTIONS
 */
CERVER_EXPORT void cerver_http_enable_admin_options_handlers (
	CerverHttp *http_cerver, const bool enable
);

/**
 * @brief Enables the authentication of the admin routes.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] auth_type how the admin routes authenticate; by default they do not
 * (#CERVER_HTTP_DEFAULT_ENABLE_ADMIN_AUTH)
 */
CERVER_EXPORT void cerver_http_enable_admin_routes_authentication (
	CerverHttp *http_cerver, const CerverHttpRouteAuthType auth_type
);

/**
 * @brief Sets how the data of the token of an admin request is decoded, and how it is released after use.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] decode_data decodes the claims of the token
 * @param[in] delete_decoded_data releases the decoded data, NULL so it is not freed
 * @warning With CERVER_HANDLER_TYPE_EVENTS decode_data runs in an I/O thread, that reads no other connection
 * until it returns: it must not wait.
 * @since 3.0b-7 decode_data is a CerverHttpDecodeData: it takes the claims as a value of the JSON of cerver.
 */
CERVER_EXPORT void cerver_http_admin_routes_auth_set_decode_data (
	CerverHttp *http_cerver,
	CerverHttpDecodeData decode_data, void (*delete_decoded_data)(void *)
);

/**
 * @brief Decodes the data of the token of an admin request into a json string, released with free ().
 *
 * @param[in,out] http_cerver the CerverHttp
 */
CERVER_EXPORT void cerver_http_admin_routes_auth_decode_to_json (
	CerverHttp *http_cerver
);

/**
 * @brief Sets the method that authenticates the admin requests, for HTTP_ROUTE_AUTH_TYPE_CUSTOM.
 *
 * @param[in,out] http_cerver the CerverHttp, configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
 * @param[in] authentication_handler returns 0 when the request is authenticated and 1 on error
 */
CERVER_EXPORT void cerver_http_admin_routes_set_authentication_handler (
	CerverHttp *http_cerver,
	CerverHttpAuthenticationHandler authentication_handler
);

/**
 * @brief Sets the custom data of every admin route.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] custom_data the data
 */
CERVER_EXPORT void cerver_http_admin_routes_set_custom_data (
	CerverHttp *http_cerver, void *custom_data
);

/**
 * @brief Sets how the custom data of the admin routes is released when the CerverHttp is deleted.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] delete_custom_data releases the data, NULL to keep it
 */
CERVER_EXPORT void cerver_http_admin_routes_set_delete_custom_data (
	CerverHttp *http_cerver, void (*delete_custom_data)(void *)
);

/**
 * @brief Enables the CORS headers in the responses of the admin routes.
 *
 * They use the admin origin (cerver_http_admin_set_origin ()), or, without it, the origin of the request when it
 * is in the origins whitelist.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] enable true to enable them, the initial value is #CERVER_HTTP_DEFAULT_ENABLE_ADMIN_CORS
 */
CERVER_EXPORT void cerver_http_enable_admin_cors_headers (
	CerverHttp *http_cerver, const bool enable
);

/**
 * @brief Sets the origin that the CORS headers of the admin responses always use.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] domain the origin
 */
CERVER_EXPORT void cerver_http_admin_set_origin (
	CerverHttp *http_cerver, const char *domain
);

/**
 * @brief Registers a file system whose stats the admin routes report.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] path the path of the file system, copied; NULL is not registered
 */
CERVER_EXPORT void cerver_http_register_admin_file_system (
	CerverHttp *http_cerver, const char *path
);

/**
 * @brief Registers a worker whose state the admin routes report.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] worker the worker, it must live as long as the CerverHttp; NULL is not registered
 */
CERVER_EXPORT void cerver_http_register_admin_worker (
	CerverHttp *http_cerver, const struct _Worker *worker
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
