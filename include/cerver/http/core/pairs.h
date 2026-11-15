#ifndef _CERVER_HTTP_CORE_PAIRS_H_
#define _CERVER_HTTP_CORE_PAIRS_H_

#include "cerver/config.h"

#include "cerver/http/core/http.h"

#ifdef __cplusplus
extern "C" {
#endif

#pragma region kvp

typedef struct KeyValuePair { 

	String *key;
	String *value;

} KeyValuePair;

CERVER_PUBLIC KeyValuePair *key_value_pair_new (void);

CERVER_PUBLIC void key_value_pair_delete (void *kvp_ptr);

CERVER_PUBLIC KeyValuePair *key_value_pair_create (
	const char *key, const char *value
);

CERVER_PUBLIC const String *key_value_pairs_get_value (
	const DoubleList *pairs, const char *key
);

CERVER_PUBLIC void key_value_pairs_print (
	const DoubleList *pairs
);

#pragma endregion
#pragma region parser

// parses a url query into key value pairs for better manipulation
CERVER_PUBLIC DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
);

// gets the matching value for the requested key from a list of pairs
CERVER_PUBLIC const String *http_query_pairs_get_value (
	const DoubleList *pairs, const char *key
);

CERVER_PUBLIC void http_query_pairs_print (
	const DoubleList *pairs
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif
