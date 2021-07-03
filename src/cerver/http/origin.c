#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "cerver/http/origin.h"

HttpOrigin *http_origin_new (void) {

	HttpOrigin *origin = (HttpOrigin *) malloc (sizeof (HttpOrigin));
	if (origin) {
		(void) memset (origin, 0, sizeof (HttpOrigin));
	}

	return origin;

}

void http_origin_delete (void *origin_ptr) {

	if (origin_ptr) free (origin_ptr);

}

void http_origin_init (
	HttpOrigin *origin, const char *value
) {

	if (origin && value) {
		(void) strncpy (
			origin->value,
			value,
			HTTP_ORIGIN_VALUE_SIZE - 1
		);

		origin->len = (int) strlen (origin->value);
	}

}

void http_origin_print (const HttpOrigin *origin) {

	if (origin) {
		(void) printf ("(%d) %s\n", origin->len, origin->value);
	}

}
