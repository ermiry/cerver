/**
 * @internal
 * @file cerver/http/parser/internal.h
 * @brief What the parts of the HTTP parser share: the states, the classes of the bytes, the kernels that search a
 * buffer and the values of the fields that change how a message is read.
 */

#ifndef _CERVER_HTTP_PARSER_INTERNAL_H_
#define _CERVER_HTTP_PARSER_INTERNAL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if defined (CERVER_HTTP_PARSER_SCALAR)
#elif defined (__aarch64__) && defined (__ARM_NEON)
#include <arm_neon.h>
#elif defined (__SSSE3__)
#include <tmmintrin.h>
#elif defined (__SSE2__)
#include <emmintrin.h>
#endif

#include "cerver/config.h"

#include "cerver/http/headers/types.h"
#include "cerver/http/parser/parser.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Where a parser is. */
typedef enum HttpParserState {

	/** @brief Waiting for the start line of a message, after the empty lines before it. */
	HTTP_PARSER_STATE_START						= 0,
	/** @brief The start line was read, the fields are read. */
	HTTP_PARSER_STATE_FIELDS					= 1,
	/** @brief The head was read, its body is not started. */
	HTTP_PARSER_STATE_HEAD						= 2,
	/** @brief The bytes of a Content-Length. */
	HTTP_PARSER_STATE_IDENTITY					= 3,
	/** @brief The bytes of a response until the end of the connection. */
	HTTP_PARSER_STATE_EOF						= 4,
	/** @brief The first digit of the size of a chunk. */
	HTTP_PARSER_STATE_CHUNK_SIZE_START			= 5,
	/** @brief The digits of the size of a chunk. */
	HTTP_PARSER_STATE_CHUNK_SIZE				= 6,
	/** @brief The spaces after the size of a chunk (#HTTP_PARSER_LENIENT_SPACES_AFTER_CHUNK_SIZE). */
	HTTP_PARSER_STATE_CHUNK_SIZE_SPACES			= 7,
	/** @brief After a ';' of the extensions of a chunk. */
	HTTP_PARSER_STATE_CHUNK_EXTENSIONS			= 8,
	/** @brief The name of an extension. */
	HTTP_PARSER_STATE_CHUNK_EXTENSION_NAME		= 9,
	/** @brief The token of the value of an extension. */
	HTTP_PARSER_STATE_CHUNK_EXTENSION_VALUE		= 10,
	/** @brief The quoted value of an extension. */
	HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED	= 11,
	/** @brief The byte after a '\\' of a quoted value. */
	HTTP_PARSER_STATE_CHUNK_EXTENSION_PAIR		= 12,
	/** @brief After the '"' that ends a quoted value. */
	HTTP_PARSER_STATE_CHUNK_EXTENSION_QUOTED_END	= 13,
	/** @brief The LF after the CR of the line of a chunk size. */
	HTTP_PARSER_STATE_CHUNK_SIZE_LF				= 14,
	/** @brief The data of a chunk. */
	HTTP_PARSER_STATE_CHUNK_DATA				= 15,
	/** @brief The CR after the data of a chunk. */
	HTTP_PARSER_STATE_CHUNK_DATA_CR				= 16,
	/** @brief The LF after the data of a chunk. */
	HTTP_PARSER_STATE_CHUNK_DATA_LF				= 17,
	/** @brief The trailers after the last chunk. */
	HTTP_PARSER_STATE_TRAILERS					= 18,
	/** @brief The message is complete. */
	HTTP_PARSER_STATE_COMPLETE					= 19,
	/** @brief A message with Connection close was complete: only empty lines can follow it. */
	HTTP_PARSER_STATE_CLOSED					= 20,
	/** @brief An error was found. */
	HTTP_PARSER_STATE_ERROR						= 21

} HttpParserState;

