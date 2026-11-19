/**
 * @file cerver/http/responses/response.h
 * @brief The response of a context: its status, its headers and how it is sent; and the prepared responses.
 */

#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/core/context.h"
#include "cerver/http/headers/types.h"
#include "cerver/http/requests/content.h"
#include "cerver/http/responses/status.h"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpResponse;

/**
 * @defgroup http_responses Responses
 * @ingroup http
 * @brief The responses of HTTP: the one that a handler writes in the context of its request, the prepared ones,
 * the status and the content types.
 */

/**
 * @defgroup http_response Response
 * @ingroup http_responses
 * @brief How a handler answers its request.
 *
 * The response of a request is written to its context: a handler sets its status and its headers, and then it
 * sends it with one of the send methods, that write it to the connection of the request. cerver adds the headers
 * of the connection and of the request (Connection, Date, the id of the request, the CORS, the entity tag and
 * the compression of its endpoint) and the Content-Length of what is sent.
 *
 * A request takes one response. A handler that returns without sending one is answered with 500; a second
 * response is refused. A response to a HEAD request is sent without its body.
 * @{
 */

/**
 * @defgroup http_response_write Status and headers
 * @ingroup http_response
 * @brief What a handler sets before it sends its response.
 * @{
 */

/**
 * @brief Sets the status of the response of a request.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status; without this call a response is sent with HTTP_STATUS_OK
 * @since 3.0b-12
 */
CERVER_EXPORT void cerver_http_response_set_status (
	CerverHttpContext *context, const CerverHttpStatus status
);

/**
 * @brief Sets a header of the response of a request; the values that the header had are replaced.
 *
 * @param[in,out] context the context of the request
 * @param[in] header the header, one of #HTTP_HEADERS_MAP
 * @param[in] value its value, copied
 * @retval 0 the header was set
 * @retval 1 a NULL context or value, a value that is not valid in a header (it has a line break), or the header
 * did not fit
 * @since 3.0b-12 it replaces http_response_header_set () and http_response_add_header ().
 */
CERVER_EXPORT u8 cerver_http_response_set_header (
	CerverHttpContext *context,
	const CerverHttpHeader header, const char *value
);

/**
 * @brief Adds a value to a header of the response of a request, for the headers that are repeated (Set-Cookie).
 *
 * @param[in,out] context the context of the request
 * @param[in] header the header, one of #HTTP_HEADERS_MAP
 * @param[in] value the value, copied
 * @retval 0 the value was added
 * @retval 1 it was not, see cerver_http_response_set_header ()
 * @since 3.0b-12 it replaces http_response_header_add ().
 */
CERVER_EXPORT u8 cerver_http_response_add_header (
	CerverHttpContext *context,
	const CerverHttpHeader header, const char *value
);

/**
 * @brief Sets a header of the response of a request with its name; the values that the header had are replaced.
 *
 * @param[in,out] context the context of the request
 * @param[in] name the name of the header
 * @param[in] value its value, copied
 * @retval 0 the header was set
 * @retval 1 a NULL argument, a name or a value that is not valid in a header, or the header did not fit
 * @since 3.0b-12 it replaces http_response_header_set_with_name ().
 */
CERVER_EXPORT u8 cerver_http_response_set_header_with_name (
	CerverHttpContext *context,
	const char *name, const char *value
);

/**
 * @brief Adds a value to a header of the response of a request with its name.
 *
 * @param[in,out] context the context of the request
 * @param[in] name the name of the header
 * @param[in] value the value, copied
 * @retval 0 the value was added
 * @retval 1 it was not, see cerver_http_response_set_header_with_name ()
 * @since 3.0b-12 it replaces http_response_header_add_with_name ().
 */
CERVER_EXPORT u8 cerver_http_response_add_header_with_name (
	CerverHttpContext *context,
	const char *name, const char *value
);

/** @} */

/**
 * @defgroup http_response_send Send
 * @ingroup http_response
 * @brief The methods that send the response of a request, with the status and the headers of its context.
 * @{
 */

