#ifndef _CERVER_HTTP_PARSER_H_
#define _CERVER_HTTP_PARSER_H_

#include "cerver/collections/dllist.h"

/*** Query ***/

typedef struct KeyValuePair { char *key, *value; } KeyValuePair;

// parses a url query into key value pairs for better manipulation
extern DoubleList *http_parse_query_into_pairs (const char *first, const char *last);

// Splits the url path from the actual query
extern char *http_strip_path_from_query (char *str);

// gets the matching value for the requested key from a list of pairs
extern const char *http_query_pairs_get_value (DoubleList *pairs, const char *key);

#endif