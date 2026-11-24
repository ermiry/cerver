/**
 * @file cerver/http/responses/response.h
 * @brief The HTTP responses: their headers, their bodies, how they are compiled and sent, and the helpers that
 * render text, json, files and videos.
 */

#ifndef _CERVER_HTTP_RESPONSE_H_
#define _CERVER_HTTP_RESPONSE_H_

#include <sys/stat.h>
#include <sys/uio.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/handler/handler.h"

#include "cerver/http/core/utils.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/requests/content.h"
#include "cerver/http/responses/status.h"
#include "cerver/http/security/origin.h"

/**
 * @defgroup http_responses Responses
 * @ingroup http
 * @brief The responses of an HTTP cerver.
 */

/**
 * @defgroup http_responses_defaults Values
 * @ingroup http_responses
 * @brief The sizes, the options and the fixed responses.
 * @{
 */

/** @brief The responses that the pool of the responses starts with. */
#define HTTP_RESPONSE_POOL_INIT					32

/** @brief The size of the status line and the Server header of a response. */
#define HTTP_RESPONSE_MAIN_HEADER_SIZE			128

/**
 * @brief The size of the buffer of a response where its header is written when it fits.
 *
 * The main header with the type and the length of its body take less than 2^8.
 */
#define HTTP_RESPONSE_HEADER_BUFFER_SIZE		(1 << 8)

/**
 * @brief The value of the Connection header of a response after which cerver closes the connection.
 *
 * A connection that cerver closes must say so (RFC 9112 9.6), otherwise a client (like nginx) may reuse it and its
 * next request gets reset when cerver closes the socket; the header is added when the response is sent, as it
 * depends on the connection.
 */
#define HTTP_RESPONSE_CONNECTION_CLOSE			"close"

/** @brief The Connection header of #HTTP_RESPONSE_CONNECTION_CLOSE, with its line end. */
#define HTTP_RESPONSE_CONNECTION_CLOSE_HEADER	"Connection: " HTTP_RESPONSE_CONNECTION_CLOSE "\r\n"

/**
 * @brief The value of the Connection header of a response to an HTTP/1.0 client whose connection cerver keeps for
 * its next request; an HTTP/1.1 client keeps its connection without the header.
 *
 * @since 2.1b-16
 */
#define HTTP_RESPONSE_CONNECTION_KEEP_ALIVE		"keep-alive"

/** @brief The size of the digits of the biggest length of a body. */
#define HTTP_RESPONSE_CONTENT_LENGTH_SIZE		32

/** @brief The size of the longest value of a header that is written with a format. */
#define HTTP_RESPONSE_HEADER_FORMAT_SIZE		(1 << 10)

/** @brief A response is sent as its header and its data, in a single system call. */
#define HTTP_RESPONSE_PARTS						2

/**
 * @brief A response is sent as its header (without the empty line that ends it), the headers that cerver adds for
 * the request, the empty line and its data.
 */
#define HTTP_RESPONSE_MESSAGE_PARTS				4

/** @brief An option of http_response_send_message (): the entity tag and the compression of the endpoint apply. */
#define HTTP_RESPONSE_SEND_OPTION_TRANSFORM		(1 << 0)

/** @brief An option of http_response_send_message (): the header starts a stream. */
#define HTTP_RESPONSE_SEND_OPTION_STREAM		(1 << 1)

/** @brief An option of http_response_send_message (): only the header is sent, its body goes after it (a file). */
#define HTTP_RESPONSE_SEND_OPTION_HEADER_ONLY	(1 << 2)

/** @brief The size of the pieces in which a file is sent. */
#define HTTP_RESPONSE_FILE_CHUNK_SIZE			2097152

/** @brief The size of the piece of a video that is sent when the range does not say it. */
#define HTTP_RESPONSE_VIDEO_CHUNK_SIZE			2097152

/** @brief The Retry-After of the response that the cerver sends when it can not handle a new connection. */
#define HTTP_RESPONSE_RETRY_AFTER_SECONDS		"1"

/** @brief The body of #HTTP_RESPONSE_SERVICE_UNAVAILABLE. */
#define HTTP_RESPONSE_SERVICE_UNAVAILABLE_BODY	"{\"error\": \"Service unavailable!\"}"

/**
 * @brief The complete response that the cerver sends when it can not handle a new connection (the thpool is full
 * or the cerver is being drained): a 503 that closes the connection.
 */
