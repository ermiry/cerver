/**
 * @file cerver/http/websockets/deflate.h
 * @brief The permessage-deflate extension (RFC 7692): its negotiation and the compression of the messages.
 */

#ifndef _CERVER_HTTP_WEBSOCKETS_DEFLATE_H_
#define _CERVER_HTTP_WEBSOCKETS_DEFLATE_H_

#include <limits.h>
#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#include "cerver/http/websockets/frame.h"

/**
 * @defgroup http_websockets_deflate Compression
 * @ingroup http_websockets
 * @brief permessage-deflate, compiled with zlib (ZLIB=1); without it the extension is never offered nor accepted.
 * @{
 */

/**
 * @brief The most bytes that zlib takes in a call: a larger message is sent without compression, and a larger
 * piece of a stream or of a message that is received is an error.
 */
#define HTTP_WEB_SOCKET_DEFLATE_INPUT_MAX				((size_t) UINT_MAX)

/** @brief The name of the extension (RFC 7692). */
#define HTTP_WEB_SOCKET_DEFLATE_NAME					"permessage-deflate"

/** @brief The smallest LZ77 window of the extension, 2^8 bytes. */
#define HTTP_WEB_SOCKET_DEFLATE_MIN_WINDOW_BITS			8

/** @brief The largest LZ77 window of the extension, 2^15 bytes. */
#define HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS			15

/**
 * @brief The smallest window of the compressor of cerver: zlib does not compress raw deflate with a window of 2^8
 * (it takes 2^9), so a peer that asks for it to the compressor of cerver is declined.
 */
#define HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS	9

/** @brief The default compression level of zlib. */
#define HTTP_WEB_SOCKET_DEFLATE_DEFAULT_LEVEL			6

/** @brief The default memory level of zlib. */
#define HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MEMORY_LEVEL	8

/** @brief The default size of the smallest message that is compressed, a smaller one is sent without compression. */
#define HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MIN_SIZE		(1 << 7)

/** @brief The size of a header value with every parameter of the extension. */
#define HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE				(1 << 8)

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief How cerver wants to use the extension, in the server or in the client.
 */
typedef struct HttpWebSocketDeflateOptions {

	/** @brief The extension is only offered or accepted when it is enabled and cerver was built with zlib. */
	bool enabled;

	/** @brief The compression level of zlib, 0 to 9. */
	int level;

	/** @brief The memory level of zlib, 1 to 9. */
	int memory_level;

	/** @brief The most window bits that the compressor of cerver uses. */
	u8 window_bits;

	/**
	 * @brief Asks the peer to reset its compressor after each message, and resets the one of cerver too, so both
	 * sides keep less memory.
	 */
	bool no_context_takeover;

	/** @brief The messages smaller than this are sent without compression. */
	size_t min_size;

} HttpWebSocketDeflateOptions;

/**
 * @brief Sets the default options: disabled, with the default levels, the largest window and
 * #HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MIN_SIZE.
 *
 * @param[out] options the options, they can be NULL
 */
CERVER_PUBLIC void http_web_socket_deflate_options_init (
	HttpWebSocketDeflateOptions *options
);

/**
 * @brief Returns whether cerver was built with zlib (ZLIB=1).
 *
 * @return true with zlib
 */
CERVER_PUBLIC bool http_web_socket_deflate_available (void);

/**
 * @brief The parameters that were agreed for a connection.
 */
typedef struct HttpWebSocketDeflateParameters {

	/** @brief The server resets its compressor after each message. */
	bool server_no_context_takeover;
	/** @brief The client resets its compressor after each message. */
	bool client_no_context_takeover;

	/** @brief The window bits of the compressor of the server, 15 when they were not limited. */
	u8 server_max_window_bits;
	/** @brief The window bits of the compressor of the client, 15 when they were not limited. */
	u8 client_max_window_bits;

	/** @brief The response says client_max_window_bits (only when the offer had it). */
	bool client_max_window_bits_set;

	/** @brief The response says server_max_window_bits. */
	bool server_max_window_bits_set;

} HttpWebSocketDeflateParameters;

/**
 * @brief The server takes the first permessage-deflate offer of a Sec-WebSocket-Extensions value that is valid and
 * that it can use; the others are declined.
 *
 * @param[in] offers the value of the header of the request
 * @param[in] length the length of the value
 * @param[in] options the options of the server
 * @param[out] parameters the parameters of the offer that was accepted
 * @return true when an offer was accepted; false for a NULL argument, disabled options or without zlib
 */
CERVER_PUBLIC bool http_web_socket_deflate_negotiate (
	const char *offers, const size_t length,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
);

/**
 * @brief Writes the value of the Sec-WebSocket-Extensions header of the response.
 *
 * @param[in] parameters the parameters that were agreed
 * @param[out] value where the value is written
 * @param[in] size the size of value
 * @return the length of the value, 0 for a NULL argument or when it does not fit
 */
CERVER_PUBLIC size_t http_web_socket_deflate_response (
	const HttpWebSocketDeflateParameters *parameters,
	char *value, const size_t size
);

/**
 * @brief Writes the offer of a client.
 *
 * @param[in] options the options of the client
 * @param[out] value where the offer is written
 * @param[in] size the size of value
 * @return the length of the offer, 0 for a NULL argument or when it does not fit
 */
CERVER_PUBLIC size_t http_web_socket_deflate_offer (
	const HttpWebSocketDeflateOptions *options,
	char *value, const size_t size
);

