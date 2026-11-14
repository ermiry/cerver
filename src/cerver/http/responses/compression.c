#include <ctype.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <strings.h>

#ifdef CERVER_ZLIB
#include <zlib.h>
#endif

#include "cerver/collections/arena.h"

#include "cerver/http/responses/compression.h"

static const char http_compression_gzip_name[] = { HTTP_COMPRESSION_ENCODING };
static const size_t http_compression_gzip_name_length = 4;

static const char http_compression_x_gzip_name[] = { "x-gzip" };
static const size_t http_compression_x_gzip_name_length = 6;

static const char http_compression_any_name = '*';

static const char http_compression_list_separator = ',';
static const char http_compression_parameter_separator = ';';

static const char http_compression_quality_name[] = { "q=" };
static const size_t http_compression_quality_name_length = 2;

// a coding without a quality value
#define HTTP_COMPRESSION_DEFAULT_QUALITY		1.0
#define HTTP_COMPRESSION_UNSET_QUALITY			-1.0

static const char *http_compression_types[] = {
	"text/",
	"application/json",
	"application/javascript",
	"application/xml",
	"application/x-www-form-urlencoded",
	"image/svg+xml"
};

static const size_t http_compression_n_types = (
	sizeof (http_compression_types) / sizeof (const char *)
);

// the suffixes of the structured types, like "application/problem+json"
static const char *http_compression_type_suffixes[] = { "+json", "+xml" };
static const size_t http_compression_n_type_suffixes = 2;

static const char http_compression_type_parameters_separator = ';';

bool http_compression_available (void) {

	#ifdef CERVER_ZLIB
	return true;
	#else
	return false;
	#endif

}

static const char *http_compression_trim_start (const char *current, const char *end) {

	while ((current < end) && isspace ((unsigned char) *current)) current += 1;

	return current;

}

static const char *http_compression_trim_end (const char *start, const char *end) {

	while ((end > start) && isspace ((unsigned char) *(end - 1))) end -= 1;

	return end;

}

// the quality value of the parameters of a coding, "q=0.5"
static double http_compression_quality (const char *parameters, const char *end) {

	double quality = HTTP_COMPRESSION_DEFAULT_QUALITY;

	const char *current = parameters;
	while (current < end) {
		current = http_compression_trim_start (current + 1, end);
		if (
			((size_t) (end - current) > http_compression_quality_name_length)
			&& !strncasecmp (current, http_compression_quality_name, http_compression_quality_name_length)
		) {
			quality = strtod (current + http_compression_quality_name_length, NULL);
			break;
		}

		const char *next = (const char *) memchr (current, http_compression_parameter_separator, (size_t) (end - current));
		current = next ? next : end;
	}

	return quality;

}

static bool http_compression_coding_is (
	const char *coding, const size_t length,
	const char *name, const size_t name_length
) {

	return (length == name_length) && !strncasecmp (coding, name, name_length);

}

// sets the quality of gzip and of "*" from one item of the list
static void http_compression_accept_item (
	const char *item, const char *item_end,
	double *gzip_quality, double *any_quality
) {

	const char *parameters = (const char *) memchr (item, http_compression_parameter_separator, (size_t) (item_end - item));
	const char *coding_end = http_compression_trim_end (item, parameters ? parameters : item_end);
	const size_t coding_length = (size_t) (coding_end - item);

	const double quality = parameters ? http_compression_quality (parameters, item_end) : HTTP_COMPRESSION_DEFAULT_QUALITY;

	if (
		http_compression_coding_is (item, coding_length, http_compression_gzip_name, http_compression_gzip_name_length)
		|| http_compression_coding_is (item, coding_length, http_compression_x_gzip_name, http_compression_x_gzip_name_length)
	) {
		*gzip_quality = quality;
	}

	else if ((coding_length == 1) && (*item == http_compression_any_name)) {
		*any_quality = quality;
	}

}

