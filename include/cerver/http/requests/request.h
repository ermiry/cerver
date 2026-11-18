/**
 * @file cerver/http/requests/request.h
 * @brief The request of a context: its method, its path, its params, its headers and its body, as views.
 */

#ifndef _CERVER_HTTP_REQUEST_H_
#define _CERVER_HTTP_REQUEST_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/core/context.h"
#include "cerver/http/core/utils.h"
#include "cerver/http/headers/types.h"
#include "cerver/http/parser/head.h"
#include "cerver/http/requests/content.h"
#include "cerver/http/requests/method.h"

#include "cerver/json/json.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _MultiPart;

/**
 * @defgroup http_request Request
 * @ingroup http_requests
 * @brief What a handler reads of its request.
 *
 * Every getter takes the context of the request. What it returns belongs to the request: it is valid until the
 * request ends and it must not be changed nor released. A getter of a NULL context returns no value.
 * @{
 */

/**
 * @brief Creates the data of an application from the claims of the token of a request.
 *
 * An endpoint, a group or a static path with a bearer authentication calls it once its token was verified; what
 * it returns is the decoded data of the request, cerver_http_request_decoded_data ().
 *
 * @param[in] claims the claims of the token, an object of the JSON of cerver; it is valid only during the call, so
 * what is needed of it is copied
 * @return the data of the application, NULL for none
 * @since 3.0b-12 it replaces HttpDecodeData.
 */
typedef void *(*CerverHttpDecodeData) (const CerverJsonValue *claims);

/**
 * @defgroup http_request_line Method and path
 * @ingroup http_request
 * @{
 */

/**
 * @brief Returns the method of a request.
 *
 * @param[in] context the context of the request
 * @return the method, REQUEST_METHOD_UNDEFINED for a NULL context
 * @since 3.0b-12 it replaces http_request_get_method ().
 */
CERVER_EXPORT CerverHttpMethod cerver_http_request_method (
	const CerverHttpContext *context
);

/**
 * @brief Returns whether a request is a HEAD that the handler of GET answers: its response is sent without its
 * body.
 *
 * @param[in] context the context of the request
 * @retval true the handler of GET is answering a HEAD
 * @retval false any other request
 * @since 3.0b-12 it replaces http_receive_is_head ().
 */
CERVER_EXPORT bool cerver_http_request_is_head (
	const CerverHttpContext *context
);

/**
 * @brief Returns the path of the url of a request, like "/api/users/5".
 *
 * @param[in] context the context of the request
 * @return the path, as it was received (not decoded)
 * @since 3.0b-12 it replaces http_request_get_url ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_path (
	const CerverHttpContext *context
);

/**
 * @brief Returns the query of the url of a request, what follows its '?'.
 *
 * @param[in] context the context of the request
 * @return the query, as it was received (not decoded); no value for a url without one
 * @since 3.0b-12 it replaces http_request_get_query ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_query (
	const CerverHttpContext *context
);

/**
 * @brief Returns the id of a request: the X-Request-Id that it brought, or the one that the cerver generated.
 *
 * @param[in] context the context of the request
 * @return the id; no value when the cerver does not identify its requests
 * @since 3.0b-12 it replaces http_request_get_id ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_id (
	const CerverHttpContext *context
);

/**
 * @brief Returns the address of the client of a request: the peer of its connection, or the one that a trusted
 * proxy forwarded.
 *
 * @param[in] context the context of the request
 * @return the address, as text
 * @since 3.0b-12 it replaces http_request_get_client_ip ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_client_ip (
	const CerverHttpContext *context
);

/**
 * @brief Returns the address of the peer of the connection of a request: who connected to the cerver, a proxy when
 * there is one in front of it.
 *
 * @param[in] context the context of the request
 * @return the address, as text; no value for a NULL context
 * @note cerver_http_request_client_ip () is the address of the client: this one, unless the peer is a trusted proxy
 * that forwarded another.
 * @since 3.0b-12
 */
CERVER_EXPORT CerverHttpView cerver_http_request_peer_ip (
	const CerverHttpContext *context
);

/**
 * @brief Returns the head of a request as the parser read it: its version, its target as it arrived and every
 * field in the order of the message, with the names and the values that the getters of the headers do not keep
 * (a repeated field, a name that cerver does not know).
 *
 * Each slice of the head is read with cerver_http_head_at ().
 *
 * @param[in] context the context of the request
 * @return the head, NULL for a NULL context
 * @warning The bytes of the head are the copy of the request: the target has a '\0' where its query starts, and
 * the head is valid until the request ends.
 * @since 3.0b-12
 */
