#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/serializer.h>

#include <cerver/collections/arena.h>

// an object from the network, decoded with each schema of cerver:
// the first byte selects the schema and the rest is the object
// a buffer that is not valid never changes the object,
// and a valid one encodes again to a buffer that decodes to the same encoding

#define FUZZ_SERIALIZER_SELECTOR_SIZE		1
#define FUZZ_SERIALIZER_GARBAGE				0xa5

static bool fuzz_serializer_untouched (const uint8_t *object, const size_t size) {

	bool untouched = true;
	for (size_t i = 0; (i < size) && untouched; i++) {
		untouched = (object[i] == FUZZ_SERIALIZER_GARBAGE);
	}

	return untouched;

}

// the object encodes again, and its encoding decodes and encodes to the same bytes
static void fuzz_serializer_round_trip (
	const SerializerSchema *schema, const void *object, Arena *arena
) {

	const size_t size = serializer_encoded_size (schema, object);
	if (!size) abort ();

	uint8_t *first = (uint8_t *) malloc (size);
	uint8_t *second = (uint8_t *) malloc (size);
	void *copy = malloc (schema->size);

	if (first && second && copy) {
		size_t written = 0;
		if (serializer_encode (schema, object, first, size, &written) != SERIALIZER_RESULT_NONE) abort ();
		if (written != size) abort ();

		if (serializer_decode (schema, first, size, arena, copy) != SERIALIZER_RESULT_NONE) abort ();
		if (serializer_encode (schema, copy, second, size, &written) != SERIALIZER_RESULT_NONE) abort ();
		if ((written != size) || memcmp (first, second, size)) abort ();
	}

	free (copy);
	free (second);
	free (first);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	if (size < FUZZ_SERIALIZER_SELECTOR_SIZE) return 0;

	static Arena *arena = NULL;
	if (!arena) arena = arena_create (0);
	if (!arena) return 0;

	size_t count = 0;
	const SerializerSchema *const *schemas = serializer_schemas (&count);
	if (!count) return 0;

	const SerializerSchema *schema = schemas[data[0] % count];

	// the object is copied to a buffer of its exact size, so a read past it is found
	const size_t object_size = size - FUZZ_SERIALIZER_SELECTOR_SIZE;
	uint8_t *buffer = (uint8_t *) malloc (object_size ? object_size : 1);
	uint8_t *object = (uint8_t *) malloc (schema->size);

	if (buffer && object) {
		(void) memcpy (buffer, data + FUZZ_SERIALIZER_SELECTOR_SIZE, object_size);
		(void) memset (object, FUZZ_SERIALIZER_GARBAGE, schema->size);

		const SerializerResult result = serializer_decode (schema, buffer, object_size, arena, object);

		if (result == SERIALIZER_RESULT_NONE) fuzz_serializer_round_trip (schema, object, arena);
		else if (!fuzz_serializer_untouched (object, schema->size)) abort ();
	}

	free (object);
	free (buffer);

	arena_reset (arena);

	return 0;

}
