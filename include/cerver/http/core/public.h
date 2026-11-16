/**
 * @file cerver/http/core/public.h
 * @brief The static paths of an HttpCerver: the directories whose files are served, with their authentication.
 */

#ifndef _CERVER_HTTP_CORE_PUBLIC_H_
#define _CERVER_HTTP_CORE_PUBLIC_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_public Static paths
 * @ingroup http_core
 * @brief The directories whose files the cerver serves.
 * @{
 */

/**
 * @brief A directory whose files the cerver serves, with its authentication.
 */
typedef struct HttpStaticPath {

	/** @brief The path of the directory. */
	String *path;

	/** @brief How the requests of its files authenticate. */
	HttpRouteAuthType auth_type;

	/** @brief Decodes the data of a bearer JWT from its claims, for the request of a file. */
	HttpDecodeData decode_data;
	/** @brief Releases the decoded data after the request. */
	void (*delete_decoded_data)(void *);

	/** @brief The method of HTTP_ROUTE_AUTH_TYPE_CUSTOM. */
	HttpAuthenticationHandler authentication_handler;

} HttpStaticPath;

/**
 * @brief Sets the authentication of the files of a whole static path.
 *
 * A request is authenticated with the static path that has its file, or with the first one that has
 * authentication when none has it, so a file that does not exist is answered like a protected one; a request that
 * fails it is answered with 401, before its file is looked for.
 *
 * @param[in,out] static_path the static path, NULL is ignored
 * @param[in] auth_type how its requests authenticate
 */
CERVER_EXPORT void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
);

/**
 * @brief Sets how the data of the bearer JWT of a static path (HTTP_ROUTE_AUTH_TYPE_BEARER) is decoded, and how it
 * is released after the request.
 *
 * @param[in,out] static_path the static path, NULL is ignored
 * @param[in] decode_data decodes the claims of the token
 * @param[in] delete_decoded_data releases the decoded data
 * @since 3.0b-7 decode_data is a HttpDecodeData: it takes the claims as a value of the JSON of cerver.
 */
CERVER_EXPORT void http_static_path_set_decode_data (
	HttpStaticPath *static_path,
	HttpDecodeData decode_data,
	void (*delete_decoded_data)(void *)
);

/**
 * @brief Sets the method that authenticates the requests of a static path with HTTP_ROUTE_AUTH_TYPE_CUSTOM.
 *
 * @param[in,out] static_path the static path, NULL is ignored
 * @param[in] authentication_handler returns 0 when the request is authenticated and 1 on error
 */
CERVER_EXPORT void http_static_path_set_authentication_handler (
	HttpStaticPath *static_path,
	HttpAuthenticationHandler authentication_handler
);

/**
 * @brief Adds a directory whose files are served when they are requested.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] static_path the path, copied; an absolute path is recommended
 * @return the new static path, that the HttpCerver owns; NULL for a NULL argument or if it could not be allocated
 */
CERVER_EXPORT HttpStaticPath *http_cerver_static_path_add (
	HttpCerver *http_cerver, const char *static_path
);

/**
 * @brief Removes a directory from the static paths and releases it.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] static_path the path, compared with the paths of the static paths
 * @retval 0 the static path was removed
 * @retval 1 a NULL argument or the path is not a static path
 */
CERVER_EXPORT u8 http_receive_public_path_remove (
	HttpCerver *http_cerver, const char *static_path
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