/**
 * @brief Sends the response of a request with a body, as it is: its Content-Type is the one that the handler set.
 *
 * @param[in,out] context the context of the request
 * @param[in] data the body, copied to the connection; NULL for a response without one
 * @param[in] length the bytes of the body
 * @retval 0 the response was given to the connection
 * @retval 1 a NULL context, a request that was already answered, or the connection failed
 * @since 3.0b-12 it replaces http_response_create_and_send () and http_response_send_split ().
 */
CERVER_EXPORT u8 cerver_http_response_send (
	CerverHttpContext *context,
	const void *data, const size_t length
);

/**
 * @brief Sends the response of a request with a body of a content type.
 *
 * @param[in,out] context the context of the request
 * @param[in] type the content type of the body, one of #HTTP_CONTENT_TYPE_MAP
 * @param[in] data the body, copied to the connection
 * @param[in] length the bytes of the body
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12
 */
CERVER_EXPORT u8 cerver_http_response_send_content (
	CerverHttpContext *context, const CerverHttpContentType type,
	const void *data, const size_t length
);

/**
 * @brief Sends the response of a request with a text/plain body.
 *
 * @param[in,out] context the context of the request
 * @param[in] text the text, copied to the connection
 * @param[in] length the bytes of the text
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_render_text ().
 */
CERVER_EXPORT u8 cerver_http_response_send_text (
	CerverHttpContext *context,
	const char *text, const size_t length
);

/**
 * @brief Sends the response of a request with an application/json body.
 *
 * @param[in,out] context the context of the request
 * @param[in] json the JSON text, copied to the connection; it is sent as it is
 * @param[in] length the bytes of the JSON
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_render_json (), http_response_json_custom_send () and
 * http_response_json_custom_reference_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json (
	CerverHttpContext *context,
	const char *json, const size_t length
);

/**
 * @brief Sends a file as the response of a request, with the content type of its extension, its Last-Modified
 * and its entity tag; a request that already has the file is answered with 304.
 *
 * @param[in,out] context the context of the request
 * @param[in] filename the path of the file
 * @retval 0 the file was given to the connection
 * @retval 1 the file was not found (the request is not answered: its handler answers it, for example with
 * CERVER_HTTP_COMMON_RESPONSE_NOT_FOUND), or the connection failed
 * @since 3.0b-12 it replaces http_response_send_file () and http_response_send_file_generate ().
 */
CERVER_EXPORT u8 cerver_http_response_send_file (
	CerverHttpContext *context, const char *filename
);

/**
 * @brief Sends a video as the response of a request: the range of bytes that the request asks for, with 206, or
 * the whole file.
 *
 * @param[in,out] context the context of the request
 * @param[in] filename the path of the video
 * @retval 0 the video was given to the connection, or its range can not be served and the request was answered
 * with 416
 * @retval 1 the file was not found (the request is not answered: its handler answers it), or the connection
 * failed
 * @since 3.0b-12 it replaces http_response_handle_video () and http_response_handle_video_generate ().
 */
CERVER_EXPORT u8 cerver_http_response_send_video (
	CerverHttpContext *context, const char *filename
);

/** @} */

/**
 * @defgroup http_response_json JSON answers
 * @ingroup http_response
 * @brief One call answers of an object with one member; each one sets the status of the response and sends it.
 * @{
 */

