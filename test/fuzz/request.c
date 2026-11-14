#include <stddef.h>
#include <stdint.h>

#include "fuzz.h"

// raw bytes to http_receive_handle (): the parser, the headers, the limits,
// the routes tree, multi-part and the authentication
// the first byte selects the size of the pieces the request is split in

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < 1) return 0;

	fuzz_cerver_create ();

	fuzz_feed (data + 1, size - 1, (size_t) data[0]);

	return 0;

}