#define HTTP_RESPONSE_SERVICE_UNAVAILABLE						\
	"HTTP/1.1 503 Service Unavailable\r\n"						\
	"Content-Type: application/json\r\n"						\
	"Content-Length: 33\r\n"									\
	"Retry-After: " HTTP_RESPONSE_RETRY_AFTER_SECONDS "\r\n"	\
	HTTP_RESPONSE_CONNECTION_CLOSE_HEADER						\
	"\r\n"														\
	HTTP_RESPONSE_SERVICE_UNAVAILABLE_BODY

/** @} */

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpCerver;
struct _HttpReceive;

/**
 * @defgroup http_responses_main Response
 * @ingroup http_responses
 * @brief The struct of a response, its headers and its body.
 * @{
 */

/**
 * @brief An HTTP response.
 */
struct _HttpResponse {

	/** @brief The status of the response, HTTP_STATUS_OK by default. */
	http_status status;

	/**
	 * @brief What the response says, without the protocol that sends it: its headers in the order they were added,
	 * a name can be repeated.
	 */
	HttpHeaders headers;

	/**
	 * @brief The status and the headers as HTTP/1.1 sends them, compiled from #headers when the response is sent;
	 * a change of the headers or the status releases it, so it is compiled again.
	 */
	void *header;
	/** @brief The length of #header. */
	size_t header_len;

	/**
	 * @brief The header was set as bytes with http_response_set_header (), so it is sent as it is and #headers are
	 * not in it.
	 */
	bool header_is_raw;

	/**
	 * @brief Where the header is written when it fits, then #header points to it, so a response without custom
	 * headers does not allocate it.
	 */
	char header_buffer[HTTP_RESPONSE_HEADER_BUFFER_SIZE];

	/** @brief The body of the response. */
	void *data;
	/** @brief The length of the body. */
	size_t data_len;
	/** @brief The body is a reference: the response does not free it. */
	bool data_ref;

	/** @brief The compiled response: the header followed by the body, from http_response_compile (). */
	void *res;
	/** @brief The length of #res. */
	size_t res_len;

	/** @brief The next response of the list of released ones that are reused. */
	struct _HttpResponse *recycle_next;

};

/** @brief An HTTP response, see struct _HttpResponse. */
typedef struct _HttpResponse HttpResponse;

/**
 * @brief Takes a released response to reuse it, or allocates a new one, with the default values.
 *
 * @return the response, NULL if it could not be allocated
 * @note For the internal methods of cerver: an application creates a response with http_response_create () or
 * takes one with http_response_get ().
 */
CERVER_PRIVATE void *http_response_new (void);

/**
 * @brief Resets a response to its default values: it releases its headers, its header, its body (unless it is a
 * reference) and its compiled response.
 *
 * @param[in,out] response the response, it must not be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_response_reset (HttpResponse *response);

/**
 * @brief Releases a response and all of its data: it is reset and kept to be reused.
 *
 * @param[in] res_ptr the response, it can be NULL
 */
CERVER_EXPORT void http_response_delete (void *res_ptr);

/**
 * @brief Takes a response from the pool of the responses, ready to be used.
 *
 * @return the response, given back with http_response_return (); NULL if the pool could not produce one, or
 * without the pool, which exists while an HTTP cerver is running (http_responses_init ())
 */
CERVER_EXPORT HttpResponse *http_response_get (void);

/**
 * @brief Resets a response that was taken with http_response_get () and gives it back to the pool.
 *
 * @param[in] response the response, it can be NULL
 * @note Without the pool, or when the pool can not take it, the response is released.
 */
CERVER_EXPORT void http_response_return (HttpResponse *response);

/**
 * @brief Sets the status of a response.
 *
 * A different status releases the compiled header, so it is compiled again.
 *
 * @param[in,out] response the response
 * @param[in] status the status
 */
CERVER_EXPORT void http_response_set_status (
	HttpResponse *response, const http_status status
);

/**
 * @brief Sets the header of a response as the bytes that are sent.
 *
 * It replaces the existing one, and the headers that were added to the response are not sent.
 *
 * @param[in,out] response the response
 * @param[in] header the status line and the headers, with the empty line; the response owns them and frees them
 * when it is deleted; NULL only releases the current header
 * @param[in] header_len the length of the header
 */
CERVER_EXPORT void http_response_set_header (
	HttpResponse *response, const void *header, const size_t header_len
);

