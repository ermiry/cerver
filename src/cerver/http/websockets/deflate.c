#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <ctype.h>

#ifdef CERVER_ZLIB
#include <zlib.h>
#endif

#include "cerver/types/types.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/frame.h"

#define HTTP_WEB_SOCKET_DEFLATE_NAME_LENGTH				(sizeof (HTTP_WEB_SOCKET_DEFLATE_NAME) - 1)

static const char *http_web_socket_deflate_server_no_context_takeover = { "server_no_context_takeover" };
static const char *http_web_socket_deflate_client_no_context_takeover = { "client_no_context_takeover" };
static const char *http_web_socket_deflate_server_max_window_bits = { "server_max_window_bits" };
static const char *http_web_socket_deflate_client_max_window_bits = { "client_max_window_bits" };

static const char http_web_socket_deflate_extension_separator = ',';
static const char http_web_socket_deflate_parameter_separator = ';';
static const char http_web_socket_deflate_value_separator = '=';
static const char http_web_socket_deflate_quote = '"';
static const char http_web_socket_deflate_escape = '\\';
static const char http_web_socket_deflate_space = ' ';
static const char http_web_socket_deflate_tab = '\t';
static const char http_web_socket_deflate_zero = '0';

// the characters that can not be part of a token (RFC 9110 section 5.6.2)
static const char *http_web_socket_deflate_delimiters = { "()<>@,;:\\\"/[]?={}" };

// the last character that is not a control one
#define HTTP_WEB_SOCKET_DEFLATE_LAST_VISIBLE			0x7E

#define HTTP_WEB_SOCKET_DEFLATE_PARAMETER_FORMAT		"; %s"
#define HTTP_WEB_SOCKET_DEFLATE_BITS_FORMAT				"; %s=%u"

// the most digits of a number of window bits
#define HTTP_WEB_SOCKET_DEFLATE_BITS_MAX_DIGITS			2

#define HTTP_WEB_SOCKET_DEFLATE_DECIMAL_BASE			10

// the bits that mark each parameter that was seen in an offer
#define HTTP_WEB_SOCKET_DEFLATE_SEEN_SERVER_NO_CONTEXT	(1 << 0)
#define HTTP_WEB_SOCKET_DEFLATE_SEEN_CLIENT_NO_CONTEXT	(1 << 1)
#define HTTP_WEB_SOCKET_DEFLATE_SEEN_SERVER_BITS		(1 << 2)
#define HTTP_WEB_SOCKET_DEFLATE_SEEN_CLIENT_BITS		(1 << 3)

// the bytes of a token or a value of a parameter
#define HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE				(1 << 6)

// the first size of the output of the compressor, that doubles when it is full
#define HTTP_WEB_SOCKET_DEFLATE_OUTPUT_SIZE				(1 << 12)

// the bytes that the decompressor writes before it gives them to the sink
#define HTTP_WEB_SOCKET_DEFLATE_CHUNK_SIZE				(1 << 14)

// the end of a flushed block, that the sender removes and the receiver adds again (RFC 7692 section 7.2.1)
#define HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE				4

#ifdef CERVER_ZLIB
static const u8 http_web_socket_deflate_tail[HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE] = { 0x00, 0x00, 0xFF, 0xFF };

// the payload of a message without bytes: the header of a block without compression
static const u8 http_web_socket_deflate_empty_block = 0x00;

// the bit of the data_type of zlib that says that inflate () stopped between two blocks
#define HTTP_WEB_SOCKET_DEFLATE_BLOCK_BOUNDARY			(1 << 7)
#endif

void http_web_socket_deflate_options_init (
	HttpWebSocketDeflateOptions *options
) {

	if (options) {
		options->enabled = false;
		options->level = HTTP_WEB_SOCKET_DEFLATE_DEFAULT_LEVEL;
		options->memory_level = HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MEMORY_LEVEL;
		options->window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
		options->no_context_takeover = false;
		options->min_size = HTTP_WEB_SOCKET_DEFLATE_DEFAULT_MIN_SIZE;
	}

}

bool http_web_socket_deflate_available (void) {

	#ifdef CERVER_ZLIB
	return true;
	#else
	return false;
	#endif

}

#pragma region parse

