#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "fuzz.h"

// the body of a multi-part request with a fixed boundary,
// so every input reaches the multi-part parser and the uploads
// the first byte selects the size of the pieces the request is split in

#define MULTIPART_HEADERS_SIZE			(1 << 9)

// a literal, so the compiler checks its arguments
#define MULTIPART_HEADERS_FORMAT												\
	"POST /api/upload HTTP/1.1\r\n"												\
	"Host: localhost\r\n"														\
	"Content-Type: multipart/form-data; boundary=FuzzBoundary\r\n"				\
	"Content-Length: %zu\r\n"													\
	"\r\n"

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < 1) return 0;

	fuzz_cerver_create ();

	const uint8_t *body = data + 1;
	const size_t body_len = size - 1;

	char headers[MULTIPART_HEADERS_SIZE];
	const int headers_len = snprintf (headers, MULTIPART_HEADERS_SIZE, MULTIPART_HEADERS_FORMAT, body_len);
	if ((headers_len < 0) || (headers_len >= MULTIPART_HEADERS_SIZE)) return 0;

	const size_t request_len = (size_t) headers_len + body_len;
	uint8_t *request = (uint8_t *) malloc (request_len);
	if (!request) return 0;

	(void) memcpy (request, headers, (size_t) headers_len);
	(void) memcpy (request + headers_len, body, body_len);

	fuzz_feed (request, request_len, (size_t) data[0]);

	free (request);

	return 0;

}
