#include <stdlib.h>
#include <string.h>

#include "cerver/types/string.h"

#include "cerver/collections/arena.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/internal.h"

static const char *http_custom_header_empty_value = { "" };

// creates a custom header with an empty value in the arena
HttpCustomHeader *http_custom_header_create (
	Arena *arena, const char *header, const size_t header_len
) {

	HttpCustomHeader *custom_header = (HttpCustomHeader *) arena_alloc (
		arena, sizeof (HttpCustomHeader)
	);

	if (custom_header) {
		custom_header->header = str_create_from_bytes_arena (arena, header, header_len);
		custom_header->value = str_create_from_bytes_arena (arena, http_custom_header_empty_value, 0);
		custom_header->next = NULL;

		if (!custom_header->header || !custom_header->value) custom_header = NULL;
	}

	return custom_header;

}

// sets the value of the custom header, a new string in the arena
// returns 0 on success, 1 on error
unsigned int http_custom_header_set_value (
	Arena *arena, HttpCustomHeader *custom_header,
	const char *value, const size_t value_len
) {

	unsigned int retval = 1;

	String *created = str_create_from_bytes_arena (arena, value, value_len);
	if (created) {
		custom_header->value = created;
		retval = 0;
	}

	return retval;

}