/**
 * @brief Sets a header of a response: it replaces the ones that it has with the same type.
 *
 * @param[in,out] response the response
 * @param[in] type the header
 * @param[in] value the value, copied; it can not have a CR, a LF or a NUL
 * @retval 0 the header was set
 * @retval 1 a NULL argument or a value that is not valid
 */
CERVER_EXPORT u8 http_response_header_set (
	HttpResponse *response, const http_header type, const char *value
);

/**
 * @brief Adds a header to a response, after the ones that it has, so a header can be repeated, like Set-Cookie.
 *
 * @param[in,out] response the response
 * @param[in] type the header
 * @param[in] value the value, copied; it can not have a CR, a LF or a NUL
 * @retval 0 the header was added
 * @retval 1 a NULL argument or a value that is not valid
 */
CERVER_EXPORT u8 http_response_header_add (
	HttpResponse *response, const http_header type, const char *value
);

/**
 * @brief Removes the headers of a response with a type.
 *
 * @param[in,out] response the response
 * @param[in] type the header
 * @return the number of headers that were removed, 0 for a NULL response
 */
CERVER_EXPORT unsigned int http_response_header_remove (
	HttpResponse *response, const http_header type
);

/**
 * @brief Returns the number of headers of a response with a type.
 *
 * @param[in] response the response
 * @param[in] type the header
 * @return the number of headers, 0 for a NULL response
 */
CERVER_EXPORT unsigned int http_response_header_count (
	const HttpResponse *response, const http_header type
);

/**
 * @brief Returns the value of a header of a response, the one in a position between the ones with the type.
 *
 * @param[in] response the response
 * @param[in] type the header
 * @param[in] position the position, 0 is the first one that was added
 * @param[out] value_length the length of the value, 0 when it does not have it; it can be NULL
 * @return the value, that does not end with a terminator; NULL if the response does not have it
 */
CERVER_EXPORT const char *http_response_header_get (
	const HttpResponse *response, const http_header type,
	const unsigned int position, size_t *value_length
);

/**
 * @brief Works like http_response_header_set () with the name of any header.
 *
 * @param[in,out] response the response
 * @param[in] name the name, a token that is compared without case
 * @param[in] value the value
 * @retval 0 the header was set
 * @retval 1 a NULL argument, a name that is not a token or a value that is not valid
 */
CERVER_EXPORT u8 http_response_header_set_with_name (
	HttpResponse *response, const char *name, const char *value
);

/**
 * @brief Works like http_response_header_add () with the name of any header.
 *
 * @param[in,out] response the response
 * @param[in] name the name, a token that is compared without case
 * @param[in] value the value
 * @retval 0 the header was added
 * @retval 1 a NULL argument, a name that is not a token or a value that is not valid
 */
CERVER_EXPORT u8 http_response_header_add_with_name (
	HttpResponse *response, const char *name, const char *value
);

/**
 * @brief Works like http_response_header_remove () with the name of any header.
 *
 * @param[in,out] response the response
 * @param[in] name the name
 * @return the number of headers that were removed, 0 for a NULL argument
 */
CERVER_EXPORT unsigned int http_response_header_remove_with_name (
	HttpResponse *response, const char *name
);

/**
 * @brief Works like http_response_header_count () with the name of any header.
 *
 * @param[in] response the response
 * @param[in] name the name
 * @return the number of headers, 0 for a NULL argument
 */
CERVER_EXPORT unsigned int http_response_header_count_with_name (
	const HttpResponse *response, const char *name
);

/**
 * @brief Works like http_response_header_get () with the name of any header.
 *
 * @param[in] response the response
 * @param[in] name the name
 * @param[in] position the position, 0 is the first one that was added
 * @param[out] value_length the length of the value, 0 when it does not have it; it can be NULL
 * @return the value, that does not end with a terminator; NULL if the response does not have it
 */
CERVER_EXPORT const char *http_response_header_get_with_name (
	const HttpResponse *response, const char *name,
	const unsigned int position, size_t *value_length
);

/**
 * @brief Sets a header of a response, like http_response_header_set ().
 *
 * @param[in,out] response the response
 * @param[in] type the header
 * @param[in] actual_header the value
 * @retval 0 the header was set
 * @retval 1 a NULL argument or a value that is not valid
 */
CERVER_EXPORT u8 http_response_add_header (
	HttpResponse *response,
	const http_header type, const char *actual_header
);