CERVER_EXPORT const CerverHttpHead *cerver_http_request_head (
	const CerverHttpContext *context
);

/** @} */

/**
 * @defgroup http_request_params Params
 * @ingroup http_request
 * @brief The params of the route and the values of the query.
 * @{
 */

/**
 * @brief Returns the number of params of the route of a request.
 *
 * @param[in] context the context of the request
 * @return the params that its path matched
 * @since 3.0b-12 it replaces http_request_get_n_params ().
 */
CERVER_EXPORT unsigned int cerver_http_request_params_count (
	const CerverHttpContext *context
);

/**
 * @brief Returns a param of the route of a request with its name, like "id" in "/users/:id".
 *
 * @param[in] context the context of the request
 * @param[in] name the name of the param in the path of its endpoint
 * @return the param, percent-decoded; no value when the route has no param with that name
 * @since 3.0b-12 it replaces http_request_get_param ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_param (
	const CerverHttpContext *context, const char *name
);

/**
 * @brief Returns a param of the route of a request with its position.
 *
 * @param[in] context the context of the request
 * @param[in] index the position of the param in the path, from 0
 * @return the param, percent-decoded; no value for a position that the route does not have
 * @since 3.0b-12 it replaces http_request_get_param_at_idx ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_param_at (
	const CerverHttpContext *context, const unsigned int index
);

/**
 * @brief Returns the name of a param of the route of a request.
 *
 * @param[in] context the context of the request
 * @param[in] index the position of the param in the path, from 0
 * @return the name, a string of its endpoint; NULL for a param without name and for a position that the route
 * does not have
 * @since 3.0b-12 it replaces http_request_get_param_name_at_idx ().
 */
CERVER_EXPORT const char *cerver_http_request_param_name_at (
	const CerverHttpContext *context, const unsigned int index
);

/**
 * @brief Returns the number of values of the query of a request.
 *
 * @param[in] context the context of the request
 * @return the pairs of its query, with the repeated keys
 * @since 3.0b-12
 */
CERVER_EXPORT size_t cerver_http_request_query_values_count (
	const CerverHttpContext *context
);

/**
 * @brief Returns the value of a key of the query of a request.
 *
 * @param[in] context the context of the request
 * @param[in] key the key
 * @return the first value of the key, percent-decoded; no value when the query does not have the key
 * @since 3.0b-12 it replaces http_request_query_params_get_value ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_query_value (
	const CerverHttpContext *context, const char *key
);

/**
 * @brief Returns a pair of the query of a request with its position.
 *
 * @param[in] context the context of the request
 * @param[in] index the position of the pair in the query, from 0
 * @param[out] key the key of the pair, percent-decoded; it can be NULL
 * @return the value of the pair, percent-decoded; no value for a position that the query does not have
 * @since 3.0b-12 it replaces the list of http_request_get_query_params ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_query_value_at (
	const CerverHttpContext *context, const size_t index, CerverHttpView *key
);

/** @} */

/**
 * @defgroup http_request_headers Headers
 * @ingroup http_request
 * @{
 */

/**
 * @brief Returns a header of a request.
 *
 * @param[in] context the context of the request
 * @param[in] header the header, one of #HTTP_HEADERS_MAP
 * @return its value, the ones of a repeated header joined with ", "; no value when the request does not have it
 * @since 3.0b-12 it replaces http_request_get_header ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_header (
	const CerverHttpContext *context, const CerverHttpHeader header
);

/**
 * @brief Returns a header of a request with its name, also one that #HTTP_HEADERS_MAP does not have.
 *
 * @param[in] context the context of the request
 * @param[in] name the name of the header, in any case
 * @return its value; no value when the request does not have it
 * @since 3.0b-12 it replaces http_request_get_custom_header ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_header_with_name (
	const CerverHttpContext *context, const char *name
);

/**
 * @brief Returns the content type of a request.
 *
 * @param[in] context the context of the request
 * @return the type of its Content-Type header, HTTP_CONTENT_TYPE_NONE for a request without one or with one
 * that #HTTP_CONTENT_TYPE_MAP does not have
 * @since 3.0b-12 it replaces http_request_get_content_type ().
 */
CERVER_EXPORT CerverHttpContentType cerver_http_request_content_type (
	const CerverHttpContext *context
);