/** @brief Where a trailer is. */
typedef enum HttpParserTrailer {

	/** @brief The first byte of a line: a name or the empty line. */
	HTTP_PARSER_TRAILER_LINE		= 0,
	/** @brief The name. */
	HTTP_PARSER_TRAILER_NAME		= 1,
	/** @brief The spaces between the name and its colon (#HTTP_PARSER_LENIENT_HEADERS). */
	HTTP_PARSER_TRAILER_NAME_SPACES	= 2,
	/** @brief The spaces before the value. */
	HTTP_PARSER_TRAILER_SPACES		= 3,
	/** @brief The value. */
	HTTP_PARSER_TRAILER_VALUE		= 4,
	/** @brief The LF after the CR of a field. */
	HTTP_PARSER_TRAILER_VALUE_LF	= 5,
	/** @brief The LF after the CR of the empty line. */
	HTTP_PARSER_TRAILER_END_LF		= 6,
	/** @brief A field ended: the next line folds it (#HTTP_PARSER_LENIENT_HEADERS) or starts another one. */
	HTTP_PARSER_TRAILER_FIELD_END	= 7

} HttpParserTrailer;

/** @brief A token (RFC 9110 section 5.6.2). */
#define HTTP_PARSER_CLASS_TOKEN			0x01u
/** @brief A byte of a request target after its first one. */
#define HTTP_PARSER_CLASS_TARGET		0x02u
/** @brief A byte of the value of a field: HTAB, SP, VCHAR and obs-text. */
#define HTTP_PARSER_CLASS_VALUE			0x04u
/** @brief A byte of an authority: the userinfo, the host and the port. */
#define HTTP_PARSER_CLASS_AUTHORITY		0x08u
/** @brief A hexadecimal digit. */
#define HTTP_PARSER_CLASS_HEX			0x10u
/** @brief A byte of a quoted string (qdtext, RFC 9110 section 5.6.4). */
#define HTTP_PARSER_CLASS_QUOTED		0x20u
/** @brief A letter. */
#define HTTP_PARSER_CLASS_ALPHA			0x40u
/** @brief A byte of the token of a Connection or of a Transfer-Encoding: anything printable but ','. */
#define HTTP_PARSER_CLASS_LIST			0x80u

/** @brief The values of a byte, the size of the tables. */
#define HTTP_PARSER_BYTES				256

/** @brief The bytes of a word of the scalar kernels: they read 8 bytes at a time (SWAR). */
#define HTTP_PARSER_WORD_SIZE			sizeof (uint64_t)
/** @brief A byte of 1 in each lane of a word. */
#define HTTP_PARSER_WORD_ONES			0x0101010101010101ull
/** @brief The high bit of each lane of a word. */
#define HTTP_PARSER_WORD_HIGHS			0x8080808080808080ull
/** @brief The bits of a lane. */
#define HTTP_PARSER_WORD_BITS			8

/** @brief The classes of each byte, filled once by http_parser_tables_init (). */
CERVER_HIDDEN uint8_t http_parser_classes[HTTP_PARSER_BYTES];

/** @brief The lower case of each byte. */
CERVER_HIDDEN uint8_t http_parser_lower[HTTP_PARSER_BYTES];

/** @brief The values of a nibble, the size of the tables of the tokens by nibbles. */
#define HTTP_PARSER_NIBBLES				16
/** @brief The bits of a nibble. */
#define HTTP_PARSER_NIBBLE_BITS			4
/** @brief The low nibble of a byte. */
#define HTTP_PARSER_NIBBLE_MASK			0x0F

/**
 * @brief The tokens by the low nibble of a byte: the bit of each high nibble (2 to 7, the ones of the bytes of a
 * token) whose byte with this low nibble is a token.
 */
CERVER_HIDDEN uint8_t http_parser_token_low[HTTP_PARSER_NIBBLES];
/** @brief The bit of each high nibble in http_parser_token_low, 0 for the ones without tokens. */
CERVER_HIDDEN uint8_t http_parser_token_high[HTTP_PARSER_NIBBLES];

/** @brief Fills the tables of the parser once for the process. */
CERVER_HIDDEN void http_parser_tables_init (void);

/** @brief Fills the table of the names of the known headers, from http_parser_tables_init (). */
CERVER_HIDDEN void http_parser_fields_init (void);

/**
 * @brief The size of the open addressing table of the names of the known headers, a power of 2 four times bigger
 * than them: its key is the length and the first and the last byte of a name, so a name is found without reading
 * it whole.
 */
