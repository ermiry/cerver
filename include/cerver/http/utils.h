#ifndef _CERVER_HTTP_UTILS_H_
#define _CERVER_HTTP_UTILS_H_

#include <stdbool.h>

#include "cerver/config.h"

#define HTTP_BYTES_RANGE_VALUE_SIZE		64

#ifdef __cplusplus
extern "C" {
#endif

#define HTTP_RANGE_TYPE_MAP(XX)		\
	XX(0,	NONE, 		None)		\
	XX(1,	EMPTY, 		Empty)		\
	XX(2,	FIRST, 		First)		\
	XX(3,	FULL, 		Full)		\
	XX(4,	SUFFIX, 	Suffix)

typedef enum RangeType {

	#define XX(num, name, string) HTTP_RANGE_TYPE_##name = num,
	HTTP_RANGE_TYPE_MAP (XX)
	#undef XX

} RangeType;

CERVER_PUBLIC const char *http_range_type_string (
	const RangeType range_type
);

// the range of a request: bytes=0- (EMPTY), bytes=N- (FIRST), bytes=N-M (FULL)
// and bytes=-N (SUFFIX, the last N bytes of the file: N is its end and its start is 0
// until the range is resolved with the size of the file)
struct _BytesRange {

	long file_size;

	RangeType type;
	long start, end;
	long chunk_size;

};

typedef struct _BytesRange BytesRange;

CERVER_PUBLIC void http_bytes_range_init (
	BytesRange *bytes_range
);

CERVER_PUBLIC void http_bytes_range_print (
	const BytesRange *bytes_range
);

#ifdef __cplusplus
}
#endif

#endif