/**
 * @brief Returns whether the content type of a request is application/json.
 *
 * @param[in] context the context of the request
 * @retval true its Content-Type is JSON
 * @retval false any other one, or none
 * @since 3.0b-12 it replaces http_request_content_type_is_json ().
 */
CERVER_EXPORT bool cerver_http_request_is_json (
	const CerverHttpContext *context
);

/**
 * @brief Reads the Range header of a request.
 *
 * @param[in] context the context of the request
 * @param[out] bytes_range the range that the request asks for
 * @retval true the request has a range of bytes that cerver can serve
 * @retval false it has none, or one that is not valid
 * @since 3.0b-12 it replaces http_request_get_bytes_range ().
 */
CERVER_EXPORT bool cerver_http_request_bytes_range (
	const CerverHttpContext *context, CerverHttpBytesRange *bytes_range
);

/** @} */

/**
 * @defgroup http_request_body Body
 * @ingroup http_request
 * @{
 */

/**
 * @brief Returns the body of a request that is not multipart.
 *
 * @param[in] context the context of the request
 * @return the body; no value for a request without one
 * @since 3.0b-12 it replaces http_request_get_body ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_body (
	const CerverHttpContext *context
);

/**
 * @brief Returns the body of a request as a document of the JSON of cerver.
 *
 * The body is read the first time, into the memory of the request, and the next calls return the same value.
 *
 * @param[in] context the context of the request
 * @param[out] error why the body is not a document, with CERVER_JSON_ERROR_NONE for one that is; it can be NULL
 * @return the root of the document, valid until the request ends; NULL for a request without a body and for a
 * body that is not JSON
 * @since 3.0b-12 it replaces http_request_json ().
 */
CERVER_EXPORT const CerverJsonValue *cerver_http_request_json (
	const CerverHttpContext *context, CerverJsonError *error
);

/**
 * @brief Returns the number of values of the application/x-www-form-urlencoded body of a request.
 *
 * @param[in] context the context of the request
 * @return the pairs of its body, with the repeated keys
 * @since 3.0b-12
 */
CERVER_EXPORT size_t cerver_http_request_form_values_count (
	const CerverHttpContext *context
);

/**
 * @brief Returns the value of a key of the application/x-www-form-urlencoded body of a request.
 *
 * @param[in] context the context of the request
 * @param[in] key the key
 * @return the first value of the key, percent-decoded; no value when the body does not have the key
 * @since 3.0b-12 it replaces http_request_body_get_value ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_form_value (
	const CerverHttpContext *context, const char *key
);

/**
 * @brief Returns a pair of the application/x-www-form-urlencoded body of a request with its position.
 *
 * @param[in] context the context of the request
 * @param[in] index the position of the pair in the body, from 0
 * @param[out] key the key of the pair, percent-decoded; it can be NULL
 * @return the value of the pair, percent-decoded; no value for a position that the body does not have
 * @since 3.0b-12 it replaces the list of http_request_get_body_values ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_form_value_at (
	const CerverHttpContext *context, const size_t index, CerverHttpView *key
);

/** @} */

/**
 * @defgroup http_request_authentication Authentication
 * @ingroup http_request
 * @brief What the authentication of a request left for its handler.
 * @{
 */

/**
 * @brief Returns the claims of the bearer token of a request, once it was verified.
 *
 * @param[in] context the context of the request
 * @return the claims, an object of the JSON of cerver that lives until the request ends; NULL for a request
 * without a verified token
 * @since 3.0b-12 it replaces http_request_claims ().
 */
CERVER_EXPORT const CerverJsonValue *cerver_http_request_claims (
	const CerverHttpContext *context
);

/**
 * @brief Returns the data that the decode method of its endpoint created from the token of a request.
 *
 * @param[in] context the context of the request
 * @return the decoded data, NULL when there is none
 * @since 3.0b-12 it replaces http_request_get_decoded_data ().
 */
CERVER_EXPORT const void *cerver_http_request_decoded_data (
	const CerverHttpContext *context
);

/**
 * @brief Sets the decoded data of a request, for example from a custom authentication handler.
 *
 * @param[in,out] context the context of the request
 * @param[in] decoded_data the data; the one that the request had is not released
 * @param[in] delete_decoded_data releases the data when the request ends, NULL to keep it
 * @since 3.0b-12 it replaces http_request_set_decoded_data () and http_request_set_delete_decoded_data ().
 */
CERVER_EXPORT void cerver_http_request_set_decoded_data (
	CerverHttpContext *context,
	void *decoded_data, void (*delete_decoded_data)(void *)
);