bool http_compression_accepts_gzip (
	const char *accept_encoding, const size_t length
) {

	double gzip_quality = HTTP_COMPRESSION_UNSET_QUALITY;
	double any_quality = HTTP_COMPRESSION_UNSET_QUALITY;

	if (accept_encoding) {
		const char *end = accept_encoding + length;
		const char *current = accept_encoding;
		while (current < end) {
			current = http_compression_trim_start (current, end);

			const size_t remaining = (current < end) ? (size_t) (end - current) : 0;
			const char *item_end = remaining ?
				(const char *) memchr (current, http_compression_list_separator, remaining) : NULL;

			if (!item_end) item_end = end;

			if (item_end > current) {
				http_compression_accept_item (current, item_end, &gzip_quality, &any_quality);
			}

			current = item_end + 1;
		}
	}

	// a coding named explicitly takes precedence over "*"
	const double quality = (gzip_quality > HTTP_COMPRESSION_UNSET_QUALITY) ? gzip_quality : any_quality;

	return quality > 0;

}

static bool http_compression_has_suffix (
	const char *content_type, const size_t length,
	const char *suffix
) {

	const size_t suffix_length = strlen (suffix);

	return (length >= suffix_length)
		&& !strncasecmp (content_type + length - suffix_length, suffix, suffix_length);

}

bool http_compression_is_compressible (
	const char *content_type, const size_t length
) {

	bool compressible = false;

	if (content_type) {
		// the media type, without its parameters ("; charset=UTF-8")
		const char *parameters = (const char *) memchr (content_type, http_compression_type_parameters_separator, length);
		const char *end = http_compression_trim_end (content_type, parameters ? parameters : content_type + length);
		const size_t type_length = (size_t) (end - content_type);

		for (size_t i = 0; !compressible && (i < http_compression_n_types); i++) {
			const size_t prefix_length = strlen (http_compression_types[i]);
			compressible = (type_length >= prefix_length)
				&& !strncasecmp (content_type, http_compression_types[i], prefix_length);
		}

		for (size_t i = 0; !compressible && (i < http_compression_n_type_suffixes); i++) {
			compressible = http_compression_has_suffix (content_type, type_length, http_compression_type_suffixes[i]);
		}
	}

	return compressible;

}

#ifdef CERVER_ZLIB

// the window bits of zlib plus 16 write a gzip header and trailer
#define HTTP_COMPRESSION_GZIP_WINDOW_BITS		(15 + 16)
#define HTTP_COMPRESSION_MEMORY_LEVEL			8

static voidpf http_compression_arena_alloc (voidpf opaque, uInt items, uInt size) {

	return arena_alloc ((Arena *) opaque, (size_t) items * (size_t) size);

}

// the memory is released with the arena
static void http_compression_arena_free (voidpf opaque, voidpf address) {

	(void) opaque;
	(void) address;

}

static bool http_compression_gzip_actual (
	z_stream *stream,
	const void *data, const size_t length,
	void **compressed, size_t *compressed_length
) {

	bool done = false;

	const uLong bound = deflateBound (stream, (uLong) length);
	Bytef *output = (Bytef *) arena_alloc ((Arena *) stream->opaque, (size_t) bound);
	if (output) {
		stream->next_in = (Bytef *) data;
		stream->avail_in = (uInt) length;
		stream->next_out = output;
		stream->avail_out = (uInt) bound;

		if (deflate (stream, Z_FINISH) == Z_STREAM_END) {
			*compressed = output;
			*compressed_length = (size_t) stream->total_out;
			done = true;
		}
	}

	return done;

}

bool http_compression_gzip (
	Arena *arena,
	const void *data, const size_t length, const int level,
	void **compressed, size_t *compressed_length
) {

	bool done = false;

	// zlib takes the lengths as 32 bits
	if (arena && data && length && compressed && compressed_length && (length <= (size_t) UINT32_MAX)) {
		z_stream stream;
		(void) memset (&stream, 0, sizeof (z_stream));
		stream.zalloc = http_compression_arena_alloc;
		stream.zfree = http_compression_arena_free;
		stream.opaque = arena;

		if (deflateInit2 (
			&stream, level, Z_DEFLATED,
			HTTP_COMPRESSION_GZIP_WINDOW_BITS, HTTP_COMPRESSION_MEMORY_LEVEL, Z_DEFAULT_STRATEGY
		) == Z_OK) {
			done = http_compression_gzip_actual (&stream, data, length, compressed, compressed_length);

			(void) deflateEnd (&stream);
		}
	}

	return done;

}

#else

bool http_compression_gzip (
	Arena *arena,
	const void *data, const size_t length, const int level,
	void **compressed, size_t *compressed_length
) {

	(void) arena;
	(void) data;
	(void) length;
	(void) level;
	(void) compressed;
	(void) compressed_length;

	return false;

}

#endif