#define HTTP_PARSER_FIELDS_TABLE_SIZE		256
/** @brief The mask of a slot of the table. */
#define HTTP_PARSER_FIELDS_TABLE_MASK		(HTTP_PARSER_FIELDS_TABLE_SIZE - 1)

/** @brief The factor of the length in the key of a name. */
#define HTTP_PARSER_KEY_LENGTH				7u
/** @brief The factor of the first byte in the key of a name. */
#define HTTP_PARSER_KEY_FIRST				3u

/** @brief The bytes of a known name in its table: the longest one (Access-Control-Allow-Credentials) with room, in words. */
#define HTTP_PARSER_FIELD_NAME_SIZE			40

/**
 * @brief The bit of a token in lower case: an upper case letter gets it, and the rest of the bytes of a known name
 * (lower case letters, digits and '-') have it, so no other byte of a token becomes one of them.
 */
#define HTTP_PARSER_LOWER_BIT				0x20
/** @brief #HTTP_PARSER_LOWER_BIT in each lane of a word. */
#define HTTP_PARSER_LOWER_WORD				0x2020202020202020ull

/** @brief The slots of the table, the header of each one plus 1 (0 is an empty slot). */
CERVER_HIDDEN uint8_t http_parser_fields_table[HTTP_PARSER_FIELDS_TABLE_SIZE];
/** @brief The length of the name of each known header. */
CERVER_HIDDEN uint8_t http_parser_fields_lengths[HTTP_HEADERS_SIZE];
/** @brief The name of each known header in lower case, its words padded with zeros. */
CERVER_HIDDEN char http_parser_fields_names[HTTP_HEADERS_SIZE][HTTP_PARSER_FIELD_NAME_SIZE];

/** @brief The slot of a name in the table. */
static inline uint32_t http_parser_fields_key (const char *name, const size_t length) {

	const uint32_t first = (uint32_t) (unsigned char) name[0] | HTTP_PARSER_LOWER_BIT;
	const uint32_t last = (uint32_t) (unsigned char) name[length - 1] | HTTP_PARSER_LOWER_BIT;

	return (((uint32_t) length * HTTP_PARSER_KEY_LENGTH) + (first * HTTP_PARSER_KEY_FIRST) + last)
		& HTTP_PARSER_FIELDS_TABLE_MASK;

}

/** @brief Compares a name that is a token with a known one in lower case, a word at a time. */
static inline bool http_parser_fields_match (
	const char *name, const size_t length, const unsigned int type
) {

	const char *known = http_parser_fields_names[type];

	size_t i = 0;
	for (; (i + HTTP_PARSER_WORD_SIZE) <= length; i += HTTP_PARSER_WORD_SIZE) {
		uint64_t word = 0;
		uint64_t known_word = 0;
		(void) memcpy (&word, name + i, HTTP_PARSER_WORD_SIZE);
		(void) memcpy (&known_word, known + i, HTTP_PARSER_WORD_SIZE);

		if ((word | HTTP_PARSER_LOWER_WORD) != known_word) return false;
	}

	for (; i < length; i++) {
		if ((char) (name[i] | HTTP_PARSER_LOWER_BIT) != known[i]) return false;
	}

	return true;

}

/** @brief Returns the known header of a name that is a token (case insensitive), #HTTP_HEADER_UNDEFINED for another one. */
static inline http_header http_parser_field_classify (const char *name, const size_t length) {

	http_header found = HTTP_HEADER_UNDEFINED;

	if (length) {
		uint32_t slot = http_parser_fields_key (name, length);
		while (http_parser_fields_table[slot]) {
			const unsigned int type = (unsigned int) http_parser_fields_table[slot] - 1;
			if ((http_parser_fields_lengths[type] == length) && http_parser_fields_match (name, length, type)) {
				found = (http_header) type;
				break;
			}

			slot = (slot + 1) & HTTP_PARSER_FIELDS_TABLE_MASK;
		}
	}

	return found;

}

/** @brief Returns whether a byte has a class. */
static inline bool http_parser_is (const unsigned char byte, const unsigned int class_bit) {

	return (http_parser_classes[byte] & class_bit) != 0;

}