/**
 * @brief Returns the custom data of a request, the one that an authentication handler or a middleware set.
 *
 * @param[in] context the context of the request
 * @return the custom data, NULL when there is none
 * @since 3.0b-12 it replaces http_request_get_custom_data ().
 */
CERVER_EXPORT const void *cerver_http_request_custom_data (
	const CerverHttpContext *context
);

/**
 * @brief Sets the custom data of a request, to pass it from an authentication handler or a middleware to the
 * handler.
 *
 * @param[in,out] context the context of the request
 * @param[in] custom_data the data; the one that the request had is not released
 * @param[in] delete_custom_data releases the data when the request ends, NULL to keep it
 * @since 3.0b-12 it replaces http_request_set_custom_data () and http_request_set_delete_custom_data ().
 */
CERVER_EXPORT void cerver_http_request_set_custom_data (
	CerverHttpContext *context,
	void *custom_data, void (*delete_custom_data)(void *)
);

/** @} */

/**
 * @defgroup http_request_parts Multipart
 * @ingroup http_request
 * @brief The parts of a multipart/form-data body: its values and its files, that are saved in the uploads path
 * of the cerver while the body is received.
 * @{
 */

/**
 * @brief A part of a multipart body: a value or a file.
 *
 * @since 3.0b-12 it replaces MultiPart, whose fields are not public anymore.
 */
typedef struct _MultiPart CerverHttpPart;

/**
 * @brief Returns the number of files of the multipart body of a request.
 *
 * @param[in] context the context of the request
 * @return the parts that are files
 * @since 3.0b-12 it replaces http_request_get_n_files ().
 */
CERVER_EXPORT unsigned int cerver_http_request_files_count (
	const CerverHttpContext *context
);

/**
 * @brief Returns the number of values of the multipart body of a request.
 *
 * @param[in] context the context of the request
 * @return the parts that are values
 * @since 3.0b-12 it replaces http_request_get_n_values ().
 */
CERVER_EXPORT unsigned int cerver_http_request_values_count (
	const CerverHttpContext *context
);

/**
 * @brief Returns a part of the multipart body of a request with its name.
 *
 * @param[in] context the context of the request
 * @param[in] name the name of the part
 * @return the first part with that name, NULL when the body has none
 * @since 3.0b-12 it replaces http_request_multi_parts_get ().
 */
CERVER_EXPORT const CerverHttpPart *cerver_http_request_part (
	const CerverHttpContext *context, const char *name
);

/**
 * @brief Returns the part of the multipart body of a request that follows another one, to visit them all.
 *
 * @param[in] context the context of the request
 * @param[in] previous the part that was returned before, NULL for the first one
 * @return the next part in the order of the body, NULL after the last one
 * @since 3.0b-12 it replaces http_request_multi_parts_iter_start () and http_request_multi_parts_iter_get_next ().
 */
CERVER_EXPORT const CerverHttpPart *cerver_http_request_part_next (
	const CerverHttpContext *context, const CerverHttpPart *previous
);

/**
 * @brief Returns the value of a part of the multipart body of a request with its name.
 *
 * @param[in] context the context of the request
 * @param[in] name the name of the part
 * @return its value; no value when the body has no value with that name
 * @since 3.0b-12 it replaces http_request_multi_parts_get_value ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_part_value (
	const CerverHttpContext *context, const char *name
);

/**
 * @brief Returns the directory where the files of a request were saved, the one that the uploads dirname
 * generator of its cerver created inside its uploads path.
 *
 * @param[in] context the context of the request
 * @return the name of the directory; no value for a cerver without a dirname generator
 * @since 3.0b-12 it replaces http_request_get_dirname ().
 */
CERVER_EXPORT CerverHttpView cerver_http_request_uploads_directory (
	const CerverHttpContext *context
);

/**
 * @brief Keeps the files of a request when it ends, in a cerver that deletes its uploads when they are done.
 *
 * @param[in,out] context the context of the request
 * @since 3.0b-12 it replaces http_request_multi_part_keep_files ().
 */
CERVER_EXPORT void cerver_http_request_keep_files (
	CerverHttpContext *context
);

/**
 * @brief Deletes the files of a request that were saved, and its uploads directory if it ends empty.
 *
 * @param[in] context the context of the request
 * @since 3.0b-12 it replaces http_request_multi_part_discard_files ().
 */