/**
 * @brief Works like http_response_add_header () with a value written with a format.
 *
 * @param[in,out] response the response
 * @param[in] type the header
 * @param[in] format the printf () format of the value
 * @param[in] ... the arguments of the format
 * @retval 0 the header was set
 * @retval 1 a NULL argument, a value longer than #HTTP_RESPONSE_HEADER_FORMAT_SIZE - 1 (it is not added cut) or one
 * that is not valid
 */
CERVER_EXPORT u8 http_response_add_custom_header (
	HttpResponse *response,
	const http_header type, const char *format, ...
) CERVER_PRINTF_FORMAT (3, 4);

/**
 * @brief Sets the Content-Type header of a response.
 *
 * @param[in,out] response the response
 * @param[in] type the content type
 * @retval 0 the header was set
 * @retval 1 a NULL response or the header was not set
 */
CERVER_EXPORT u8 http_response_add_content_type_header (
	HttpResponse *response, const ContentType type
);

/**
 * @brief Sets the Content-Length header of a response.
 *
 * @param[in,out] response the response
 * @param[in] length the length of the body
 * @retval 0 the header was set
 * @retval 1 a NULL response or the header was not set
 */
CERVER_EXPORT u8 http_response_add_content_length_header (
	HttpResponse *response, const size_t length
);

/**
 * @brief Sets the Content-Type header to application/json and the Content-Length of a response.
 *
 * @param[in,out] response the response
 * @param[in] json_len the length of the json body
 */
CERVER_EXPORT void http_response_add_json_headers (
	HttpResponse *response, const size_t json_len
);

/**
 * @brief Sets the Access-Control-Allow-Origin header of a response.
 *
 * @param[in,out] response the response
 * @param[in] origin the origin
 * @retval 0 the header was set
 * @retval 1 a NULL argument or the header was not set
 */
CERVER_EXPORT u8 http_response_add_cors_header (
	HttpResponse *response, const char *origin
);

/**
 * @brief Works like http_response_add_cors_header () with an HttpOrigin.
 *
 * @param[in,out] response the response
 * @param[in] origin the origin
 * @retval 0 the header was set
 * @retval 1 a NULL argument or the header was not set
 */
CERVER_EXPORT u8 http_response_add_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
);

/**
 * @brief Works like http_response_add_cors_header (), only if the domain is in the origins whitelist.
 *
 * @param[in,out] response the response
 * @param[in] domain the domain, compared exactly with each origin of the whitelist
 * @retval 0 the header was set
 * @retval 1 a NULL argument, no HTTP cerver, a domain that is not in the whitelist, or the header was not set
 * @note The whitelist is the one of the HTTP cerver of the responses (http_responses_init ()).
 * @warning With more than one HTTP cerver in a process, it is the whitelist of the first one that is alive, not
 * the one of the cerver that sends the response: http_response_add_whitelist_cors_header_from_request () takes the
 * one of the cerver of the request.
 */
CERVER_EXPORT u8 http_response_add_whitelist_cors_header (
	HttpResponse *response, const char *domain
);

/**
 * @brief Works like http_response_add_whitelist_cors_header () with an HttpOrigin.
 *
 * @param[in,out] response the response
 * @param[in] origin the origin
 * @retval 0 the header was set
 * @retval 1 a NULL argument, an origin that is not in the whitelist, or the header was not set
 */
CERVER_EXPORT u8 http_response_add_whitelist_cors_header_from_origin (
	HttpResponse *response, const HttpOrigin *origin
);

/**
 * @brief Sets the Access-Control-Allow-Origin header of a response when the Origin of the request is in the
 * origins whitelist of the HTTP cerver of the request.
 *
 * @param[in] http_receive the receive of the request
 * @param[in,out] response the response
 * @retval 0 the header was set
 * @retval 1 a NULL receive or request, a request without an Origin, an origin that is not in the whitelist, or the
 * header was not set
 */
CERVER_EXPORT u8 http_response_add_whitelist_cors_header_from_request (
	const struct _HttpReceive *http_receive,
	HttpResponse *response
);

/**
 * @brief Sets the Access-Control-Allow-Credentials header of a response to "true", needed when a CORS request has
 * an Authorization header.
 *
 * @param[in,out] response the response
 * @retval 0 the header was set
 * @retval 1 a NULL response or the header was not set
 */
CERVER_EXPORT u8 http_response_add_cors_allow_credentials_header (
	HttpResponse *response
);

