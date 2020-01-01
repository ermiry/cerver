#ifndef _CERVER_HTTP_PARSER_H_
#define _CERVER_HTTP_PARSER_H_

#include "cerver/collections/dllist.h"

struct phr_header {

	const char *name;
	size_t name_len;
	const char *value;
	size_t value_len;
	
};

extern int phr_parse_request (const char *buf_start, size_t len, const char **method, size_t *method_len, const char **path,
	size_t *path_len, int *minor_version, struct phr_header *headers, size_t *num_headers, size_t last_len);

/*** Query ***/

typedef struct KeyValuePair { char *key, *value; } KeyValuePair;

// parses a url query into key value pairs for better manipulation
extern DoubleList *http_parse_query_into_pairs (const char *first, const char *last);

// Splits the url path from the actual query
extern char *http_strip_path_from_query (char *str);

// gets the matching value for the requested key from a list of pairs
extern const char *http_query_pairs_get_value (DoubleList *pairs, const char *key);

#endif