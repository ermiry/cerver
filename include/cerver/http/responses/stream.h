#ifndef _CERVER_HTTP_STREAM_H_
#define _CERVER_HTTP_STREAM_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

// the header of a chunk, its length in hex and "\r\n"
#define HTTP_STREAM_CHUNK_HEADER_SIZE		32

// the parts of a chunk: its header, its data and "\r\n"
#define HTTP_STREAM_CHUNK_PARTS				3

// the value of the Transfer-Encoding of a stream
#define HTTP_STREAM_CHUNKED_ENCODING		"chunked"

// the last chunk of a stream, without trailers
#define HTTP_STREAM_LAST_CHUNK				"0\r\n\r\n"

#ifdef __cplusplus
extern "C" {
#endif

struct _HttpReceive;
struct _HttpResponse;

typedef enum HttpStreamState {

	HTTP_STREAM_STATE_NONE			= 0,
	HTTP_STREAM_STATE_STARTED		= 1,
	HTTP_STREAM_STATE_ENDED			= 2,

	// a piece could not be sent, the client is gone and nothing else is sent
	HTTP_STREAM_STATE_FAILED		= 3

} HttpStreamState;

// a response that is sent in pieces, with Transfer-Encoding: chunked to an HTTP/1.1 client,
// or delimited by closing the connection to an HTTP/1.0 one
typedef struct HttpStream {

	HttpStreamState state;
	bool chunked;

} HttpStream;

// sends the status and the headers of the response (it must not have a Content-Length),
// its body is sent with http_response_stream_write ()
// the response to a HEAD request only sends its header
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_stream_begin (
	const struct _HttpReceive *http_receive, struct _HttpResponse *response
);

// sends a piece of the body of a response started with http_response_stream_begin (),
// an empty piece is not sent, because it would end a chunked body
// a piece that fails ends the stream (HTTP_STREAM_STATE_FAILED): the client is gone,
// so the next pieces fail without being sent and the handler should stop producing them
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_stream_write (
	const struct _HttpReceive *http_receive,
	const void *data, const size_t length
);

// ends the body of the response, cerver ends a stream that its handler did not end
// returns 0 on success, 1 on error
CERVER_EXPORT u8 http_response_stream_end (
	const struct _HttpReceive *http_receive
);

// returns true if the response is being sent with a stream
CERVER_EXPORT bool http_response_stream_is_active (
	const struct _HttpReceive *http_receive
);

#ifdef __cplusplus
}
#endif

#endif