/**
 * @brief Answers a request with `{"msg": "<message>"}`.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status of the response
 * @param[in] message the message, escaped as a JSON string
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_json_msg_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json_message (
	CerverHttpContext *context,
	const CerverHttpStatus status, const char *message
);

/**
 * @brief Answers a request with `{"error": "<error>"}`.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status of the response
 * @param[in] error the error, escaped as a JSON string
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_json_error_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json_error (
	CerverHttpContext *context,
	const CerverHttpStatus status, const char *error
);

/**
 * @brief Answers a request with `{"<key>": "<value>"}`.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status of the response
 * @param[in] key the key, escaped as a JSON string
 * @param[in] value the value, escaped as a JSON string
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_json_key_value_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json_key_value (
	CerverHttpContext *context,
	const CerverHttpStatus status, const char *key, const char *value
);

/**
 * @brief Answers a request with `{"<key>": <value>}`, an integer.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status of the response
 * @param[in] key the key, escaped as a JSON string
 * @param[in] value the value
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_json_int_value_send () and http_response_json_large_int_value_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json_integer (
	CerverHttpContext *context,
	const CerverHttpStatus status, const char *key, const i64 value
);

/**
 * @brief Answers a request with `{"<key>": <value>}`, a real number.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status of the response
 * @param[in] key the key, escaped as a JSON string
 * @param[in] value the value
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_json_real_value_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json_real (
	CerverHttpContext *context,
	const CerverHttpStatus status, const char *key, const double value
);

/**
 * @brief Answers a request with `{"<key>": true}` or `{"<key>": false}`.
 *
 * @param[in,out] context the context of the request
 * @param[in] status the status of the response
 * @param[in] key the key, escaped as a JSON string
 * @param[in] value the value
 * @retval 0 the response was given to the connection
 * @retval 1 it was not, see cerver_http_response_send ()
 * @since 3.0b-12 it replaces http_response_json_bool_value_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_json_boolean (
	CerverHttpContext *context,
	const CerverHttpStatus status, const char *key, const bool value
);

/** @} */

/**
 * @defgroup http_response_stream Stream
 * @ingroup http_response
 * @brief A response whose length is not known when it starts: it is sent in pieces, with the chunked transfer
 * encoding of HTTP/1.1.
 * @{
 */

/**
 * @brief Starts the response of a request as a stream: its status and its headers are sent, and its body follows
 * in pieces.
 *
 * @param[in,out] context the context of the request
 * @retval 0 the stream started
 * @retval 1 a NULL context, a request that was already answered, or the connection failed
 * @warning A handler that runs in an event loop writes its pieces to the memory of its connection, and they are
 * sent when it returns: a long stream belongs to a pool of threads (cerver/http/core/pool.h), whose handler waits
 * for its client when it writes faster than it is read.
 * @since 3.0b-12 it replaces http_response_stream_begin (), that took a response.
 */
CERVER_EXPORT u8 cerver_http_response_stream_begin (
	CerverHttpContext *context
);

/**
 * @brief Sends a piece of the body of a response that was started as a stream.
 *
 * @param[in,out] context the context of the request
 * @param[in] data the piece, copied to the connection
 * @param[in] length the bytes of the piece; a piece without bytes is not sent
 * @retval 0 the piece was given to the connection
 * @retval 1 the response is not a stream, or the connection failed: the handler stops writing
 * @since 3.0b-12 it replaces http_response_stream_write ().
 */
CERVER_EXPORT u8 cerver_http_response_stream_write (
	CerverHttpContext *context,
	const void *data, const size_t length
);

/**
 * @brief Ends a response that was started as a stream; a handler that returns without ending its stream has it
 * ended by cerver.
 *
 * @param[in,out] context the context of the request
 * @retval 0 the end of the stream was given to the connection
 * @retval 1 the response is not a stream, or the connection failed
 * @since 3.0b-12 it replaces http_response_stream_end ().
 */
CERVER_EXPORT u8 cerver_http_response_stream_end (
	CerverHttpContext *context
);

/**
 * @brief Returns whether the response of a request is a stream that has not ended.
 *
 * @param[in] context the context of the request
 * @retval true the stream started and it has not ended nor failed
 * @retval false any other response
 * @since 3.0b-12 it replaces http_response_stream_is_active ().
 */
CERVER_EXPORT bool cerver_http_response_stream_is_active (
	const CerverHttpContext *context
);

/**
 * @brief Called when what was written of the stream of a deferred response was sent to its client: the next
 * piece can be written.
 *
 * @param[in,out] context the context of the request
 * @param[in] data what was passed to cerver_http_response_stream_on_drained ()
 * @warning It runs in the event loop of the request: it must not wait.
 * @since 3.0b-13
 */