/**
 * @brief Sets the Access-Control-Allow-Methods header of a response, needed in the responses to the preflight
 * OPTIONS requests.
 *
 * @param[in,out] response the response
 * @param[in] methods the list of methods, like "GET, HEAD, OPTIONS"
 * @retval 0 the header was set
 * @retval 1 a NULL argument or the header was not set
 */
CERVER_EXPORT u8 http_response_add_cors_allow_methods_header (
	HttpResponse *response, const char *methods
);

/**
 * @brief Sets the header `Content-Range: bytes <start>-<end>/<file size>` of a response.
 *
 * @param[in,out] response the response
 * @param[in] bytes_range the range
 * @retval 0 the header was set
 * @retval 1 a NULL argument or the header was not set
 */
CERVER_PUBLIC u8 http_response_add_content_range_header (
	HttpResponse *response, const BytesRange *bytes_range
);

/**
 * @brief Sets the headers of a piece of a video: Accept-Ranges (bytes), Content-Type, Content-Length (the chunk
 * size of the range) and Content-Range.
 *
 * @param[in,out] response the response
 * @param[in] content_type the content type of the video
 * @param[in] bytes_range the range, it must not be NULL
 */
CERVER_PUBLIC void http_response_add_video_headers (
	HttpResponse *response,
	const ContentType content_type, const BytesRange *bytes_range
);

/**
 * @brief Sets the body of a response; it replaces the existing one.
 *
 * The body that the response owned is freed, a reference is not.
 *
 * @param[in,out] response the response
 * @param[in] data the body, allocated with malloc (); the response owns it and frees it when it is deleted; NULL
 * leaves the response without a body
 * @param[in] data_len the length of the body
 */
CERVER_EXPORT void http_response_set_data (
	HttpResponse *response, const void *data, const size_t data_len
);

/**
 * @brief Sets a reference to the body of a response: it is not copied and it is not freed, like
 * packet_set_data_ref ().
 *
 * @param[in,out] response the response
 * @param[in] data the body, that must live until the response is sent
 * @param[in] data_size the length of the body
 * @retval 0 the reference was set
 * @retval 1 a NULL argument
 */
CERVER_EXPORT u8 http_response_set_data_ref (
	HttpResponse *response, const void *data, const size_t data_size
);

/**
 * @brief Creates a response with a status and a copy of a body.
 *
 * @param[in] status the status
 * @param[in] data the body, copied, so the original can be released; NULL for a response without a body
 * @param[in] data_len the length of the body
 * @return the new response, released with http_response_delete (); NULL if it could not be allocated
 */
CERVER_EXPORT HttpResponse *http_response_create (
	const http_status status, const void *data, size_t data_len
);

/**
 * @brief Compiles the status and the headers of a response into its header, as HTTP/1.1 sends them, with the
 * default headers of the HTTP cerver that the response does not have.
 *
 * @param[in,out] response the response, it must not be NULL
 * @warning A response compiled before it is sent has the default headers of the HTTP cerver of the responses (the
 * first one that is alive in the process), not the ones of the cerver that sends it. A response that
 * http_response_send () or http_response_stream_begin () compiles takes the ones of the cerver of its request.
 */
CERVER_PUBLIC void http_response_compile_header (
	HttpResponse *response
);

/**
 * @brief Compiles the header (if it is not compiled) and the body of a response into a continuous buffer, its
 * field res.
 *
 * @param[in,out] response the response
 * @retval 0 the response was compiled
 * @retval 1 a NULL response or the buffer could not be allocated
 * @warning Its header takes the default headers of the HTTP cerver of the responses, like
 * http_response_compile_header (): with more than one HTTP cerver in a process, send the response without
 * compiling it, so it takes the ones of the cerver of its request.
 */
CERVER_EXPORT u8 http_response_compile (
	HttpResponse *response
);

/**
 * @brief Prints the compiled response, if it was compiled.
 *
 * @param[in] response the response
 */
CERVER_PUBLIC void http_response_print (
	const HttpResponse *response
);

/** @} */

/**
 * @defgroup http_responses_send Send
 * @ingroup http_responses
 * @brief Sending a response to the connection of a request.
 *
 * Every response that cerver sends marks the request as answered, even if the client is already gone.
 * @{
 */

