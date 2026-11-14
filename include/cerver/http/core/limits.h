#ifndef _CERVER_HTTP_CORE_LIMITS_H_
#define _CERVER_HTTP_CORE_LIMITS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region limits

// sets the max size in bytes of a request's body
// a bigger request is answered with 413
// the default value is HTTP_CERVER_DEFAULT_MAX_BODY_SIZE, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_body_size (
	HttpCerver *http_cerver, const size_t max_body_size
);

// sets the max size in bytes of a request's url (path and query)
// a bigger url is answered with 414
// the default value is HTTP_CERVER_DEFAULT_MAX_URL_SIZE,
// the limit is always active, 0 sets the default value
CERVER_EXPORT void http_cerver_set_max_url_size (
	HttpCerver *http_cerver, const size_t max_url_size
);

// sets the max size in bytes of all of a request's headers
// bigger headers are answered with 431
// the default value is HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE,
// the limit is always active (the parser does not have one of its own), 0 sets the default value
CERVER_EXPORT void http_cerver_set_max_header_size (
	HttpCerver *http_cerver, const size_t max_header_size
);

// sets the max number of headers of a request
// more headers are answered with 431
// the default value is HTTP_CERVER_DEFAULT_MAX_HEADERS, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_headers (
	HttpCerver *http_cerver, const unsigned int max_headers
);

// sets the max size in bytes of the value of a header that is not one of the HTTP headers map
// (a custom header, like the ones of a trace or the ones that a proxy adds),
// with the values of its repetitions joined; a bigger value is answered with 431
// the default value is HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE, 0 disables the limit,
// and the size of all the headers is still limited by http_cerver_set_max_header_size ()
CERVER_EXPORT void http_cerver_set_max_custom_header_size (
	HttpCerver *http_cerver, const size_t max_custom_header_size
);

// sets the max number of parts of a multi-part request
// more parts are answered with 413
// the default value is HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_multipart_parts (
	HttpCerver *http_cerver, const unsigned int max_multipart_parts
);

// sets the max size in bytes of each file of a multi-part request
// a bigger file is answered with 413
// the default value is HTTP_CERVER_DEFAULT_MAX_FILE_SIZE, 0 disables the limit
CERVER_EXPORT void http_cerver_set_max_file_size (
	HttpCerver *http_cerver, const size_t max_file_size
);

// sets the secs to receive a request's headers
// a client that is slower (like a slowloris) is answered with 408
// the default value is HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT, 0 disables the timeout
CERVER_EXPORT void http_cerver_set_headers_timeout (
	HttpCerver *http_cerver, const unsigned int headers_timeout
);

// the same as http_cerver_set_headers_timeout (), in milliseconds
CERVER_EXPORT void http_cerver_set_headers_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int headers_timeout
);

// sets the secs to receive a complete request (headers and body)
// a slower request is answered with 408
// while it is disabled, a body only needs to make progress
// before the idle timeout (http_cerver_set_idle_timeout_milliseconds ())
// the default value is HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT, 0 disables the timeout
CERVER_EXPORT void http_cerver_set_request_timeout (
	HttpCerver *http_cerver, const unsigned int request_timeout
);

// the same as http_cerver_set_request_timeout (), in milliseconds
CERVER_EXPORT void http_cerver_set_request_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int request_timeout
);

// sets the most milliseconds that a connection waits without receiving anything,
// a request that had started is answered with 408, an empty connection is only closed
// the default value is HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS, 0 keeps the default
CERVER_EXPORT void http_cerver_set_idle_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int idle_timeout
);

// sets the most milliseconds that all the sends of a response take together,
// headers, body and files, also in a blocking socket with a longer SO_SNDTIMEO,
// so a client that stops reading costs that time in total and not in each send,
// each piece of a stream has its own deadline
// the default value is 0, the send timeout of the cerver (cerver_set_send_timeout (), in secs)
CERVER_EXPORT void http_cerver_set_send_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int send_timeout
);

// sets if http_receive_is_client_gone () takes a client that closed its side
// of the connection (HTTP_CLIENT_STATE_CLOSED) as gone,
// a client that uses a half-close to end its request still waits for the response
// the default value is HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE
CERVER_EXPORT void http_cerver_set_half_close_is_gone (
	HttpCerver *http_cerver, const bool half_close_is_gone
);

// sets the lenient flags (HttpParserLenient, combined with |) of the parser of every request,
// each flag accepts requests that the strict mode answers with 400,
// and it can open the cerver to request smuggling or cache poisoning attacks,
// so a warning is logged for each flag that is enabled
// the default value is HTTP_PARSER_LENIENT_NONE (the strict mode)
CERVER_EXPORT void http_cerver_set_parser_lenient (
	HttpCerver *http_cerver, const unsigned int lenient
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