/**
 * @brief Finds the end of a line: the first LF, or with the lenient flag of a CR without LF, the first CR or LF.
 *
 * @return its offset from start, or length when there is none
 */
CERVER_HIDDEN size_t http_parser_scan_line (
	const char *start, const size_t length, const bool carriage
);

/** @brief The first byte that is not a control character. */
#define HTTP_PARSER_BYTE_CONTROL_END	0x20
/** @brief DEL. */
#define HTTP_PARSER_BYTE_DELETE			0x7F

/** @brief Loads 8 bytes in a word, its first byte in its lowest lane. */
static inline uint64_t http_parser_word_load (const char *bytes) {

	uint64_t word = 0;
	(void) memcpy (&word, bytes, HTTP_PARSER_WORD_SIZE);

	#if defined (__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
	word = __builtin_bswap64 (word);
	#endif

	return word;

}

/** @brief The high bit of the lanes of a word that are a byte; the lowest one that is set is the first one. */
static inline uint64_t http_parser_word_equal (const uint64_t word, const uint8_t byte) {

	const uint64_t difference = word ^ (HTTP_PARSER_WORD_ONES * byte);

	return (difference - HTTP_PARSER_WORD_ONES) & ~difference & HTTP_PARSER_WORD_HIGHS;

}

/** @brief The high bit of the lanes of a word that are below a byte (at most 0x80); the lowest one that is set is the first one. */
static inline uint64_t http_parser_word_below (const uint64_t word, const uint8_t byte) {

	return (word - (HTTP_PARSER_WORD_ONES * byte)) & ~word & HTTP_PARSER_WORD_HIGHS;

}

/** @brief The lane of the lowest high bit of a mask. */
static inline size_t http_parser_word_first (const uint64_t mask) {

	return (size_t) __builtin_ctzll (mask) / HTTP_PARSER_WORD_BITS;

}

/** @brief The first byte of the visible ASCII (VCHAR, RFC 5234 appendix B.1). */
#define HTTP_PARSER_BYTE_VISIBLE_FIRST	0x21
/** @brief The last byte of the visible ASCII. */
#define HTTP_PARSER_BYTE_VISIBLE_LAST	0x7E

/*
 * The kernels of 16 bytes: NEON in arm64 and SSE2 in x86_64, the base of each architecture (no dispatch);
 * CERVER_HTTP_PARSER_SCALAR leaves only the words of 8 bytes, the kernels of any other architecture.
 * A mask has the lanes that matched, the lowest one first.
 */

#if !defined (CERVER_HTTP_PARSER_SCALAR) && defined (__aarch64__) && defined (__ARM_NEON)

/** @brief The kernels of 16 bytes are compiled. */
#define HTTP_PARSER_VECTOR

/** @brief The bytes of a vector. */
#define HTTP_PARSER_VECTOR_SIZE			16
/** @brief The bits of each lane in the mask of a vector: NEON narrows each byte to 4 bits. */
#define HTTP_PARSER_VECTOR_LANE_BITS	4
/** @brief The lane of 64 bits of the narrowed vector. */
#define HTTP_PARSER_VECTOR_MASK_LANE	0

/** @brief The mask of a vector of lanes that are 0xFF or 0. */
static inline uint64_t http_parser_vector_mask (const uint8x16_t lanes) {

	const uint8x8_t narrowed = vshrn_n_u16 (vreinterpretq_u16_u8 (lanes), HTTP_PARSER_VECTOR_LANE_BITS);

	return vget_lane_u64 (vreinterpret_u64_u8 (narrowed), HTTP_PARSER_VECTOR_MASK_LANE);

}

/** @brief The bytes of 16 that are not of a value: below SP (a HTAB, a CTL) or DEL. */
static inline uint64_t http_parser_vector_value (const char *bytes) {

	const uint8x16_t lanes = vld1q_u8 ((const uint8_t *) bytes);

	const uint8x16_t invalid = vorrq_u8 (
		vcltq_u8 (lanes, vdupq_n_u8 (HTTP_PARSER_BYTE_CONTROL_END)),
		vceqq_u8 (lanes, vdupq_n_u8 (HTTP_PARSER_BYTE_DELETE))
	);

	return http_parser_vector_mask (invalid);

}

