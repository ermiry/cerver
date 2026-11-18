/**
 * @file cerver/http/core/limits.h
 * @brief The limits and the timeouts of the requests of an HttpCerver, and the lenient flags of its parser.
 */

#ifndef _CERVER_HTTP_CORE_LIMITS_H_
#define _CERVER_HTTP_CORE_LIMITS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_limits Limits
 * @ingroup http_core
 * @brief How big and how slow a request can be.
 *
 * The setters ignore a NULL HttpCerver. The size limits log a warning when the value is not a power of 2, and keep
 * it anyway; the timeouts take any value.
 * @{
 */

/**
 * @brief Sets the max size in bytes of the body of a request; a bigger request is answered with 413.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_body_size the size, 0 disables the limit; the default is #HTTP_CERVER_DEFAULT_MAX_BODY_SIZE
 */
CERVER_EXPORT void http_cerver_set_max_body_size (
	HttpCerver *http_cerver, const size_t max_body_size
);

/**
 * @brief Sets the max size in bytes of the url of a request (its path and its query); a bigger url is answered with
 * 414.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_url_size the size, the limit is always active and 0 sets the default,
 * #HTTP_CERVER_DEFAULT_MAX_URL_SIZE
 */
CERVER_EXPORT void http_cerver_set_max_url_size (
	HttpCerver *http_cerver, const size_t max_url_size
);

/**
 * @brief Sets the max size in bytes of all the headers of a request; bigger headers are answered with 431.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_header_size the size, the limit is always active (the parser does not have one of its own) and 0
 * sets the default, #HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE
 */
CERVER_EXPORT void http_cerver_set_max_header_size (
	HttpCerver *http_cerver, const size_t max_header_size
);

/**
 * @brief Sets the max number of headers of a request; more headers are answered with 431.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_headers the headers, 0 disables the limit; the default is #HTTP_CERVER_DEFAULT_MAX_HEADERS
 */
CERVER_EXPORT void http_cerver_set_max_headers (
	HttpCerver *http_cerver, const unsigned int max_headers
);

/**
 * @brief Sets the max size in bytes of the value of a header that is not in the map of the HTTP headers (a custom
 * header, like the ones of a trace or the ones that a proxy adds), with the values of its repetitions joined; a
 * bigger value is answered with 431.
 *
 * The size of all the headers is still limited by http_cerver_set_max_header_size ().
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_custom_header_size the size, 0 disables the limit; the default is
 * #HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE
 */
CERVER_EXPORT void http_cerver_set_max_custom_header_size (
	HttpCerver *http_cerver, const size_t max_custom_header_size
);

/**
 * @brief Sets the max number of parts of a multipart request; more parts are answered with 413.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_multipart_parts the parts, 0 disables the limit; the default is
 * #HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS
 */
CERVER_EXPORT void http_cerver_set_max_multipart_parts (
	HttpCerver *http_cerver, const unsigned int max_multipart_parts
);

/**
 * @brief Sets the max size in bytes of each file of a multipart request; a bigger file is answered with 413.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] max_file_size the size, 0 disables the limit; the default is #HTTP_CERVER_DEFAULT_MAX_FILE_SIZE
 */
CERVER_EXPORT void http_cerver_set_max_file_size (
	HttpCerver *http_cerver, const size_t max_file_size
);

/**
 * @brief Sets the seconds to receive the headers of a request; a client that is slower (like a slowloris) is
 * answered with 408.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] headers_timeout the seconds, 0 disables the timeout; the default is
 * #HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT
 */
CERVER_EXPORT void http_cerver_set_headers_timeout (
	HttpCerver *http_cerver, const unsigned int headers_timeout
);

/**
 * @brief The same as http_cerver_set_headers_timeout (), in milliseconds.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] headers_timeout the milliseconds, 0 disables the timeout
 */
CERVER_EXPORT void http_cerver_set_headers_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int headers_timeout
);

/**
 * @brief Sets the seconds to receive a complete request (its headers and its body); a slower request is answered
 * with 408.
 *
 * While it is disabled, a body only needs to make progress before the idle timeout
 * (http_cerver_set_idle_timeout_milliseconds ()).
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] request_timeout the seconds, 0 disables the timeout; the default is
 * #HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT
 */
CERVER_EXPORT void http_cerver_set_request_timeout (
	HttpCerver *http_cerver, const unsigned int request_timeout
);

/**
 * @brief The same as http_cerver_set_request_timeout (), in milliseconds.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] request_timeout the milliseconds, 0 disables the timeout
 */