/**
 * @brief Sends a complete header and its data, with the headers that cerver adds for the request (Connection,
 * Date, X-Request-Id, CORS, Vary), and with the entity tag and the compression of the endpoint when the options have
 * #HTTP_RESPONSE_SEND_OPTION_TRANSFORM.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status of the response
 * @param[in] headers the headers that the header was compiled from, NULL when only its bytes are known
 * @param[in] header the header, it ends with an empty line; one that does not is sent as it is
 * @param[in] header_len the length of the header
 * @param[in] data the body
 * @param[in] data_len the length of the body
 * @param[in] options the HTTP_RESPONSE_SEND_OPTION_* flags
 * @retval 0 the response was sent
 * @retval 1 it was not sent
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_send_message (
	const struct _HttpReceive *http_receive, const http_status status,
	const HttpHeaders *headers,
	const void *header, const size_t header_len,
	const void *data, const size_t data_len,
	const unsigned int options
);

/**
 * @brief Answers with 304 Not Modified, with the headers of the response that it replaces.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] header the header of the response that is replaced
 * @param[in] header_len the length of the header
 * @retval 0 the response was sent
 * @retval 1 it was not sent
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_send_not_modified_header (
	const struct _HttpReceive *http_receive,
	const void *header, const size_t header_len
);

/**
 * @brief Sends the parts with a single system call and adds them to the sent bytes.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] parts the parts
 * @param[in] n_parts the number of parts
 * @retval 0 the parts were sent
 * @retval 1 they were not sent
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_send_pieces (
	const struct _HttpReceive *http_receive, struct iovec *parts, const int n_parts
);

/**
 * @brief Sends a response to the connection of a request.
 *
 * A compiled response is sent as it is; otherwise its header is compiled (if it is not) and sent with its body.
 *
 * @param[in,out] response the response, it still belongs to the caller
 * @param[in] http_receive the receive of the request
 * @retval 0 the response was sent
 * @retval 1 a NULL argument, a receive without a connection, or it was not sent
 */
CERVER_EXPORT u8 http_response_send (
	HttpResponse *response,
	const struct _HttpReceive *http_receive
);

/**
 * @brief Sends the compiled header and the body of a response without the continuous buffer of
 * http_response_compile ().
 *
 * @param[in] response the response, with its header compiled and a body
 * @param[in] http_receive the receive of the request
 * @retval 0 the response was sent
 * @retval 1 a NULL argument, a receive without a connection, a response without a header or without a body, or it
 * was not sent
 */
CERVER_EXPORT u8 http_response_send_split (
	HttpResponse *response,
	const struct _HttpReceive *http_receive
);

/**
 * @brief Creates, compiles, sends and releases a response with a status and a copy of a body.
 *
 * @param[in] status the status
 * @param[in] data the body, copied
 * @param[in] data_len the length of the body
 * @param[in] http_receive the receive of the request
 * @retval 0 the response was sent
 * @retval 1 it could not be created, compiled or sent
 */
CERVER_EXPORT u8 http_response_create_and_send (
	const http_status status, const void *data, size_t data_len,
	const struct _HttpReceive *http_receive
);

/**
 * @brief Sends a file whose status is already known, with its Content-Type, its Content-Length and its validators
 * (ETag, Last-Modified).
 *
 * A 200 to a request whose validators match is answered with 304 Not Modified; a HEAD request only gets the header.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] filename the path of the file
 * @param[in] filestatus the status of the file
 * @retval 0 the file was sent
 * @retval 1 it was not sent
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_send_file_internal (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *filename,
	struct stat *filestatus
);

/**
 * @brief Opens a file and sends it to the client, with the header generated from the file (see
 * http_response_send_file_internal ()).
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] filename the path of the file
 * @retval 0 the file was sent
 * @retval 1 the file was not found or it was not sent
 */
CERVER_EXPORT u8 http_response_send_file (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *filename
);

/**
 * @brief Works like http_response_send_file () with a path written with a format.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] format the printf () format of the path, truncated to FILENAME_DEFAULT_SIZE - 1 characters
 * @param[in] ... the arguments of the format
 * @retval 0 the file was sent
 * @retval 1 the file was not found or it was not sent
 */
CERVER_EXPORT u8 http_response_send_file_generate (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *format, ...
) CERVER_PRINTF_FORMAT (3, 4);

/** @} */

/**
 * @defgroup http_responses_render Render
 * @ingroup http_responses
 * @brief Sending a text or a json as the body of a response.
 * @{
 */

/**
 * @brief Sends a text with the text/html content type.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] text the text, it is not copied
 * @param[in] text_len the length of the text
 * @retval 0 the response was sent
 * @retval 1 a NULL argument, a receive without a connection, or it was not sent
 */