/** @brief The bytes of 16 that are not visible ASCII: one compare after moving '!' to 0. */
static inline uint64_t http_parser_vector_invisible (const char *bytes) {

	const uint8x16_t lanes = vld1q_u8 ((const uint8_t *) bytes);
	const uint8x16_t moved = vsubq_u8 (lanes, vdupq_n_u8 (HTTP_PARSER_BYTE_VISIBLE_FIRST));

	return http_parser_vector_mask (
		vcgtq_u8 (moved, vdupq_n_u8 (HTTP_PARSER_BYTE_VISIBLE_LAST - HTTP_PARSER_BYTE_VISIBLE_FIRST))
	);

}

/** @brief The kernel of the tokens is compiled. */
#define HTTP_PARSER_VECTOR_TOKEN

/** @brief The bytes of 16 that are not of a token: the class of the low nibble of each one has the bit of its high one. */
static inline uint64_t http_parser_vector_untoken (const char *bytes) {

	const uint8x16_t lanes = vld1q_u8 ((const uint8_t *) bytes);

	const uint8x16_t low = vqtbl1q_u8 (
		vld1q_u8 (http_parser_token_low), vandq_u8 (lanes, vdupq_n_u8 (HTTP_PARSER_NIBBLE_MASK))
	);

	const uint8x16_t high = vqtbl1q_u8 (vld1q_u8 (http_parser_token_high), vshrq_n_u8 (lanes, HTTP_PARSER_NIBBLE_BITS));

	return http_parser_vector_mask (vceqzq_u8 (vandq_u8 (low, high)));

}

#elif !defined (CERVER_HTTP_PARSER_SCALAR) && defined (__SSE2__)

/** @brief The kernels of 16 bytes are compiled. */
#define HTTP_PARSER_VECTOR

/** @brief The bytes of a vector. */
#define HTTP_PARSER_VECTOR_SIZE			16
/** @brief The bits of each lane in the mask of a vector: SSE2 moves the high bit of each byte. */
#define HTTP_PARSER_VECTOR_LANE_BITS	1

/** @brief The bytes of 16 that are not of a value: below SP (a HTAB, a CTL) or DEL. */
static inline uint64_t http_parser_vector_value (const char *bytes) {

	const __m128i lanes = _mm_loadu_si128 ((const __m128i *) bytes);

	// unsigned: a byte is below SP when the minimum with the last control character is itself
	const __m128i control = _mm_cmpeq_epi8 (
		_mm_min_epu8 (lanes, _mm_set1_epi8 (HTTP_PARSER_BYTE_CONTROL_END - 1)), lanes
	);

	const __m128i delete_lanes = _mm_cmpeq_epi8 (lanes, _mm_set1_epi8 (HTTP_PARSER_BYTE_DELETE));

	return (uint64_t) (unsigned int) _mm_movemask_epi8 (_mm_or_si128 (control, delete_lanes));

}

/** @brief The bytes of 16 that are not visible ASCII: one compare after moving '!' to 0. */
static inline uint64_t http_parser_vector_invisible (const char *bytes) {

	const __m128i lanes = _mm_loadu_si128 ((const __m128i *) bytes);
	const __m128i moved = _mm_sub_epi8 (lanes, _mm_set1_epi8 (HTTP_PARSER_BYTE_VISIBLE_FIRST));

	// unsigned: a byte is above the range when the maximum with the first one out of it is itself
	const __m128i outside = _mm_set1_epi8 (HTTP_PARSER_BYTE_VISIBLE_LAST - HTTP_PARSER_BYTE_VISIBLE_FIRST + 1);

	return (uint64_t) (unsigned int) _mm_movemask_epi8 (_mm_cmpeq_epi8 (_mm_max_epu8 (moved, outside), moved));

}

#if defined (__SSSE3__)

/** @brief The kernel of the tokens is compiled: SSSE3 has the shuffle of bytes, SSE2 does not. */
#define HTTP_PARSER_VECTOR_TOKEN

