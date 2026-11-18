/**
 * @internal
 * @file cerver/http/responses/stream.h
 * @brief The responses that are sent in pieces: chunked to an HTTP/1.1 client, delimited by the close to an
 * HTTP/1.0 one.
 */

#ifndef _CERVER_HTTP_STREAM_H_
#define _CERVER_HTTP_STREAM_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

/**
 * @defgroup http_responses_stream Stream
 * @ingroup http_responses
 * @brief Sending the body of a response while the handler produces it.
 * @{
 */

/** @brief The size of the header of a chunk: its length in hex and "\r\n". */
#define HTTP_STREAM_CHUNK_HEADER_SIZE		32

/** @brief The parts of a chunk: its header, its data and "\r\n". */
#define HTTP_STREAM_CHUNK_PARTS				3

/** @brief The value of the Transfer-Encoding of a stream. */
#define HTTP_STREAM_CHUNKED_ENCODING		"chunked"

/** @brief The last chunk of a stream, without trailers. */
#define HTTP_STREAM_LAST_CHUNK				"0\r\n\r\n"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;
struct _HttpResponse;

/**
 * @brief The state of the stream of a response.
 */
typedef enum HttpStreamState {

	/** @brief The response is not sent with a stream. */
	HTTP_STREAM_STATE_NONE			= 0,
	/** @brief The header was sent, the pieces of the body can be sent. */
	HTTP_STREAM_STATE_STARTED		= 1,
	/** @brief The body was ended. */
	HTTP_STREAM_STATE_ENDED			= 2,

	/** @brief A piece could not be sent: the client is gone and nothing else is sent. */
	HTTP_STREAM_STATE_FAILED		= 3

} HttpStreamState;

/**
 * @brief The stream of a response: with Transfer-Encoding: chunked to an HTTP/1.1 client, or delimited by closing
 * the connection to an HTTP/1.0 one.
 */
typedef struct HttpStream {

	/** @brief The state of the stream. */
	HttpStreamState state;
	/** @brief The body is sent with Transfer-Encoding: chunked (an HTTP/1.1 or newer request). */
	bool chunked;

} HttpStream;

/**
 * @brief Sends the status and the headers of a response; its body is sent with http_response_stream_write ().
 *
 * The header is compiled if it was not; the response to a HEAD request only sends its header.
 *
 * @param[in] http_receive the receive of the request
 * @param[in,out] response the response, it must not have a Content-Length
 * @retval 0 the header was sent
 * @retval 1 a NULL argument, a receive without a connection, a stream that was already started, a response with a
 * Content-Length, or the header was not sent (then the stream is HTTP_STREAM_STATE_FAILED)
 */
CERVER_EXPORT u8 http_response_stream_begin (
	const struct _HttpReceive *http_receive, struct _HttpResponse *response
);

/**
 * @brief Sends a piece of the body of a response started with http_response_stream_begin ().
 *
 * An empty piece is not sent, because it would end a chunked body; the pieces of a response to a HEAD request are
 * not sent either.
 *
 * @param[in] http_receive the receive of the request
 * @param[in] data the piece
 * @param[in] length the length of the piece
 * @retval 0 the piece was sent, or it was empty or for a HEAD request
 * @retval 1 the stream was not started, it failed before, or the piece was not sent
 * @warning A piece that fails ends the stream (HTTP_STREAM_STATE_FAILED): the client is gone, so the next pieces
 * fail without being sent and the handler should stop producing them.
 */
CERVER_EXPORT u8 http_response_stream_write (
	const struct _HttpReceive *http_receive,
	const void *data, const size_t length
);

/**
 * @brief Ends the body of a response: it sends the last chunk of a chunked stream.
 *
 * cerver ends a stream that its handler did not end.
 *
 * @param[in] http_receive the receive of the request
 * @retval 0 the stream was ended, or it was already ended
 * @retval 1 a NULL receive, a stream that was not started or that failed, or the last chunk was not sent
 */
CERVER_EXPORT u8 http_response_stream_end (
	const struct _HttpReceive *http_receive
);

/**
 * @brief Returns whether the response is being sent with a stream.
 *
 * @param[in] http_receive the receive of the request
 * @return true while the stream is HTTP_STREAM_STATE_STARTED
 */
CERVER_EXPORT bool http_response_stream_is_active (
	const struct _HttpReceive *http_receive
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
