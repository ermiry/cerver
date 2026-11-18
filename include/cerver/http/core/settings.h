/**
 * @file cerver/http/core/settings.h
 * @brief The settings of a CerverHttp: the headers that it adds, its compression, its trusted proxies, its origins,
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
 * @brief The settings of a CerverHttp.
 *
 * The setters ignore a NULL CerverHttp.
 * @{
 */

/**
 * @brief Sets whether every response has the Date header.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] date_header true to add it, the default is #CERVER_HTTP_DEFAULT_DATE_HEADER
 */
CERVER_EXPORT void cerver_http_set_date_header (
	CerverHttp *http_cerver, const bool date_header
);

/**
 * @brief Sets whether every request is identified with its X-Request-Id or with a new UUID v4.
 *
 * The X-Request-Id of a request is used when it has up to HTTP_REQUEST_ID_LENGTH characters of [A-Za-z0-9._-]; the
 * id is sent back in the X-Request-Id of the response and it is logged with the request.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] request_id true to identify them, the default is #CERVER_HTTP_DEFAULT_REQUEST_ID
 */
CERVER_EXPORT void cerver_http_set_request_id (
	CerverHttp *http_cerver, const bool request_id
);

/**
 * @brief Sets the smallest body that is compressed.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] min_size the bytes, the default is HTTP_COMPRESSION_DEFAULT_MIN_SIZE
 */
CERVER_EXPORT void cerver_http_set_compression_min_size (
	CerverHttp *http_cerver, const size_t min_size
);

/**
 * @brief Sets the level of zlib of the compression.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] level from HTTP_COMPRESSION_MIN_LEVEL to HTTP_COMPRESSION_MAX_LEVEL, another value logs a warning and
 * keeps the current one; the default is HTTP_COMPRESSION_DEFAULT_LEVEL
 */
CERVER_EXPORT void cerver_http_set_compression_level (
	CerverHttp *http_cerver, const int level
);

/**
 * @brief Sets from which peers the X-Forwarded-For and X-Real-IP headers are used to find the address of the
 * client (http_request_get_client_ip ()).
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] trusted_proxies the peers, the default is CERVER_HTTP_DEFAULT_TRUSTED_PROXIES
 */
CERVER_EXPORT void cerver_http_set_trusted_proxies (
	CerverHttp *http_cerver, const CerverHttpTrustedProxies trusted_proxies
);

/**
 * @brief Adds a network in CIDR notation ("172.16.0.0/12", "fc00::/7" or a single address) to the trusted proxies of
 * HTTP_TRUSTED_PROXIES_LIST.
 *
 * An invalid network, or more than HTTP_TRUSTED_PROXIES_SIZE, is a registration error of the routes, so the cerver
 * does not start.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] cidr the network
 * @retval 0 the network was added
 * @retval 1 a NULL CerverHttp, an invalid network or the list is full
 */
CERVER_EXPORT u8 cerver_http_add_trusted_proxy (
	CerverHttp *http_cerver, const char *cidr
);

/**
 * @brief Adds a domain to the origins whitelist.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] domain the origin, copied
 * @retval 0 the origin was added
 * @retval 1 a NULL argument or the whitelist already has HTTP_ORIGINS_SIZE origins
 */
CERVER_EXPORT u8 cerver_http_add_origin_to_whitelist (
	CerverHttp *http_cerver, const char *domain
);

/**
 * @brief Prints the origins whitelist to stdout.
 *
 * @param[in] http_cerver the CerverHttp, NULL prints nothing
 */
CERVER_EXPORT void cerver_http_print_origins_whitelist (
	const CerverHttp *http_cerver
);

/**
 * @brief Returns the custom data of the CerverHttp, that the application can set and read at any time.
 *
 * @param[in] http_cerver the CerverHttp
 * @return the data, NULL for a NULL CerverHttp
 */
CERVER_EXPORT const void *cerver_http_get_custom_data (
	const CerverHttp *http_cerver
);

/**
 * @brief Sets the custom data of the CerverHttp.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] custom_data the data
 */
CERVER_EXPORT void cerver_http_set_custom_data (
	CerverHttp *http_cerver, void *custom_data
);

/**
 * @brief Sets how the custom data is released when the CerverHttp is deleted.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] delete_custom_data releases the data, NULL to keep it
 */
CERVER_EXPORT void cerver_http_set_delete_custom_data (
	CerverHttp *http_cerver, void (*delete_custom_data)(void *)
);

/**
 * @brief Releases the custom data with free () when the CerverHttp is deleted.
 *
 * @param[in,out] http_cerver the CerverHttp
 */
CERVER_EXPORT void cerver_http_set_default_delete_custom_data (
	CerverHttp *http_cerver
);

/**
 * @brief Adds a header to every response of the CerverHttp; a response with the same header keeps its own.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] type the header
 * @param[in] actual_header the value, copied
 * @retval 0 the header was added
 * @retval 1 a NULL argument or it could not be set
 */
CERVER_PUBLIC u8 cerver_http_add_responses_header (
	CerverHttp *http_cerver,
	const CerverHttpHeader type, const char *actual_header
);

/**
 * @brief Works like cerver_http_add_responses_header () with the name of any header; it replaces the ones with the
 * same name.
 *
 * @param[in,out] http_cerver the CerverHttp
 * @param[in] name the name of the header
 * @param[in] value the value, copied
 * @retval 0 the header was added
 * @retval 1 a NULL argument or it could not be set
 */
CERVER_PUBLIC u8 cerver_http_add_responses_header_with_name (
	CerverHttp *http_cerver, const char *name, const char *value
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