// a position in the value of a Sec-WebSocket-Extensions header
typedef struct HttpWebSocketDeflateCursor {

	const char *value;
	size_t length;
	size_t position;

	// the syntax was broken, nothing after it can be trusted
	bool broken;

} HttpWebSocketDeflateCursor;

// one extension of the list, with its parameters
typedef struct HttpWebSocketDeflateExtension {

	bool is_deflate;

	// a parameter that is unknown, repeated or with a wrong value
	bool invalid;

	unsigned int seen;

	bool server_no_context_takeover;
	bool client_no_context_takeover;

	u8 server_max_window_bits;
	u8 client_max_window_bits;

} HttpWebSocketDeflateExtension;

static void http_web_socket_deflate_skip_spaces (HttpWebSocketDeflateCursor *cursor) {

	while (
		(cursor->position < cursor->length)
		&& (
			(cursor->value[cursor->position] == http_web_socket_deflate_space)
			|| (cursor->value[cursor->position] == http_web_socket_deflate_tab)
		)
	) {
		cursor->position += 1;
	}

}

static bool http_web_socket_deflate_is_token_char (const char c) {

	return (c > http_web_socket_deflate_space) && (c <= HTTP_WEB_SOCKET_DEFLATE_LAST_VISIBLE)
		&& !strchr (http_web_socket_deflate_delimiters, c);

}

// reads a token in token (with its '\0'), returns its length, 0 when there is none
static size_t http_web_socket_deflate_read_token (
	HttpWebSocketDeflateCursor *cursor, char *token
) {

	size_t length = 0;
	http_web_socket_deflate_skip_spaces (cursor);
	while (
		(cursor->position < cursor->length)
		&& http_web_socket_deflate_is_token_char (cursor->value[cursor->position])
	) {
		if (length < (HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE - 1)) token[length] = cursor->value[cursor->position];
		length += 1;
		cursor->position += 1;
	}

	if (length >= HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE) cursor->broken = true;

	token[(length < HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE) ? length : 0] = '\0';
	http_web_socket_deflate_skip_spaces (cursor);

	return length;

}

// reads a quoted string in value, without its quotes and escapes
static size_t http_web_socket_deflate_read_quoted (
	HttpWebSocketDeflateCursor *cursor, char *value
) {

	size_t length = 0;
	bool closed = false;

	cursor->position += 1;
	while (!closed && (cursor->position < cursor->length)) {
		char c = cursor->value[cursor->position];
		cursor->position += 1;

		if (c == http_web_socket_deflate_quote) {
			closed = true;
		}

		else {
			if ((c == http_web_socket_deflate_escape) && (cursor->position < cursor->length)) {
				c = cursor->value[cursor->position];
				cursor->position += 1;
			}

			if (length < (HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE - 1)) value[length] = c;
			length += 1;
		}
	}

	if (!closed || (length >= HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE)) cursor->broken = true;

	value[(length < HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE) ? length : 0] = '\0';
	http_web_socket_deflate_skip_spaces (cursor);

	return length;

}

// a number of window bits, 8 to 15, only with digits
static bool http_web_socket_deflate_parse_bits (const char *value, u8 *bits) {

	bool valid = false;

	const size_t length = strlen (value);
	if ((length > 0) && (length <= HTTP_WEB_SOCKET_DEFLATE_BITS_MAX_DIGITS)) {
		unsigned int number = 0;
		valid = true;
		for (size_t i = 0; i < length; i++) {
			if (!isdigit ((unsigned char) value[i])) valid = false;
			else number = (number * HTTP_WEB_SOCKET_DEFLATE_DECIMAL_BASE) + (unsigned int) (value[i] - http_web_socket_deflate_zero);
		}

		valid = valid && (value[0] != http_web_socket_deflate_zero)
			&& (number >= HTTP_WEB_SOCKET_DEFLATE_MIN_WINDOW_BITS)
			&& (number <= HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS);

		if (valid) *bits = (u8) number;
	}

	return valid;

}

// marks a parameter as seen, a repeated one makes the extension invalid
static bool http_web_socket_deflate_see (
	HttpWebSocketDeflateExtension *extension, const unsigned int bit
) {

	if (extension->seen & bit) extension->invalid = true;
	extension->seen |= bit;

	return !extension->invalid;

}

// a parameter without a value
static void http_web_socket_deflate_parameter_flag (
	HttpWebSocketDeflateExtension *extension,
	const unsigned int bit, bool *flag, const bool has_value
) {

	if (has_value) extension->invalid = true;
	else if (http_web_socket_deflate_see (extension, bit)) *flag = true;

}

