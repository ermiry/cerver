#ifndef _CERVER_HTTP_WEBSOCKETS_DEFLATE_H_
#define _CERVER_HTTP_WEBSOCKETS_DEFLATE_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/websockets/frame.h"

// the name of the extension (RFC 7692)
#define HTTP_WEB_SOCKET_DEFLATE_NAME					"permessage-deflate"

// the range of the LZ77 window of the extension, 2^8 to 2^15 bytes
#define HTTP_WEB_SOCKET_DEFLATE_MIN_WINDOW_BITS			8
#define HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS			15

// zlib does not compress raw deflate with a window of 2^8 (it takes 2^9),
// so a peer that asks for it to the compressor of cerver is declined
#define HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS	9

// the defaults of the compression of cerver
#define HTTP_WEB_SOCKET_DEFLATE_DEFAULT_LEVEL			6
#define HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MEMORY_LEVEL	8

// messages smaller than this are sent without compression
#define HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MIN_SIZE		(1 << 7)

// the bytes of a header value with every parameter of the extension
#define HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE				(1 << 8)

#ifdef __cplusplus
extern "C" {
#endif

// how cerver wants to use the extension, in the server or in the client
typedef struct HttpWebSocketDeflateOptions {

	// it is only offered or accepted when it is enabled and cerver was built with zlib
	bool enabled;

	// the compression level of zlib, 0 to 9
	int level;

	// the memory level of zlib, 1 to 9
	int memory_level;

	// the most window bits that the compressor of cerver uses
	u8 window_bits;

	// asks the peer to reset its compressor after each message,
	// and resets the one of cerver too, so both sides keep less memory
	bool no_context_takeover;

	// messages smaller than this are sent without compression
	size_t min_size;

} HttpWebSocketDeflateOptions;

// the default options, disabled
CERVER_PUBLIC void http_web_socket_deflate_options_init (
	HttpWebSocketDeflateOptions *options
);

// returns true when cerver was built with zlib (ZLIB=1)
CERVER_PUBLIC bool http_web_socket_deflate_available (void);

// the parameters that were agreed for a connection
typedef struct HttpWebSocketDeflateParameters {

	bool server_no_context_takeover;
	bool client_no_context_takeover;

	// the window bits of each compressor, 15 when they were not limited
	u8 server_max_window_bits;
	u8 client_max_window_bits;

	// the response says client_max_window_bits (only when the offer had it)
	bool client_max_window_bits_set;

	// the response says server_max_window_bits
	bool server_max_window_bits_set;

} HttpWebSocketDeflateParameters;

// the server takes the first permessage-deflate offer of the Sec-WebSocket-Extensions value
// that is valid and that it can use, the others are declined
// returns true when one was accepted, and sets its parameters
CERVER_PUBLIC bool http_web_socket_deflate_negotiate (
	const char *offers, const size_t length,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
);

// writes the value of the Sec-WebSocket-Extensions header of the response
// returns its length, 0 when it does not fit
CERVER_PUBLIC size_t http_web_socket_deflate_response (
	const HttpWebSocketDeflateParameters *parameters,
	char *value, const size_t size
);

// writes the offer of a client
// returns its length, 0 when it does not fit
CERVER_PUBLIC size_t http_web_socket_deflate_offer (
	const HttpWebSocketDeflateOptions *options,
	char *value, const size_t size
);

// the client validates the Sec-WebSocket-Extensions value of the response to its offer
// returns true when it is valid, and sets the parameters
CERVER_PUBLIC bool http_web_socket_deflate_accept (
	const char *value, const size_t length,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
);

// the compressor and the decompressor of a connection
typedef struct HttpWebSocketDeflate {

	HttpWebSocketRole role;

	HttpWebSocketDeflateParameters parameters;

	int level;
	int memory_level;
	size_t min_size;

	// zlib streams, NULL until they are first used
	void *compressor;
	void *decompressor;

	// the output of the compressor, valid until its next call
	u8 *output;
	size_t output_size;
	size_t output_capacity;

	// the output of the decompressor before it is given away
	u8 *chunk;
	size_t chunk_size;

	// the message had a last block (BFINAL), nothing else of it can follow
	bool ended;

} HttpWebSocketDeflate;

// returns NULL without zlib
CERVER_PUBLIC HttpWebSocketDeflate *http_web_socket_deflate_create (
	const HttpWebSocketRole role,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
);

CERVER_PUBLIC void http_web_socket_deflate_delete (HttpWebSocketDeflate *deflate);

// compresses the next bytes of a message, end is true with its last bytes,
// the output is valid until the next call, and without the tail 00 00 FF FF at the end
// returns 0 on success, 1 on error
CERVER_PUBLIC u8 http_web_socket_deflate_compress (
	HttpWebSocketDeflate *deflate,
	const void *data, const size_t size, const bool end,
	const u8 **output, size_t *output_size
);

// receives the decompressed bytes, returns 0 to continue
typedef int (*HttpWebSocketInflateSink)(void *data, const u8 *bytes, size_t size);

// decompresses the next bytes of a message, end is true with its last bytes,
// every decompressed piece goes to the sink, that can stop it
// returns 0 on success, 1 on a stream that is not valid, 2 when the sink stopped it
CERVER_PUBLIC u8 http_web_socket_deflate_decompress (
	HttpWebSocketDeflate *deflate,
	const u8 *data, const size_t size, const bool end,
	HttpWebSocketInflateSink sink, void *sink_data
);

#ifdef __cplusplus
}
#endif

#endif
