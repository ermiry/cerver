/**
 * @file cerver/json/pointer.h
 * @brief The queries by pointer (RFC 6901) into a document of the JSON of cerver.
 */

#ifndef _CERVER_JSON_POINTER_H_
#define _CERVER_JSON_POINTER_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/json/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup json_pointer Pointer
 * @ingroup json
 * @brief A value found by its path from another value (RFC 6901).
 * @{
 */

/**
 * @brief Returns the value that a pointer names, from a value.
 *
 * A pointer is a sequence of tokens, each one after a `/`: the key of a member of an object, or the position of
 * an element of an array (its digits, without a zero before them). In a token `~1` is a `/` and `~0` is a `~`.
 * The empty pointer names the value itself.
 *
 * @param[in] value the value where the path starts
 * @param[in] pointer the pointer, as it is written (not as a fragment of a URI)
 * @param[in] length the bytes of the pointer
 * @return the value; NULL when the path does not exist (a key that an object does not have, a position that an
 * array does not have, the `-` of the element after the last one, a token into a value that is not an array nor
 * an object), when the pointer is not valid (it does not start with `/`, a `~` without its 0 or 1), and for a
 * NULL value or a NULL pointer
 * @note For a key that an object has more than once, it follows its last member.
 * @threadsafety Safe from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT const CerverJsonValue *cerver_json_pointer_get (
	const CerverJsonValue *value, const char *pointer, const size_t length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