// a parameter of window bits, its value is optional only when it can be empty
static void http_web_socket_deflate_parameter_bits (
	HttpWebSocketDeflateExtension *extension,
	const unsigned int bit, u8 *bits,
	const char *value, const bool has_value, const bool value_optional
) {

	if (http_web_socket_deflate_see (extension, bit)) {
		if (has_value) {
			if (!http_web_socket_deflate_parse_bits (value, bits)) extension->invalid = true;
		}

		else if (!value_optional) {
			extension->invalid = true;
		}
	}

}

// applies a parameter of permessage-deflate,
// client_max_window_bits can be empty only in an offer
static void http_web_socket_deflate_parameter (
	HttpWebSocketDeflateExtension *extension,
	const char *name, const char *value, const bool has_value,
	const bool offer
) {

	if (!strcasecmp (name, http_web_socket_deflate_server_no_context_takeover)) {
		http_web_socket_deflate_parameter_flag (
			extension, HTTP_WEB_SOCKET_DEFLATE_SEEN_SERVER_NO_CONTEXT,
			&extension->server_no_context_takeover, has_value
		);
	}

	else if (!strcasecmp (name, http_web_socket_deflate_client_no_context_takeover)) {
		http_web_socket_deflate_parameter_flag (
			extension, HTTP_WEB_SOCKET_DEFLATE_SEEN_CLIENT_NO_CONTEXT,
			&extension->client_no_context_takeover, has_value
		);
	}

	else if (!strcasecmp (name, http_web_socket_deflate_server_max_window_bits)) {
		http_web_socket_deflate_parameter_bits (
			extension, HTTP_WEB_SOCKET_DEFLATE_SEEN_SERVER_BITS,
			&extension->server_max_window_bits, value, has_value, false
		);
	}

	else if (!strcasecmp (name, http_web_socket_deflate_client_max_window_bits)) {
		http_web_socket_deflate_parameter_bits (
			extension, HTTP_WEB_SOCKET_DEFLATE_SEEN_CLIENT_BITS,
			&extension->client_max_window_bits, value, has_value, offer
		);
	}

	else {
		extension->invalid = true;
	}

}

// reads the value of a parameter, a token or a quoted string
static bool http_web_socket_deflate_read_value (
	HttpWebSocketDeflateCursor *cursor, char *value
) {

	bool has_value = false;

	if (
		(cursor->position < cursor->length)
		&& (cursor->value[cursor->position] == http_web_socket_deflate_value_separator)
	) {
		cursor->position += 1;
		http_web_socket_deflate_skip_spaces (cursor);

		if (
			(cursor->position < cursor->length)
			&& (cursor->value[cursor->position] == http_web_socket_deflate_quote)
		) {
			(void) http_web_socket_deflate_read_quoted (cursor, value);
		}

		else if (!http_web_socket_deflate_read_token (cursor, value)) {
			cursor->broken = true;
		}

		has_value = true;
	}

	return has_value;

}

// reads the parameters of an extension, until the ',' that ends it
static void http_web_socket_deflate_read_parameters (
	HttpWebSocketDeflateCursor *cursor,
	HttpWebSocketDeflateExtension *extension, const bool offer
) {

	char name[HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE];
	char value[HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE];

	while (
		!cursor->broken
		&& (cursor->position < cursor->length)
		&& (cursor->value[cursor->position] == http_web_socket_deflate_parameter_separator)
	) {
		cursor->position += 1;

		if (!http_web_socket_deflate_read_token (cursor, name)) {
			cursor->broken = true;
		}

		else {
			value[0] = '\0';
			const bool has_value = http_web_socket_deflate_read_value (cursor, value);
			if (extension->is_deflate && !cursor->broken) {
				http_web_socket_deflate_parameter (extension, name, value, has_value, offer);
			}
		}
	}

}