CERVER_EXPORT void http_cerver_set_request_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int request_timeout
);

/**
 * @brief Sets the most milliseconds that a connection waits without receiving anything.
 *
 * A request that had started is answered with 408, an empty connection is only closed.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] idle_timeout the milliseconds, 0 sets the default, #HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS
 */
CERVER_EXPORT void http_cerver_set_idle_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int idle_timeout
);

/**
 * @brief Sets the most milliseconds that all the sends of a response take together: its headers, its body and its
 * files.
 *
 * It applies also in a blocking socket with a longer SO_SNDTIMEO, so a client that stops reading costs that time in
 * total and not in each send; each piece of a stream has its own deadline.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] send_timeout the milliseconds; the default is 0, that uses the send timeout of the cerver
 * (cerver_set_send_timeout (), in seconds)
 */
CERVER_EXPORT void http_cerver_set_send_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int send_timeout
);

/**
 * @brief Sets whether a connection is kept after a request for the next one of its client, when the client
 * asked for it and its response tells where it ends.
 *
 * Without this call a cerver of CERVER_HANDLER_TYPE_EVENTS keeps its connections and the other handler types
 * close each one after its response.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] keep_alive true to keep the connections, false to close each one after its response
 * @warning With a handler type that is not CERVER_HANDLER_TYPE_EVENTS a connection that is kept uses a thread of
 * the pool of the cerver while it waits for its next request, up to the timeout of
 * http_cerver_set_keep_alive_timeout ().
 * @since 3.0b-5
 */
CERVER_EXPORT void http_cerver_set_keep_alive (
	HttpCerver *http_cerver, const bool keep_alive
);

/**
 * @brief Sets the most seconds that a connection that is kept waits for its next request, then it is closed
 * without a response.
 *
 * Behind a proxy that keeps its connections it has to be more than the time that the proxy keeps an idle one,
 * so the proxy is the one that closes them: a proxy that sends a request through a connection that the cerver
 * is closing answers 502.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] keep_alive_timeout the seconds, 0 sets the default, #HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT
 * @since 3.0b-5
 */
CERVER_EXPORT void http_cerver_set_keep_alive_timeout (
	HttpCerver *http_cerver, const unsigned int keep_alive_timeout
);

/**
 * @brief The same as http_cerver_set_keep_alive_timeout (), in milliseconds.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] keep_alive_timeout the milliseconds, 0 sets the default,
 * #HTTP_CERVER_DEFAULT_KEEP_ALIVE_TIMEOUT_MILLISECONDS
 * @since 3.0b-5
 */
CERVER_EXPORT void http_cerver_set_keep_alive_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int keep_alive_timeout
);

/**
 * @brief Sets the most requests that a connection handles: the response of the last one has
 * "Connection: close" and the connection is closed after it.
 *
 * Behind a proxy that keeps its connections it has to be more than the requests that the proxy sends through
 * one of them.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] keep_alive_requests the requests, 0 disables the limit; the default is
 * #HTTP_CERVER_DEFAULT_KEEP_ALIVE_REQUESTS
 * @since 3.0b-5
 */
CERVER_EXPORT void http_cerver_set_keep_alive_requests (
	HttpCerver *http_cerver, const unsigned int keep_alive_requests
);

/**
 * @brief Sets whether http_receive_is_client_gone () takes a client that closed its side of the connection
 * (HTTP_CLIENT_STATE_CLOSED) as gone.
 *
 * A client that uses a half-close to end its request still waits for the response.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] half_close_is_gone true to take it as gone; the default is #HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE
 */
CERVER_EXPORT void http_cerver_set_half_close_is_gone (
	HttpCerver *http_cerver, const bool half_close_is_gone
);

/**
 * @brief Sets the lenient flags of the parser of every request.
 *
 * Each flag accepts requests that the strict mode answers with 400; a warning is logged for each flag that is
 * enabled.
 *
 * @param[in,out] http_cerver the HttpCerver
 * @param[in] lenient the HttpParserLenient flags combined with |, the values out of #HTTP_PARSER_LENIENT_ALL are
 * dropped; the default is HTTP_PARSER_LENIENT_NONE (the strict mode)
 * @warning Each flag can open the cerver to request smuggling or cache poisoning attacks.
 */
CERVER_EXPORT void http_cerver_set_parser_lenient (
	HttpCerver *http_cerver, const unsigned int lenient
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