/** @brief The bytes of 16 that are not of a token: the class of the low nibble of each one has the bit of its high one. */
static inline uint64_t http_parser_vector_untoken (const char *bytes) {

	const __m128i lanes = _mm_loadu_si128 ((const __m128i *) bytes);
	const __m128i nibble = _mm_set1_epi8 (HTTP_PARSER_NIBBLE_MASK);

	const __m128i low = _mm_shuffle_epi8 (
		_mm_loadu_si128 ((const __m128i *) http_parser_token_low), _mm_and_si128 (lanes, nibble)
	);

	const __m128i high = _mm_shuffle_epi8 (
		_mm_loadu_si128 ((const __m128i *) http_parser_token_high),
		_mm_and_si128 (_mm_srli_epi16 (lanes, HTTP_PARSER_NIBBLE_BITS), nibble)
	);

	return (uint64_t) (unsigned int) _mm_movemask_epi8 (
		_mm_cmpeq_epi8 (_mm_and_si128 (low, high), _mm_setzero_si128 ())
	);

}

#endif

#endif

#if defined (HTTP_PARSER_VECTOR)

/** @brief The lane of the lowest bit of the mask of a vector. */
static inline size_t http_parser_vector_first (const uint64_t mask) {

	return (size_t) __builtin_ctzll (mask) / HTTP_PARSER_VECTOR_LANE_BITS;

}

#endif

/**
 * @brief Finds the first byte that is not visible ASCII: a SP, a CTL, DEL or a byte with its high bit.
 *
 * @return its offset from start, or length when there is none
 */
static inline size_t http_parser_scan_visible (const char *start, const size_t length) {

	size_t i = 0;

	#if defined (HTTP_PARSER_VECTOR)
	for (; (i + HTTP_PARSER_VECTOR_SIZE) <= length; i += HTTP_PARSER_VECTOR_SIZE) {
		const uint64_t mask = http_parser_vector_invisible (start + i);
		if (mask) return i + http_parser_vector_first (mask);
	}
	#endif

	for (; (i + HTTP_PARSER_WORD_SIZE) <= length; i += HTTP_PARSER_WORD_SIZE) {
		const uint64_t word = http_parser_word_load (start + i);
		const uint64_t mask = http_parser_word_below (word, HTTP_PARSER_BYTE_VISIBLE_FIRST)
			| http_parser_word_equal (word, HTTP_PARSER_BYTE_DELETE) | (word & HTTP_PARSER_WORD_HIGHS);

		if (mask) return i + http_parser_word_first (mask);
	}

	for (; i < length; i++) {
		const unsigned char byte = (unsigned char) start[i];
		if ((byte < HTTP_PARSER_BYTE_VISIBLE_FIRST) || (byte > HTTP_PARSER_BYTE_VISIBLE_LAST)) return i;
	}

	return length;

}

/**
 * @brief Finds the first byte that is not of a token (RFC 9110 section 5.6.2): the end of a name.
 *
 * @return its offset from start, or length when there is none
 */
static inline size_t http_parser_scan_token (const char *start, const size_t length) {

	size_t i = 0;

	#if defined (HTTP_PARSER_VECTOR_TOKEN)
	for (; (i + HTTP_PARSER_VECTOR_SIZE) <= length; i += HTTP_PARSER_VECTOR_SIZE) {
		const uint64_t mask = http_parser_vector_untoken (start + i);
		if (mask) return i + http_parser_vector_first (mask);
	}
	#endif

	while ((i < length) && http_parser_is ((unsigned char) start[i], HTTP_PARSER_CLASS_TOKEN)) i++;

	return i;

}

/**
 * @brief Finds the first byte of a value that is not VCHAR, SP or obs-text: a HTAB, a CTL, DEL or the end of the
 * line.
 *
 * @return its offset from start, or length when there is none
 */
