#include <stdlib.h>
#include <stdio.h>

#include "cerver/http/utils.h"

const char *http_range_type_string (
	const RangeType range_type
) {

	switch (range_type) {
		#define XX(num, name, string) case HTTP_RANGE_TYPE_##name: return #string;
		HTTP_RANGE_TYPE_MAP(XX)
		#undef XX
	}

	return http_range_type_string (HTTP_RANGE_TYPE_NONE);

}

void http_bytes_range_init (BytesRange *bytes_range) {

	bytes_range->file_size = 0;

	bytes_range->type = HTTP_RANGE_TYPE_NONE;
	bytes_range->start = 0;
	bytes_range->end = 0;
	bytes_range->chunk_size = 0;

}

void http_bytes_range_print (const BytesRange *bytes_range) {

	(void) printf ("Bytes Range: \n");
	(void) printf ("\tFile size: %ld", bytes_range->file_size);
	(void) printf ("\tType: %s", http_range_type_string (bytes_range->type));
	(void) printf ("\tStart: %ld", bytes_range->start);
	(void) printf ("\tEnd: %ld", bytes_range->end);
	(void) printf ("\tChunk size: %ld", bytes_range->chunk_size);

}
