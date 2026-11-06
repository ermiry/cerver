#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include <cerver/http/url.h>

// the percent-decoding of a whole value (query and urlencoded bodies) and of a path segment

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	char *value = (char *) malloc (size + 1);
	if (!value) return 0;

	(void) memcpy (value, data, size);
	value[size] = '\0';

	char *decoded = http_url_decode (value);
	free (decoded);

	// a destination of the exact size, a path segment never grows when it is decoded
	char *segment = (char *) malloc (size ? size : 1);
	if (segment) {
		const size_t decoded_len = http_url_decode_path (segment, (const char *) data, size);
		if (decoded_len > size) abort ();

		free (segment);
	}

	free (value);

	return 0;

}
