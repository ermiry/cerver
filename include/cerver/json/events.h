/**
 * @file cerver/json/events.h
 * @brief The reader by events of the JSON of cerver: a text is read from its start to its end and each value is
 * given to a method, without a document.
 */

#ifndef _CERVER_JSON_EVENTS_H_
#define _CERVER_JSON_EVENTS_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#include "cerver/json/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup json_events Events
 * @ingroup json
 * @brief A text read as a sequence of events, for the texts that are too big for a document or that are only
 * read once.
 *
 * It takes the same texts and gives the same errors as cerver_json_parse (), and no memory for each value.
 * @{
 */

/**
 * @brief The methods that a reader by events calls, in the order of the text.
 *
 * Every method gets the data of the call and returns true to continue or false to stop the reader
 * (CERVER_JSON_ERROR_STOPPED). A method that is NULL is skipped. The strings and the keys are in UTF-8, without
 * their escapes and with a NUL after them, and they are only valid while their method runs.
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonEvents {

	/** @brief A null. */
	bool (*on_null) (void *data);

	/** @brief A true or a false. */
	bool (*on_boolean) (void *data, const bool value);

	/** @brief A number that fits in 64 bits with sign, see CERVER_JSON_TYPE_INTEGER. */
	bool (*on_integer) (void *data, const i64 value);

	/** @brief A number that only fits in 64 bits without sign, see CERVER_JSON_TYPE_UNSIGNED. */
	bool (*on_unsigned) (void *data, const u64 value);

	/** @brief A real, see CERVER_JSON_TYPE_REAL. */
	bool (*on_real) (void *data, const double value);

	/** @brief A string that is a value, with its bytes. */
	bool (*on_string) (void *data, const char *string, const size_t length);

	/** @brief The key of a member of an object, with its bytes; its value is the next event. */
	bool (*on_key) (void *data, const char *key, const size_t length);

	/** @brief The start of an object. */
	bool (*on_object_begin) (void *data);

	/** @brief The end of the object that was started last. */
	bool (*on_object_end) (void *data);

	/** @brief The start of an array. */
	bool (*on_array_begin) (void *data);

	/** @brief The end of the array that was started last. */
	bool (*on_array_end) (void *data);

} CerverJsonEvents;

/**
 * @brief Reads a text and calls a method for each of its values.
 *
 * The text is copied once into the arena and it is not changed.
 *
 * @param[in,out] arena the arena for the copy of the text and for the state of the reader; with NULL the reader
 * creates its own one and releases it before it returns
 * @param[in] text the text, it does not need a NUL at its end
 * @param[in] length the bytes of the text
 * @param[in] flags the `CERVER_JSON_PARSE_<name>` flags, #CERVER_JSON_FLAGS_NONE for the defaults;
 * #CERVER_JSON_PARSE_REJECT_DUPLICATES is not supported (CERVER_JSON_ERROR_UNSUPPORTED): the keys of an object
 * are not kept
 * @param[in] limits the limits of the text, NULL for the defaults
 * @param[in] events the methods to call
 * @param[in] data what each method gets, it can be NULL
 * @param[out] error why the text was not read and where, with CERVER_JSON_ERROR_NONE and the position 0 when
 * it was; it can be NULL
 * @return true when the whole text was read; false when it is not valid, it is over a limit, a method stopped it
 * or the memory could not be allocated
 * @warning The methods of the values before an error were already called when the reader finds it.
 * @threadsafety Safe from any thread that owns the arena.
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_read (
	Arena *arena,
	const char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	const CerverJsonEvents *events, void *data,
	CerverJsonError *error
);

/**
 * @brief Works as cerver_json_read (), without the copy of the text.
 *
 * @param[in,out] arena the arena for the state of the reader, like in cerver_json_read ()
 * @param[in,out] text the text, in a buffer with #CERVER_JSON_PADDING bytes after its end that the reader can
 * write
 * @param[in] length the bytes of the text, without the padding
 * @param[in] flags the flags, like in cerver_json_read ()
 * @param[in] limits the limits of the text, NULL for the defaults
 * @param[in] events the methods to call
 * @param[in] data what each method gets, it can be NULL
 * @param[out] error why the text was not read and where; it can be NULL
 * @return true when the whole text was read, like cerver_json_read ()
 * @warning The text is changed, also when it is not valid, like in cerver_json_parse_in_situ ().
 * @threadsafety Safe from any thread that owns the arena and the text.
 * @since 3.0b-6
 */
CERVER_EXPORT bool cerver_json_read_in_situ (
	Arena *arena,
	char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	const CerverJsonEvents *events, void *data,
	CerverJsonError *error
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
