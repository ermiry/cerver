#ifndef _CERVER_HTTP_PARSER_H_
#define _CERVER_HTTP_PARSER_H_

#include "cerver/collections/dllist.h"

/*** Request ***/

/* contains name and value of a header (name == NULL if is a continuing line
 * of a multiline header */
struct phr_header {

    const char *name;
    size_t name_len;
    const char *value;
    size_t value_len;
    
};


/* returns number of bytes consumed if successful, -2 if request is partial,
 * -1 if failed */
int phr_parse_request (const char *buf, size_t len, const char **method, size_t *method_len, const char **path, size_t *path_len,
                      int *minor_version, struct phr_header *headers, size_t *num_headers, size_t last_len);

/* ditto */
int phr_parse_response (const char *_buf, size_t len, int *minor_version, int *status, const char **msg, size_t *msg_len,
                       struct phr_header *headers, size_t *num_headers, size_t last_len);

/* ditto */
int phr_parse_headers (const char *buf, size_t len, struct phr_header *headers, size_t *num_headers, size_t last_len);

/*** Query ***/

typedef struct KeyValuePair { char *key, *value; } KeyValuePair;

extern DoubleList *http_parse_query_into_pairs (const char *first, const char *last);
extern char *http_strip_path_from_query (char *str);

extern const char *http_query_pairs_get_value (DoubleList *pairs, const char *key);

#endif