// reads the next extension of the list
// returns false when there are no more, or the syntax is broken
static bool http_web_socket_deflate_read_extension (
	HttpWebSocketDeflateCursor *cursor,
	HttpWebSocketDeflateExtension *extension, const bool offer
) {

	(void) memset (extension, 0, sizeof (HttpWebSocketDeflateExtension));
	extension->server_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
	extension->client_max_window_bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	char name[HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE];

	// empty elements of the list are skipped
	http_web_socket_deflate_skip_spaces (cursor);
	while (
		(cursor->position < cursor->length)
		&& (cursor->value[cursor->position] == http_web_socket_deflate_extension_separator)
	) {
		cursor->position += 1;
		http_web_socket_deflate_skip_spaces (cursor);
	}

	bool read = false;
	if (!cursor->broken && (cursor->position < cursor->length)) {
		if (!http_web_socket_deflate_read_token (cursor, name)) {
			cursor->broken = true;
		}

		else {
			extension->is_deflate = !strcasecmp (name, HTTP_WEB_SOCKET_DEFLATE_NAME);
			http_web_socket_deflate_read_parameters (cursor, extension, offer);

			const bool ended = (cursor->position >= cursor->length)
				|| (cursor->value[cursor->position] == http_web_socket_deflate_extension_separator);

			if (!ended) cursor->broken = true;
			read = !cursor->broken;
		}
	}

	return read;

}

#pragma endregion

#pragma region negotiation

// the window bits of the compressor of cerver, that zlib can use
static u8 http_web_socket_deflate_options_bits (
	const HttpWebSocketDeflateOptions *options
) {

	u8 bits = options->window_bits;
	if (bits < HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS) bits = HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS;
	if (bits > HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS) bits = HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;

	return bits;

}

// the server decides the parameters of an offer that it can accept
// returns false when it has to decline it
static bool http_web_socket_deflate_accept_offer (
	const HttpWebSocketDeflateExtension *offer,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
) {

	bool accepted = false;

	u8 bits = http_web_socket_deflate_options_bits (options);
	const bool limited = (offer->seen & HTTP_WEB_SOCKET_DEFLATE_SEEN_SERVER_BITS) != 0;
	if (!limited || (offer->server_max_window_bits >= HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS)) {
		if (limited && (offer->server_max_window_bits < bits)) bits = offer->server_max_window_bits;

		(void) memset (parameters, 0, sizeof (HttpWebSocketDeflateParameters));

		parameters->server_no_context_takeover = offer->server_no_context_takeover || options->no_context_takeover;
		parameters->client_no_context_takeover = offer->client_no_context_takeover || options->no_context_takeover;

		parameters->server_max_window_bits = bits;
		parameters->server_max_window_bits_set = limited || (bits < HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS);

		// the client keeps its window, the decompressor of cerver always takes 2^15
		parameters->client_max_window_bits = (offer->seen & HTTP_WEB_SOCKET_DEFLATE_SEEN_CLIENT_BITS) ?
			offer->client_max_window_bits : HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS;
		parameters->client_max_window_bits_set = false;

		accepted = true;
	}

	return accepted;

}

bool http_web_socket_deflate_negotiate (
	const char *offers, const size_t length,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
) {

	bool accepted = false;

	if (
		offers && options && parameters
		&& options->enabled && http_web_socket_deflate_available ()
	) {
		HttpWebSocketDeflateCursor cursor = { offers, length, 0, false };
		HttpWebSocketDeflateExtension extension;

		while (!accepted && http_web_socket_deflate_read_extension (&cursor, &extension, true)) {
			if (extension.is_deflate && !extension.invalid) {
				accepted = http_web_socket_deflate_accept_offer (&extension, options, parameters);
			}
		}
	}

	return accepted;

}

// appends text to the value, returns false when it does not fit
static bool http_web_socket_deflate_append (
	char *value, const size_t size, size_t *length, const char *text
) {

	bool appended = false;

	const size_t text_length = strlen (text);
	if ((*length + text_length) < size) {
		(void) memcpy (value + *length, text, text_length);
		*length += text_length;
		value[*length] = '\0';
		appended = true;
	}

	return appended;

}

// appends "; name" or "; name=bits"
static bool http_web_socket_deflate_append_parameter (
	char *value, const size_t size, size_t *length,
	const char *name, const u8 bits, const bool with_bits
) {

	char parameter[HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE];
	const int written = with_bits ?
		snprintf (parameter, HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE, HTTP_WEB_SOCKET_DEFLATE_BITS_FORMAT, name, (unsigned int) bits) :
		snprintf (parameter, HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE, HTTP_WEB_SOCKET_DEFLATE_PARAMETER_FORMAT, name);

	return (written > 0) && (written < HTTP_WEB_SOCKET_DEFLATE_TOKEN_SIZE)
		&& http_web_socket_deflate_append (value, size, length, parameter);

}

