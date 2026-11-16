#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>

// a JSON that is loaded is dumped and loaded again with the same value,
// the first byte chooses the flags of the load, and the values are unpacked

#define JSON_FUZZ_DUMP_SIZE			(1 << 12)

static const size_t json_fuzz_flags[] = {
	JSON_DECODE_ANY,
	JSON_DECODE_ANY | JSON_REJECT_DUPLICATES,
	JSON_DECODE_ANY | JSON_ALLOW_NUL,
	JSON_DECODE_ANY | JSON_DECODE_INT_AS_REAL,
	0
};

#define JSON_FUZZ_FLAGS_SIZE		(sizeof (json_fuzz_flags) / sizeof (json_fuzz_flags[0]))

static void json_fuzz_round_trip (const json_t *json) {

	json_error_t error;

	char *dumped = json_dumps (json, JSON_ENCODE_ANY | JSON_COMPACT);
	if (!dumped) return;

	// the compact dump has fewer bytes than an input with spaces,
	// so it can need more memory for each byte than the limit gives
	json_set_parser_memory_factor (0);
	json_t *loaded = json_loads (dumped, JSON_DECODE_ANY | JSON_ALLOW_NUL, &error);
	json_set_parser_memory_factor (JSON_PARSER_DEFAULT_MEMORY_FACTOR);

	if (!loaded || !json_equal (json, loaded)) abort ();

	// a dump into a buffer has the same bytes, or the size that it needs
	char buffer[JSON_FUZZ_DUMP_SIZE];
	const size_t length = json_dumpb (json, buffer, sizeof (buffer), JSON_ENCODE_ANY | JSON_COMPACT);
	if (length != strlen (dumped)) abort ();
	if ((length <= sizeof (buffer)) && memcmp (buffer, dumped, length)) abort ();

	json_decref (loaded);
	free (dumped);

}

// the optional values of an object, with any of their types
static void json_fuzz_unpack (json_t *json) {

	const char *string = NULL;
	size_t string_length = 0;
	int integer = 0;
	json_int_t large = 0;
	double real = 0;
	json_t *object = NULL;

	(void) json_unpack (json, "{s?s%}", "string", &string, &string_length);
	(void) json_unpack (json, "{s?i}", "integer", &integer);
	(void) json_unpack (json, "{s?I}", "integer", &large);
	(void) json_unpack (json, "{s?F}", "real", &real);
	(void) json_unpack (json, "{s?o}", "object", &object);
	(void) json_unpack (json, "[i, s, b]", &integer, &string, &integer);

	json_t *packed = json_pack ("{s:O, s:i}", "value", json, "integer", integer);
	json_decref (packed);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (!size) return 0;

	const size_t flags = json_fuzz_flags[data[0] % JSON_FUZZ_FLAGS_SIZE];

	json_error_t error;
	json_t *json = json_loadb ((const char *) data + 1, size - 1, flags, &error);
	if (!json) return 0;

	json_fuzz_round_trip (json);

	json_fuzz_unpack (json);

	json_decref (json);

	return 0;

}
