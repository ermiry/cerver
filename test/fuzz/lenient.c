#include <stdint.h>
#include <stddef.h>

#include "fuzz.h"

// raw bytes to http_receive_handle () with the lenient flags of the parser:
// the first byte selects the size of the pieces the request is split in,
// the next two bytes select the lenient flags (HttpParserLenient)

#define FUZZ_LENIENT_HEADER_SIZE		3
#define FUZZ_LENIENT_HIGH_SHIFT			8

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < FUZZ_LENIENT_HEADER_SIZE) return 0;

	fuzz_cerver_create ();

	// set without the setter, that warns about each flag
	HttpCerver *http_cerver = (HttpCerver *) fuzz_cerver->cerver_data;
	http_cerver->parser_lenient = (
		(unsigned int) data[1] | ((unsigned int) data[2] << FUZZ_LENIENT_HIGH_SHIFT)
	) & HTTP_PARSER_LENIENT_ALL;

	fuzz_feed (data + FUZZ_LENIENT_HEADER_SIZE, size - FUZZ_LENIENT_HEADER_SIZE, (size_t) data[0]);

	return 0;

}