size_t http_web_socket_deflate_response (
	const HttpWebSocketDeflateParameters *parameters,
	char *value, const size_t size
) {

	size_t length = 0;
	bool fits = parameters && value && size
		&& http_web_socket_deflate_append (value, size, &length, HTTP_WEB_SOCKET_DEFLATE_NAME);

	if (fits && parameters->server_no_context_takeover) {
		fits = http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_server_no_context_takeover, 0, false
		);
	}

	if (fits && parameters->client_no_context_takeover) {
		fits = http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_client_no_context_takeover, 0, false
		);
	}

	if (fits && parameters->server_max_window_bits_set) {
		fits = http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_server_max_window_bits,
			parameters->server_max_window_bits, true
		);
	}

	if (fits && parameters->client_max_window_bits_set) {
		fits = http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_client_max_window_bits,
			parameters->client_max_window_bits, true
		);
	}

	return fits ? length : 0;

}

size_t http_web_socket_deflate_offer (
	const HttpWebSocketDeflateOptions *options,
	char *value, const size_t size
) {

	size_t length = 0;
	bool fits = options && value && size
		&& http_web_socket_deflate_append (value, size, &length, HTTP_WEB_SOCKET_DEFLATE_NAME);

	if (fits && options->no_context_takeover) {
		fits = http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_server_no_context_takeover, 0, false
		) && http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_client_no_context_takeover, 0, false
		);
	}

	// the client says that it can limit its window, to the value of the response
	if (fits) {
		const u8 bits = http_web_socket_deflate_options_bits (options);
		fits = http_web_socket_deflate_append_parameter (
			value, size, &length, http_web_socket_deflate_client_max_window_bits,
			bits, bits < HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS
		);
	}

	return fits ? length : 0;

}

// the parameters of a valid response, from the side of the client
static bool http_web_socket_deflate_accept_response (
	const HttpWebSocketDeflateExtension *response,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
) {

	bool accepted = false;

	u8 bits = http_web_socket_deflate_options_bits (options);
	const bool limited = (response->seen & HTTP_WEB_SOCKET_DEFLATE_SEEN_CLIENT_BITS) != 0;
	if (!limited || (response->client_max_window_bits >= HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS)) {
		if (limited && (response->client_max_window_bits < bits)) bits = response->client_max_window_bits;

		(void) memset (parameters, 0, sizeof (HttpWebSocketDeflateParameters));

		parameters->server_no_context_takeover = response->server_no_context_takeover;
		parameters->client_no_context_takeover = response->client_no_context_takeover || options->no_context_takeover;

		parameters->server_max_window_bits = response->server_max_window_bits;
		parameters->server_max_window_bits_set = (response->seen & HTTP_WEB_SOCKET_DEFLATE_SEEN_SERVER_BITS) != 0;

		parameters->client_max_window_bits = bits;
		parameters->client_max_window_bits_set = limited;

		accepted = true;
	}

	return accepted;

}

bool http_web_socket_deflate_accept (
	const char *value, const size_t length,
	const HttpWebSocketDeflateOptions *options,
	HttpWebSocketDeflateParameters *parameters
) {

	bool accepted = false;

	if (
		value && options && parameters
		&& options->enabled && http_web_socket_deflate_available ()
	) {
		HttpWebSocketDeflateCursor cursor = { value, length, 0, false };
		HttpWebSocketDeflateExtension extension;

		// the response has only the extension that was offered, once
		unsigned int n_extensions = 0;
		bool valid = true;
		while (valid && http_web_socket_deflate_read_extension (&cursor, &extension, false)) {
			n_extensions += 1;
			valid = extension.is_deflate && !extension.invalid && (n_extensions == 1)
				&& http_web_socket_deflate_accept_response (&extension, options, parameters);
		}

		accepted = valid && !cursor.broken && (n_extensions == 1);
	}

	return accepted;

}

#pragma endregion

#pragma region streams