CERVER_EXPORT void cerver_http_request_discard_files (
	const CerverHttpContext *context
);

/**
 * @brief Returns whether a part is a file.
 *
 * @param[in] part the part
 * @retval true it is a file, saved in the uploads path
 * @retval false it is a value, or the part is NULL
 * @since 3.0b-12 it replaces http_multi_part_is_file ().
 */
CERVER_EXPORT bool cerver_http_part_is_file (
	const CerverHttpPart *part
);

/**
 * @brief Returns the name of a part.
 *
 * @param[in] part the part
 * @return its name; no value for a NULL part
 * @since 3.0b-12 it replaces http_multi_part_get_name ().
 */
CERVER_EXPORT CerverHttpView cerver_http_part_name (
	const CerverHttpPart *part
);

/**
 * @brief Returns the value of a part that is not a file.
 *
 * @param[in] part the part
 * @return its value; no value for a file and for a NULL part
 * @since 3.0b-12 it replaces http_multi_part_get_value ().
 */
CERVER_EXPORT CerverHttpView cerver_http_part_value (
	const CerverHttpPart *part
);

/**
 * @brief Returns the name that the client gave to the file of a part.
 *
 * @param[in] part the part
 * @return the filename of its Content-Disposition; no value for a part that is not a file
 * @warning It is what the client sent: it is never used as a path as it is.
 * @since 3.0b-12 it replaces http_multi_part_get_filename ().
 */
CERVER_EXPORT CerverHttpView cerver_http_part_filename (
	const CerverHttpPart *part
);

/**
 * @brief Returns where the file of a part was saved.
 *
 * @param[in] part the part
 * @return the path of the saved file; no value for a part that is not a file and for one that could not be
 * saved
 * @since 3.0b-12 it replaces http_multi_part_get_saved_filename ().
 */
CERVER_EXPORT CerverHttpView cerver_http_part_saved_filename (
	const CerverHttpPart *part
);

/**
 * @brief Returns the bytes of the file of a part that were saved.
 *
 * @param[in] part the part
 * @return the bytes that were written, 0 for a part that is not a file
 * @since 3.0b-12 it replaces http_multi_part_get_total_wrote ().
 */
CERVER_EXPORT size_t cerver_http_part_size (
	const CerverHttpPart *part
);

/**
 * @brief Moves the saved file of a part to another path, in the same file system.
 *
 * @param[in,out] part the part, of the request that is being handled
 * @param[in] destination the new path of the file
 * @retval 0 the file was moved, and the saved filename of the part is the new path
 * @retval 1 the part is not a saved file, or the file could not be moved
 * @since 3.0b-12 it replaces http_multi_part_move_file ().
 */
CERVER_EXPORT unsigned int cerver_http_part_move_file (
	const CerverHttpPart *part, const char *destination
);

/** @} */

/**
 * @defgroup http_request_uploads Uploads generators
 * @ingroup http_request
 * @brief What the generators of the uploads of a cerver use (cerver/http/core/uploads.h): they run while the body
 * of a multipart request is received, before its handler.
 * @{
 */

/**
 * @brief Returns the part of a multipart request that is being received: the file that the uploads filename
 * generator names.
 *
 * @param[in] context the context of the request
 * @return the part, with its name and the filename that the client sent; NULL outside a generator
 * @since 3.0b-12 it replaces http_request_get_current_mpart ().
 */
CERVER_EXPORT const CerverHttpPart *cerver_http_request_current_part (
	const CerverHttpContext *context
);

/**
 * @brief Sets the name with which the file of a part is saved, from an uploads filename generator.
 *
 * @param[in,out] part the part that is being received, cerver_http_request_current_part ()
 * @param[in] format the format of the name, like the one of printf (), followed by its values
 * @note A name that is not set, or that is empty, is replaced by the sanitized filename of the part.
 * @since 3.0b-12 it replaces http_multi_part_set_generated_filename ().
 */
CERVER_EXPORT void cerver_http_part_set_generated_filename (
	const CerverHttpPart *part, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Sets the directory, inside the uploads path of the cerver, where the files of a request are saved, from
 * an uploads dirname generator.
 *
 * @param[in,out] context the context of the request
 * @param[in] format the format of the name of the directory, like the one of printf (), followed by its values
 * @note A name that does not fit is not used: the request is rejected.
 * @since 3.0b-12 it replaces http_request_set_dirname ().
 */
CERVER_EXPORT void cerver_http_request_set_uploads_directory (
	CerverHttpContext *context, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif
