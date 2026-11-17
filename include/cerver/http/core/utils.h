/**
 * @file cerver/http/core/utils.h
 * @brief The byte ranges of the requests of a file.
 */

#ifndef _CERVER_HTTP_UTILS_H_
#define _CERVER_HTTP_UTILS_H_

#include <stdbool.h>

#include "cerver/config.h"

/**
 * @defgroup http_ranges Ranges
 * @ingroup http_core
 * @brief The Range of a request (RFC 9110 section 14).
 * @{
 */

/**
 * @brief The size of the value of a bytes range.
 *
 * @note cerver does not use it.
 */
#define HTTP_BYTES_RANGE_VALUE_SIZE		64

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The X-macro table of the types of a range: the number, the name and the string of each one.
 */
#define HTTP_RANGE_TYPE_MAP(XX)		\
	XX(0,	NONE, 		None)		\
	XX(1,	EMPTY, 		Empty)		\
	XX(2,	FIRST, 		First)		\
	XX(3,	FULL, 		Full)		\
	XX(4,	SUFFIX, 	Suffix)

/**
 * @brief The type of a range, one `HTTP_RANGE_TYPE_<name>` for each row of #HTTP_RANGE_TYPE_MAP.
 */
typedef enum RangeType {

	#define XX(num, name, string) HTTP_RANGE_TYPE_##name = num,
	HTTP_RANGE_TYPE_MAP (XX)
	#undef XX

} RangeType;

/**
 * @brief Returns the string of a range type.
 *
 * @param[in] range_type the type
 * @return the string of its row in #HTTP_RANGE_TYPE_MAP, or "Undefined" for a value out of the table; a static
 * string that must not be freed
 * @since 2.1b-21 (it was http_range_type_string ())
 */
CERVER_PUBLIC const char *http_range_type_to_string (
	const RangeType range_type
);

/**
 * @brief The range of a request.
 *
 * bytes=0- is EMPTY, bytes=N- is FIRST, bytes=N-M is FULL and bytes=-N is SUFFIX: the last N bytes of the file, N
 * is its end and its start is 0 until the range is resolved with the size of the file.
 */
struct _BytesRange {

	/** @brief The size of the file. */
	long file_size;

	/** @brief The type of the range. */
	RangeType type;
	/** @brief The first byte of the range. */
	long start, end; /**< @brief The last byte of the range. */
	/** @brief The bytes of the range, its Content-Length. */
	long chunk_size;

};

/** @brief A range, see struct _BytesRange. */
typedef struct _BytesRange BytesRange;

/**
 * @brief Sets every value of a range to zero and its type to HTTP_RANGE_TYPE_NONE.
 *
 * @param[out] bytes_range the range, it must not be NULL
 */
CERVER_PUBLIC void http_bytes_range_init (
	BytesRange *bytes_range
);

/**
 * @brief Prints the values of a range to stdout.
 *
 * @param[in] bytes_range the range, it must not be NULL
 */
CERVER_PUBLIC void http_bytes_range_print (
	const BytesRange *bytes_range
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