HttpWebSocketDeflate *http_web_socket_deflate_create (
	const HttpWebSocketRole role,
	const HttpWebSocketDeflateParameters *parameters,
	const HttpWebSocketDeflateOptions *options
) {

	HttpWebSocketDeflate *deflate = NULL;

	if (parameters && options && http_web_socket_deflate_available ()) {
		deflate = (HttpWebSocketDeflate *) calloc (1, sizeof (HttpWebSocketDeflate));
		if (deflate) {
			deflate->role = role;
			deflate->parameters = *parameters;
			deflate->level = options->level;
			deflate->memory_level = options->memory_level;
			deflate->min_size = options->min_size;
		}
	}

	return deflate;

}

#ifdef CERVER_ZLIB

// the window bits of the compressor of cerver, the decompressor takes any window
static int http_web_socket_deflate_compressor_bits (const HttpWebSocketDeflate *deflate) {

	const u8 bits = (deflate->role == HTTP_WEB_SOCKET_ROLE_SERVER) ?
		deflate->parameters.server_max_window_bits : deflate->parameters.client_max_window_bits;

	return (bits < HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS) ?
		HTTP_WEB_SOCKET_DEFLATE_MIN_COMPRESSION_BITS : bits;

}

// the compressor of cerver starts each message from zero
static bool http_web_socket_deflate_compressor_resets (const HttpWebSocketDeflate *deflate) {

	return (deflate->role == HTTP_WEB_SOCKET_ROLE_SERVER) ?
		deflate->parameters.server_no_context_takeover : deflate->parameters.client_no_context_takeover;

}

// the peer starts each message from zero, so the decompressor does too
static bool http_web_socket_deflate_decompressor_resets (const HttpWebSocketDeflate *deflate) {

	return (deflate->role == HTTP_WEB_SOCKET_ROLE_SERVER) ?
		deflate->parameters.client_no_context_takeover : deflate->parameters.server_no_context_takeover;

}

static z_stream *http_web_socket_deflate_compressor (HttpWebSocketDeflate *deflate) {

	if (!deflate->compressor) {
		z_stream *stream = (z_stream *) calloc (1, sizeof (z_stream));
		if (stream) {
			// a negative window is raw deflate, without the header and the checksum of zlib
			if (deflateInit2 (
				stream, deflate->level, Z_DEFLATED,
				-http_web_socket_deflate_compressor_bits (deflate),
				deflate->memory_level, Z_DEFAULT_STRATEGY
			) == Z_OK) {
				deflate->compressor = stream;
			}

			else {
				free (stream);
			}
		}
	}

	return (z_stream *) deflate->compressor;

}

static z_stream *http_web_socket_deflate_decompressor (HttpWebSocketDeflate *deflate) {

	if (!deflate->decompressor) {
		z_stream *stream = (z_stream *) calloc (1, sizeof (z_stream));
		u8 *chunk = (u8 *) malloc (HTTP_WEB_SOCKET_DEFLATE_CHUNK_SIZE);
		if (
			stream && chunk
			&& (inflateInit2 (stream, -HTTP_WEB_SOCKET_DEFLATE_MAX_WINDOW_BITS) == Z_OK)
		) {
			deflate->decompressor = stream;
			deflate->chunk = chunk;
			deflate->chunk_size = HTTP_WEB_SOCKET_DEFLATE_CHUNK_SIZE;
		}

		else {
			free (stream);
			free (chunk);
		}
	}

	return (z_stream *) deflate->decompressor;

}

// doubles the output when it is full
static bool http_web_socket_deflate_output_reserve (HttpWebSocketDeflate *deflate) {

	bool reserved = true;

	if (deflate->output_size == deflate->output_capacity) {
		const size_t capacity = deflate->output_capacity ?
			(deflate->output_capacity * 2) : HTTP_WEB_SOCKET_DEFLATE_OUTPUT_SIZE;

		u8 *output = (u8 *) realloc (deflate->output, capacity);
		if (output) {
			deflate->output = output;
			deflate->output_capacity = capacity;
		}

		else {
			reserved = false;
		}
	}

	return reserved;

}

// runs the compressor until it takes all the input, and until it flushes everything at the end
static u8 http_web_socket_deflate_compress_run (
	HttpWebSocketDeflate *compression, z_stream *stream, const int flush
) {

	u8 retval = 0;

	bool running = true;
	while (running && !retval) {
		if (!http_web_socket_deflate_output_reserve (compression)) {
			retval = 1;
			break;
		}

		stream->next_out = compression->output + compression->output_size;
		stream->avail_out = (uInt) (compression->output_capacity - compression->output_size);

		const int result = deflate (stream, flush);
		compression->output_size = compression->output_capacity - stream->avail_out;

		if ((result != Z_OK) && (result != Z_BUF_ERROR)) retval = 1;

		// a flush is complete when it did not fill the output
		running = stream->avail_in || !stream->avail_out;
	}

	return retval;

}

