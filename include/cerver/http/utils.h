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
	XX(3,	FULL, 		Full)

typedef enum RangeType {

	#define XX(num, name, string) HTTP_RANGE_TYPE_##name = num,
	HTTP_RANGE_TYPE_MAP (XX)
	#undef XX

} RangeType;

CERVER_PUBLIC const char *http_range_type_string (
	const RangeType range_type
);

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