CERVER_EXPORT u8 http_response_render_text (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *text, const size_t text_len
);

/**
 * @brief Sends a json with the application/json content type.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] json the json, it is not copied
 * @param[in] json_len the length of the json
 * @retval 0 the response was sent
 * @retval 1 a NULL argument, a receive without a connection, or it was not sent
 */
CERVER_EXPORT u8 http_response_render_json (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
);

/** @} */

/**
 * @defgroup http_responses_videos Videos
 * @ingroup http_responses
 * @brief Sending a piece of a video with 206 Partial Content.
 * @{
 */

/**
 * @brief Sends a video with the Range of the request, as RFC 9110 says.
 *
 * Without a Range (or with an invalid one, like `bytes=9-1`) the whole video goes with 200; a range that starts
 * inside the video goes with 206 Partial Content and its Content-Range: `bytes=N-M` includes M and ends with the
 * video, a range without an end (`bytes=N-`) sends at most #HTTP_RESPONSE_VIDEO_CHUNK_SIZE bytes, and `bytes=-N` the
 * last N; a range that starts after the end of the video gets 416 with a Content-Range of the unsatisfied range
 * (`bytes`, an asterisk, a slash and the size of the video). The content type comes from the extension of the file
 * (MP4 without one).
 *
 * @param[in] http_receive the receive of the request
 * @param[in] filename the path of the video
 * @retval 0 the response was sent
 * @retval 1 the video was not found, it could not be opened, or the response could not be created or sent
 * @since 2.1b-18 (the 200 and the 416)
 */
CERVER_EXPORT u8 http_response_handle_video (
	const struct _HttpReceive *http_receive,
	const char *filename
);

/**
 * @brief Works like http_response_handle_video () with a path written with a format.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] format the printf () format of the path, truncated to FILENAME_DEFAULT_SIZE - 1 characters
 * @param[in] ... the arguments of the format
 * @retval 0 the piece was sent
 * @retval 1 the video was not found, it could not be opened or the piece was not sent
 */