/**
 * @brief The client validates the Sec-WebSocket-Extensions value of the response to its offer: a single
 * permessage-deflate with parameters that it can use.
 *
 * @param[in] value the value of the header of the response
 * @param[in] length the length of the value
 * @param[in] options the options of the client
 * @param[out] parameters the parameters of the response
 * @return true when the response is valid; false for a NULL argument, disabled options or without zlib
 */
CERVER_PUBLIC bool http_web_socket_deflate_accept (
	const char *value, const size_t length,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
);

/**
 * @brief The compressor and the decompressor of a connection.
 */
typedef struct HttpWebSocketDeflate {

	/** @brief The role of this side. */
	HttpWebSocketRole role;

	/** @brief The parameters that were agreed. */
	HttpWebSocketDeflateParameters parameters;

	/** @brief The compression level of zlib. */
	int level;
	/** @brief The memory level of zlib. */
	int memory_level;
	/** @brief The messages smaller than this are sent without compression. */
	size_t min_size;

	/** @brief The zlib stream of the compressor, NULL until it is first used. */
	void *compressor;
	/** @brief The zlib stream of the decompressor, NULL until it is first used. */
	void *decompressor;

	/** @brief The output of the compressor, valid until its next call. */
	u8 *output;
	/** @brief The bytes of the output. */
	size_t output_size;
	/** @brief The size of the buffer of the output. */
	size_t output_capacity;

	/** @brief The output of the decompressor before it is given to the sink. */
	u8 *chunk;
	/** @brief The size of the buffer of the decompressor. */
	size_t chunk_size;

	/**
	 * @brief The last thing that the inflater took was a last block (BFINAL): it started again with its window and
	 * a message can end there without its tail.
	 */
	bool ended;

} HttpWebSocketDeflate;

/**
 * @brief Creates the compressor and the decompressor of a connection; their zlib streams are created when they are
 * first used.
 *
 * @param[in] role the role of this side
 * @param[in] parameters the parameters that were agreed, copied
 * @param[in] options the options, their levels and min size are copied
 * @return the new compression, released with http_web_socket_deflate_delete (); NULL for a NULL argument, without
 * zlib, or if it could not be allocated
 */
CERVER_PUBLIC HttpWebSocketDeflate *http_web_socket_deflate_create (
	const HttpWebSocketRole role,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
);

/**
 * @brief Releases a compression, its zlib streams and its buffers.
 *
 * @param[in] deflate the compression, it can be NULL
 */
CERVER_PUBLIC void http_web_socket_deflate_delete (HttpWebSocketDeflate *deflate);

/**
 * @brief Compresses the next bytes of a message.
 *
 * The output does not have the tail 00 00 FF FF at its end; the compressor is reset after the message when the
 * parameters ask for no context takeover.
 *
 * @param[in,out] deflate the compression
 * @param[in] data the bytes, NULL only when size is 0
 * @param[in] size the number of bytes, up to #HTTP_WEB_SOCKET_DEFLATE_INPUT_MAX
 * @param[in] end true with the last bytes of the message
 * @param[out] output the compressed bytes, valid until the next call
 * @param[out] output_size the number of compressed bytes
 * @retval 0 the bytes were compressed
 * @retval 1 a NULL argument, a size out of range, a failure of zlib, or cerver was built without zlib
 */
CERVER_PUBLIC u8 http_web_socket_deflate_compress (
	HttpWebSocketDeflate *deflate,
	const void *data, const size_t size, const bool end,
	const u8 **output, size_t *output_size
);

/** @brief A result of http_web_socket_deflate_decompress (): the stream is not valid. */
#define HTTP_WEB_SOCKET_INFLATE_INVALID					1

/** @brief A result of http_web_socket_deflate_decompress (): the sink stopped it. */
#define HTTP_WEB_SOCKET_INFLATE_STOPPED					2

/** @brief A result of http_web_socket_deflate_decompress (): the decompressor failed (its memory), it is not an error of the peer. */
#define HTTP_WEB_SOCKET_INFLATE_FAILED					3

/**
 * @brief Receives the decompressed bytes; it returns 0 to continue.
 */
typedef int (*HttpWebSocketInflateSink)(void *data, const u8 *bytes, size_t size);

/**
 * @brief Decompresses the next bytes of a message; every decompressed piece goes to the sink, that can stop it.
 *
 * A block with BFINAL does not end the message (RFC 7692 section 7.2.3.4), other blocks can follow it, and a
 * message that ends inside a block is not valid.
 *
 * @param[in,out] deflate the compression
 * @param[in] data the compressed bytes
 * @param[in] size the number of bytes
 * @param[in] end true with the last bytes of the message
 * @param[in] sink the method that receives the decompressed bytes
 * @param[in] sink_data the data of the sink
 * @retval 0 the bytes were decompressed
 * @retval HTTP_WEB_SOCKET_INFLATE_INVALID the stream is not valid
 * @retval HTTP_WEB_SOCKET_INFLATE_STOPPED the sink stopped it
 * @retval HTTP_WEB_SOCKET_INFLATE_FAILED the decompressor failed, or cerver was built without zlib
 */
CERVER_PUBLIC u8 http_web_socket_deflate_decompress (
	HttpWebSocketDeflate *deflate,
	const u8 *data, const size_t size, const bool end,
	HttpWebSocketInflateSink sink, void *sink_data
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
