#include "cerver/config.h"

#include "cerver/http/json/config.h"
#include "cerver/http/json/json.h"

CERVER_INLINE json_t *json_incref (json_t *json) {

	if (json && json->refcount != (size_t) - 1)
		JSON_INTERNAL_INCREF(json);
	
	return json;

}

CERVER_INLINE void json_decref (json_t *json) {

	if (
		json
		&& json->refcount != (size_t) - 1
		&& JSON_INTERNAL_DECREF(json) == 0
	) {
		json_delete(json);
	}

}

#if defined(__GNUC__) || defined(__clang__)
CERVER_INLINE void json_decrefp (json_t **json) {

	if (json) {
		json_decref(*json);
		*json = NULL;
	}

}

#endif