// a message without bytes is an empty block without compression, without its tail
// (RFC 7692 section 7.2.3.6), that does not change the context that the peer keeps
// returns 0 on success, 1 on error
static u8 http_web_socket_deflate_compress_empty (HttpWebSocketDeflate *deflate) {

	u8 retval = 1;

	if (http_web_socket_deflate_output_reserve (deflate)) {
		deflate->output[0] = http_web_socket_deflate_empty_block;
		deflate->output_size = sizeof (http_web_socket_deflate_empty_block);

		retval = 0;
	}

	return retval;

}

u8 http_web_socket_deflate_compress (
	HttpWebSocketDeflate *deflate,
	const void *data, const size_t size, const bool end,
	const u8 **output, size_t *output_size
) {

	u8 retval = 1;

	z_stream *stream = deflate ? http_web_socket_deflate_compressor (deflate) : NULL;
	if (
		stream && (data || !size) && output && output_size
		&& (size <= HTTP_WEB_SOCKET_DEFLATE_INPUT_MAX)
	) {
		deflate->output_size = 0;

		stream->next_in = (Bytef *) data;
		stream->avail_in = (uInt) size;

		retval = http_web_socket_deflate_compress_run (
			deflate, stream, end ? Z_SYNC_FLUSH : Z_NO_FLUSH
		);

		if (!retval && end) {
			if (
				(deflate->output_size >= HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE)
				&& !memcmp (
					deflate->output + deflate->output_size - HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE,
					http_web_socket_deflate_tail, HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE
				)
			) {
				deflate->output_size -= HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE;
			}

			// the compressor gives nothing for a flush without input right after another flush
			if (!deflate->output_size) retval = http_web_socket_deflate_compress_empty (deflate);

			if (http_web_socket_deflate_compressor_resets (deflate)) (void) deflateReset (stream);
		}

		*output = deflate->output;
		*output_size = deflate->output_size;
	}

	return retval;

}

// a block with BFINAL ends the stream of zlib, but not the message nor the context of the connection
// (RFC 7692 section 7.2.3.4): the inflater starts again in the next byte and keeps its window,
// inflateResetKeep () is the inflateReset () that does not forget it
// returns 0 on success, 1 on error
static u8 http_web_socket_deflate_decompress_restart (
	HttpWebSocketDeflate *deflate, z_stream *stream
) {

	u8 retval = HTTP_WEB_SOCKET_INFLATE_FAILED;

	if (inflateResetKeep (stream) == Z_OK) {
		deflate->ended = true;

		retval = 0;
	}

	return retval;

}

// the memory of zlib is a failure of cerver, anything else is in the bytes of the peer
static u8 http_web_socket_deflate_decompress_error (const int result) {

	return (result == Z_MEM_ERROR) ?
		HTTP_WEB_SOCKET_INFLATE_FAILED : HTTP_WEB_SOCKET_INFLATE_INVALID;

}

// inflates the input that the stream has, giving each piece to the sink
static u8 http_web_socket_deflate_decompress_run (
	HttpWebSocketDeflate *deflate, z_stream *stream,
	HttpWebSocketInflateSink sink, void *sink_data
) {

	u8 retval = 0;

	bool running = true;
	while (running && !retval) {
		const uInt input = stream->avail_in;

		stream->next_out = deflate->chunk;
		stream->avail_out = (uInt) deflate->chunk_size;

		deflate->ended = false;

		const int result = inflate (stream, Z_SYNC_FLUSH);
		const size_t produced = deflate->chunk_size - stream->avail_out;

		if (produced && sink (sink_data, deflate->chunk, produced)) retval = HTTP_WEB_SOCKET_INFLATE_STOPPED;

		if (result == Z_STREAM_END) {
			const u8 restarted = http_web_socket_deflate_decompress_restart (deflate, stream);
			if (!retval) retval = restarted;

			// the blocks that follow the last one
			running = (stream->avail_in != 0);
		}

		else if ((result != Z_OK) && (result != Z_BUF_ERROR)) {
			if (!retval) retval = http_web_socket_deflate_decompress_error (result);
		}

		else if (!produced && (input == stream->avail_in)) {
			// zlib can not go on, the input is truncated until the next piece
			running = false;
		}

		else {
			running = stream->avail_in || !stream->avail_out;
		}
	}

	return retval;

}