static inline size_t http_parser_scan_value (const char *start, const size_t length) {

	size_t i = 0;

	#if defined (HTTP_PARSER_VECTOR)
	for (; (i + HTTP_PARSER_VECTOR_SIZE) <= length; i += HTTP_PARSER_VECTOR_SIZE) {
		const uint64_t mask = http_parser_vector_value (start + i);
		if (mask) return i + http_parser_vector_first (mask);
	}
	#endif

	for (; (i + HTTP_PARSER_WORD_SIZE) <= length; i += HTTP_PARSER_WORD_SIZE) {
		const uint64_t word = http_parser_word_load (start + i);
		const uint64_t mask = http_parser_word_below (word, HTTP_PARSER_BYTE_CONTROL_END)
			| http_parser_word_equal (word, HTTP_PARSER_BYTE_DELETE);

		if (mask) return i + http_parser_word_first (mask);
	}

	for (; i < length; i++) {
		const unsigned char byte = (unsigned char) start[i];
		if ((byte < HTTP_PARSER_BYTE_CONTROL_END) || (byte == HTTP_PARSER_BYTE_DELETE)) return i;
	}

	return length;

}

/** @brief Compares a name with a lower case one, case insensitive. */
CERVER_HIDDEN bool http_parser_name_equal (
	const char *name, const size_t length, const char *lower, const size_t lower_length
);

/** @brief The special fields: the ones that change how a message is read. */
typedef enum HttpParserSpecial {

	/** @brief Any other field. */
	HTTP_PARSER_SPECIAL_NONE				= 0,
	/** @brief Connection or Proxy-Connection. */
	HTTP_PARSER_SPECIAL_CONNECTION			= 1,
	/** @brief Content-Length. */
	HTTP_PARSER_SPECIAL_CONTENT_LENGTH		= 2,
	/** @brief Transfer-Encoding. */
	HTTP_PARSER_SPECIAL_TRANSFER_ENCODING	= 3,
	/** @brief Upgrade. */
	HTTP_PARSER_SPECIAL_UPGRADE				= 4

} HttpParserSpecial;

/** @brief Returns the special field of a name (Proxy-Connection is a Connection). */
CERVER_HIDDEN HttpParserSpecial http_parser_special (
	const char *name, const size_t length, const http_header type
);

/** @brief The bytes of the name Proxy-Connection, the only special field that is not a known header. */
#define HTTP_PARSER_PROXY_CONNECTION_LENGTH		16

/**
 * @brief Returns whether a field can be a special one, without reading its name: its known header, or the length
 * of Proxy-Connection; http_parser_special () decides once the head is complete.
 */
static inline bool http_parser_special_candidate (const CerverHttpField *field) {

	bool candidate = false;

	switch (field->type) {
		case HTTP_HEADER_CONNECTION:
		case HTTP_HEADER_CONTENT_LENGTH:
		case HTTP_HEADER_TRANSFER_ENCODING:
		case HTTP_HEADER_UPGRADE:
			candidate = true;
			break;

		default:
			candidate = (field->name.length == HTTP_PARSER_PROXY_CONNECTION_LENGTH);
			break;
	}

	return candidate;

}

/**
 * @brief Applies the value of a special field to the flags of a head (the folded line breaks are spaces).
 *
 * @return CERVER_HTTP_PARSER_ERROR_NONE, or the error of the value
 */
CERVER_HIDDEN CerverHttpParserError http_parser_special_value (
	CerverHttpParser *parser, const HttpParserSpecial special,
	const char *value, const size_t length
);

/** @brief Counts a field (or a trailer) against the limits: its name and its value. */
static inline CerverHttpParserError http_parser_count_field (
	CerverHttpParser *parser, const size_t bytes
) {

	CerverHttpHead *head = &parser->head;

	head->fields_size += bytes;

	const size_t fields_size = parser->limits.fields_size;

	return (fields_size && (head->fields_size > fields_size)) ?
		CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE : CERVER_HTTP_PARSER_ERROR_NONE;

}

/** @brief Sets the error of a parser. */
CERVER_HIDDEN CerverHttpParserResult http_parser_fail (
	CerverHttpParser *parser, const CerverHttpParserError error
);

/** @brief Decides how the body of a head is read, once the head is complete. */
CERVER_HIDDEN CerverHttpParserResult http_parser_head_complete (
	CerverHttpParser *parser
);

#ifdef __cplusplus
}
#endif

#endif
