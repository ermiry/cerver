#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>

#include <cerver/http/json/json.h>

// a JSON that is loaded is dumped and loaded again with the same value

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	json_error_t error;
	json_t *json = json_loadb ((const char *) data, size, JSON_DECODE_ANY, &error);
	if (!json) return 0;

	char *dumped = json_dumps (json, JSON_ENCODE_ANY | JSON_COMPACT);
	if (dumped) {
		json_t *loaded = json_loads (dumped, JSON_DECODE_ANY, &error);
		if (!loaded || !json_equal (json, loaded)) abort ();

		json_decref (loaded);
		free (dumped);
	}

	json_decref (json);

	return 0;

}
