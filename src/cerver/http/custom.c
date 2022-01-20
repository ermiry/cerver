#include <stdlib.h>
#include <string.h>

#include "cerver/http/custom.h"

HttpCustomHeader *http_custom_header_new (void) {

	HttpCustomHeader *custom_header = (HttpCustomHeader *) malloc (
		sizeof (HttpCustomHeader)
	);

	if (custom_header) {
		(void) memset (custom_header, 0, sizeof (HttpCustomHeader));
	}

	return custom_header;

}

void http_custom_header_delete (void *custom_header_ptr) {

	if (custom_header_ptr) {
		free (custom_header_ptr);
	}

}

HttpCustomHeader *http_custom_header_create (const char *header) {

	HttpCustomHeader *custom_header = http_custom_header_new ();
	if (custom_header) {
		(void) strncpy (custom_header->header, header, HTTP_CUSTOM_HEADER_SIZE - 1);
		custom_header->header_len = (unsigned int) strlen (custom_header->header);
	}

	return custom_header;

}