static u8 http_web_socket_deflate_decompress_input (
	HttpWebSocketDeflate *deflate, z_stream *stream,
	const u8 *data, const size_t size,
	HttpWebSocketInflateSink sink, void *sink_data
) {

	u8 retval = 0;

	if (size) {
		stream->next_in = (Bytef *) data;
		stream->avail_in = (uInt) size;
		retval = http_web_socket_deflate_decompress_run (deflate, stream, sink, sink_data);
	}

	return retval;

}

// the end of a message: the tail that its sender removed completes the empty block of its flush,
// and the inflater must be left between two blocks, a message that ends inside one was cut
// a message that ends right after a block with BFINAL is complete without the tail
// returns 0 on success, HTTP_WEB_SOCKET_INFLATE_INVALID on a message that is not complete
static u8 http_web_socket_deflate_decompress_end (
	HttpWebSocketDeflate *deflate, z_stream *stream,
	HttpWebSocketInflateSink sink, void *sink_data
) {

	u8 retval = 0;

	if (!deflate->ended) {
		stream->next_in = (Bytef *) http_web_socket_deflate_tail;
		stream->avail_in = HTTP_WEB_SOCKET_DEFLATE_TAIL_SIZE;
		retval = http_web_socket_deflate_decompress_run (deflate, stream, sink, sink_data);

		if (!retval && !(stream->data_type & HTTP_WEB_SOCKET_DEFLATE_BLOCK_BOUNDARY)) {
			retval = HTTP_WEB_SOCKET_INFLATE_INVALID;
		}
	}

	if (!retval && http_web_socket_deflate_decompressor_resets (deflate)) {
		(void) inflateReset (stream);
	}

	return retval;

}

u8 http_web_socket_deflate_decompress (
	HttpWebSocketDeflate *deflate,
	const u8 *data, const size_t size, const bool end,
	HttpWebSocketInflateSink sink, void *sink_data
) {

	// without its decompressor (its memory) or with a piece that zlib does not take
	u8 retval = HTTP_WEB_SOCKET_INFLATE_FAILED;

	z_stream *stream = deflate ? http_web_socket_deflate_decompressor (deflate) : NULL;
	if (stream && (data || !size) && sink && (size <= HTTP_WEB_SOCKET_DEFLATE_INPUT_MAX)) {
		retval = http_web_socket_deflate_decompress_input (deflate, stream, data, size, sink, sink_data);

		if (!retval && end) {
			retval = http_web_socket_deflate_decompress_end (deflate, stream, sink, sink_data);
		}

		if (end) deflate->ended = false;
	}

	return retval;

}

void http_web_socket_deflate_delete (HttpWebSocketDeflate *deflate) {

	if (deflate) {
		if (deflate->compressor) {
			(void) deflateEnd ((z_stream *) deflate->compressor);
			free (deflate->compressor);
		}

		if (deflate->decompressor) {
			(void) inflateEnd ((z_stream *) deflate->decompressor);
			free (deflate->decompressor);
		}

		free (deflate->output);
		free (deflate->chunk);

		free (deflate);
	}

}

#else

u8 http_web_socket_deflate_compress (
	HttpWebSocketDeflate *deflate,
	const void *data, const size_t size, const bool end,
	const u8 **output, size_t *output_size
) {

	(void) deflate;
	(void) data;
	(void) size;
	(void) end;
	(void) output;
	(void) output_size;

	return 1;

}

u8 http_web_socket_deflate_decompress (
	HttpWebSocketDeflate *deflate,
	const u8 *data, const size_t size, const bool end,
	HttpWebSocketInflateSink sink, void *sink_data
) {

	(void) deflate;
	(void) data;
	(void) size;
	(void) end;
	(void) sink;
	(void) sink_data;

	return HTTP_WEB_SOCKET_INFLATE_FAILED;

}

void http_web_socket_deflate_delete (HttpWebSocketDeflate *deflate) {

	free (deflate);

}

#endif

#pragma endregion