typedef void (*CerverHttpStreamDrained) (
	CerverHttpContext *context, void *data
);

/**
 * @brief Sets the method that the event loop of a request calls each time that what was written of its stream
 * was sent: a handler of an event loop writes a long stream piece by piece, without keeping it in memory and
 * without a thread that waits for its client.
 *
 * The handler starts its stream, writes its first piece, sets this method, defers its response
 * (cerver_http_context_defer ()) and returns. The method writes the next piece each time that it is called, and
 * after the last one it ends the stream and completes the request (cerver_http_context_complete ()): it is not
 * called again.
 *
 * @param[in,out] context the context of the request, in its handler
 * @param[in] drained the method, NULL to set none
 * @param[in] data passed to the method
 * @retval 0 the method was set
 * @retval 1 a NULL context, a response that is not a stream, or a handler that does not run in an event loop:
 * the handler of a pool of threads waits for its client when it writes, it needs no method
 * @note The method is called only after bytes were sent: a stream whose handler wrote nothing before it
 * returned gets no call.
 * @threadsafety The thread that runs the handler of the request.
 * @since 3.0b-13
 */
CERVER_EXPORT u8 cerver_http_response_stream_on_drained (
	CerverHttpContext *context, CerverHttpStreamDrained drained, void *data
);

/** @} */

/**
 * @defgroup http_response_prepared Prepared responses
 * @ingroup http_response
 * @brief A response that is built once and sent to many requests: its bytes are ready, so sending it only adds
 * the headers of each request.
 *
 * A prepared response is created and changed before the cerver starts (or by one thread, before the others know
 * it); after that it is only sent, by any number of threads at the same time.
 * @{
 */

/**
 * @brief A response that is built once and sent many times.
 *
 * @since 3.0b-12 it replaces HttpResponse, whose fields are not public anymore.
 */
typedef struct _HttpResponse CerverHttpPrepared;

/**
 * @brief Creates a prepared response with a status and a body.
 *
 * @param[in] status the status
 * @param[in] type the content type of the body, HTTP_CONTENT_TYPE_NONE for none
 * @param[in] data the body, copied; NULL for a response without one
 * @param[in] length the bytes of the body
 * @return the response, released with cerver_http_prepared_delete (); NULL if it could not be created
 * @since 3.0b-12 it replaces http_response_create () followed by http_response_compile ().
 */
CERVER_EXPORT CerverHttpPrepared *cerver_http_prepared_create (
	const CerverHttpStatus status, const CerverHttpContentType type,
	const void *data, const size_t length
);

/**
 * @brief Creates a prepared response with `{"msg": "<message>"}`.
 *
 * @param[in] status the status
 * @param[in] message the message, escaped as a JSON string
 * @return the response, released with cerver_http_prepared_delete (); NULL if it could not be created
 * @since 3.0b-12 it replaces http_response_json_msg ().
 */
CERVER_EXPORT CerverHttpPrepared *cerver_http_prepared_create_json_message (
	const CerverHttpStatus status, const char *message
);

/**
 * @brief Creates a prepared response with `{"error": "<error>"}`.
 *
 * @param[in] status the status
 * @param[in] error the error, escaped as a JSON string
 * @return the response, released with cerver_http_prepared_delete (); NULL if it could not be created
 * @since 3.0b-12 it replaces http_response_json_error ().
 */
CERVER_EXPORT CerverHttpPrepared *cerver_http_prepared_create_json_error (
	const CerverHttpStatus status, const char *error
);

/**
 * @brief Creates a prepared response with `{"<key>": "<value>"}`.
 *
 * @param[in] status the status
 * @param[in] key the key, escaped as a JSON string
 * @param[in] value the value, escaped as a JSON string
 * @return the response, released with cerver_http_prepared_delete (); NULL if it could not be created
 * @since 3.0b-12 it replaces http_response_json_key_value ().
 */
CERVER_EXPORT CerverHttpPrepared *cerver_http_prepared_create_json_key_value (
	const CerverHttpStatus status, const char *key, const char *value
);