CERVER_EXPORT u8 http_response_handle_video_generate (
	const struct _HttpReceive *http_receive,
	const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/** @} */

/**
 * @defgroup http_responses_json Json
 * @ingroup http_responses
 * @brief The responses with a json body.
 *
 * The keys and the values are escaped as json; a value that is not UTF-8, or a real that is NaN, gives no response.
 * The responses that are created are released with http_response_delete ().
 * @{
 */

/**
 * @brief Creates a compiled response with a json body that is a reference: it is not copied and it is not freed.
 *
 * @param[in] status the status
 * @param[in] json the json, that must live until the response is sent
 * @param[in] json_len the length of the json
 * @return the new response, NULL for a NULL json or if it could not be allocated
 */
CERVER_EXPORT HttpResponse *http_response_create_json (
	const http_status status, const char *json, const size_t json_len
);

/**
 * @brief Creates a compiled response with the body `{"key": "value"}`.
 *
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value, a string
 * @return the new response, NULL for a NULL argument or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_create_json_key_value (
	const http_status status, const char *key, const char *value
);

/**
 * @brief Creates a compiled response with the body `{"key": value}`, with an int.
 *
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @return the new response, NULL for a NULL key or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_int_value (
	const http_status status, const char *key, const int value
);

/**
 * @brief Creates, sends and releases a response with the body `{"key": value}`, with an int.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_int_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const int value
);

/**
 * @brief Creates a compiled response with the body `{"key": value}`, with a long.
 *
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @return the new response, NULL for a NULL key or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_large_int_value (
	const http_status status, const char *key, const long value
);

/**
 * @brief Creates, sends and releases a response with the body `{"key": value}`, with a long.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_large_int_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const long value
);

/**
 * @brief Creates a compiled response with the body `{"key": value}`, with a double.
 *
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value, written without the locale
 * @return the new response, NULL for a NULL key, a NaN or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_real_value (
	const http_status status, const char *key, const double value
);

/**
 * @brief Creates, sends and releases a response with the body `{"key": value}`, with a double.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_real_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const double value
);

/**
 * @brief Creates a compiled response with the body `{"key": true}` or `{"key": false}`.
 *
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @return the new response, NULL for a NULL key or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_bool_value (
	const http_status status, const char *key, const bool value
);

/**
 * @brief Creates, sends and releases a response with the body `{"key": true}` or `{"key": false}`.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_bool_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const bool value
);

/**
 * @brief Creates a compiled response with the body `{"msg": "message"}`.
 *
 * @param[in] status the status
 * @param[in] msg the message
 * @return the new response, NULL for a NULL message or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_msg (
	const http_status status, const char *msg
);

/**
 * @brief Creates, sends and releases a response with the body `{"msg": "message"}`.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] msg the message
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_msg_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *msg
);

/**
 * @brief Creates a compiled response with the body `{"error": "message"}`.
 *
 * @param[in] status the status
 * @param[in] error_msg the message of the error
 * @return the new response, NULL for a NULL message or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_error (
	const http_status status, const char *error_msg
);

/**
 * @brief Creates, sends and releases a response with the body `{"error": "message"}`.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] error_msg the message of the error
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_error_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *error_msg
);

/**
 * @brief Creates a compiled response with the body `{"key": "value"}`, like
 * http_response_create_json_key_value ().
 *
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @return the new response, NULL for a NULL argument or if the json could not be created
 */
CERVER_EXPORT HttpResponse *http_response_json_key_value (
	const http_status status, const char *key, const char *value
);

/**
 * @brief Creates, sends and releases a response with the body `{"key": "value"}`.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] key the key
 * @param[in] value the value
 * @retval 0 the response was sent
 * @retval 1 it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_key_value_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *key, const char *value
);

/**
 * @brief Creates a compiled response with a copy of a json as its body.
 *
 * @param[in] status the status
 * @param[in] json the json, a string that is copied
 * @return the new response, NULL for a NULL json or if it could not be allocated
 */
CERVER_EXPORT HttpResponse *http_response_json_custom (
	const http_status status, const char *json
);

/**
 * @brief Creates, sends and releases a response with a copy of a json as its body.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] json the json, a string that is copied
 * @retval 0 the response was sent
 * @retval 1 a NULL argument, or it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_custom_send (
	const struct _HttpReceive *http_receive,
	const http_status status, const char *json
);

/**
 * @brief Creates a compiled response with a reference to a json as its body: it is not copied and it is not freed.
 *
 * @param[in] status the status
 * @param[in] json the json, that must live until the response is sent
 * @param[in] json_len the length of the json
 * @return the new response, NULL for a NULL json or if it could not be allocated
 */
CERVER_EXPORT HttpResponse *http_response_json_custom_reference (
	const http_status status,
	const char *json, const size_t json_len
);

/**
 * @brief Creates, sends and releases a response with a reference to a json as its body.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] status the status
 * @param[in] json the json, it is not copied
 * @param[in] json_len the length of the json
 * @retval 0 the response was sent
 * @retval 1 a NULL argument, or it could not be created or sent
 */
CERVER_EXPORT u8 http_response_json_custom_reference_send (
	const struct _HttpReceive *http_receive,
	const http_status status,
	const char *json, const size_t json_len
);

/** @} */

/**
 * @defgroup http_responses_producer Producer
 * @ingroup http_responses
 * @brief The producer of the responses that the HTTP cervers of the process share.
 * @{
 */

/**
 * @brief Creates the producer of the responses and its pool.
 *
 * The producer is shared by every HTTP cerver of the process: the origins whitelist and the default headers of its
 * compiled responses are the ones of the first HTTP cerver that started.
 *
 * @param[in] http_cerver the HTTP cerver
 * @retval 0 the producer was created
 * @retval 1 it could not be created
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE unsigned int http_responses_init (
	const struct _HttpCerver *http_cerver
);

/**
 * @brief Moves the producer to a cerver that is still alive when its own one ends.
 *
 * @param[in] ended the HTTP cerver that ended
 * @param[in] alive an HTTP cerver that is still alive
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_responses_follow (
	const struct _HttpCerver *ended, const struct _HttpCerver *alive
);

/**
 * @brief Releases the producer of the responses and its pool.
 *
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_responses_end (void);

/**
 * @brief Sends #HTTP_RESPONSE_SERVICE_UNAVAILABLE without blocking to a connection that the cerver can not handle
 * (the thpool is full or the cerver is being drained); the socket is not closed.
 *
 * @param[in] sock_fd the socket of the connection
 * @retval 0 the whole response was sent
 * @retval 1 a negative socket, or it was not sent completely
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE u8 http_response_send_service_unavailable (
	const int sock_fd
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