/**
 * @brief Sets a header of a prepared response; the values that the header had are replaced.
 *
 * @param[in,out] prepared the response
 * @param[in] header the header, one of #HTTP_HEADERS_MAP
 * @param[in] value its value, copied
 * @retval 0 the header was set and the response is ready again
 * @retval 1 a NULL argument, a value that is not valid in a header, or the header did not fit
 * @warning Only before the response is shared between threads.
 * @since 3.0b-12 it replaces http_response_header_set () of a response.
 */
CERVER_EXPORT u8 cerver_http_prepared_set_header (
	CerverHttpPrepared *prepared,
	const CerverHttpHeader header, const char *value
);

/**
 * @brief Sets a header of a prepared response with its name; the values that the header had are replaced.
 *
 * @param[in,out] prepared the response
 * @param[in] name the name of the header
 * @param[in] value its value, copied
 * @retval 0 the header was set and the response is ready again
 * @retval 1 a NULL argument, a name or a value that is not valid in a header, or the header did not fit
 * @warning Only before the response is shared between threads.
 * @since 3.0b-12 it replaces http_response_header_set_with_name () of a response.
 */
CERVER_EXPORT u8 cerver_http_prepared_set_header_with_name (
	CerverHttpPrepared *prepared,
	const char *name, const char *value
);

/**
 * @brief Releases a prepared response.
 *
 * @param[in] prepared the response, it can be NULL; never one of the common ones of cerver
 * @since 3.0b-12 it replaces http_response_delete ().
 */
CERVER_EXPORT void cerver_http_prepared_delete (
	CerverHttpPrepared *prepared
);

/**
 * @brief Sends a prepared response as the response of a request; the status and the headers of its context are
 * not used.
 *
 * @param[in,out] context the context of the request
 * @param[in] prepared the response
 * @retval 0 the response was given to the connection
 * @retval 1 a NULL argument, a request that was already answered, or the connection failed
 * @since 3.0b-12 it replaces http_response_send ().
 */
CERVER_EXPORT u8 cerver_http_response_send_prepared (
	CerverHttpContext *context, const CerverHttpPrepared *prepared
);

/**
 * @brief The prepared responses that every cerver has.
 *
 * @since 3.0b-12 they replace the global responses `oki_doki`, `bad_request_error`, `bad_user_error`,
 * `bad_auth_error`, `not_found_error` and `server_error`.
 */
typedef enum CerverHttpCommonResponse {

	/** @brief 200 with `{"oki": "doki"}`. */
	CERVER_HTTP_COMMON_RESPONSE_OK				= 0,
	/** @brief 400 with `{"error": "Bad request!"}`. */
	CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST		= 1,
	/** @brief 400 with `{"error": "Bad user!"}`. */
	CERVER_HTTP_COMMON_RESPONSE_BAD_USER		= 2,
	/** @brief 401 with `{"error": "Failed to authenticate!"}`. */
	CERVER_HTTP_COMMON_RESPONSE_BAD_AUTH		= 3,
	/** @brief 404 with `{"error": "Not found!"}`. */
	CERVER_HTTP_COMMON_RESPONSE_NOT_FOUND		= 4,
	/** @brief 500 with `{"error": "Internal error!"}`. */
	CERVER_HTTP_COMMON_RESPONSE_SERVER_ERROR	= 5,

	/** @brief The number of common responses, the first value that is not valid. */
	CERVER_HTTP_COMMON_RESPONSE_SIZE			= 6

} CerverHttpCommonResponse;

/**
 * @brief Answers a request with one of the common responses of cerver.
 *
 * @param[in,out] context the context of the request
 * @param[in] common the common response
 * @retval 0 the response was given to the connection
 * @retval 1 a NULL context, a value out of #CerverHttpCommonResponse, a request that was already answered, or the
 * connection failed
 * @since 3.0b-12
 */
CERVER_EXPORT u8 cerver_http_response_send_common (
	CerverHttpContext *context, const CerverHttpCommonResponse common
);

/** @} */

/** @} */

#ifdef __cplusplus
}
#endif

#endif
