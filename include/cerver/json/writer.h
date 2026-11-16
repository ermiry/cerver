/**
 * @file cerver/json/writer.h
 * @brief The writer of the JSON of cerver: a text written value by value into a buffer or into an arena.
 */

#ifndef _CERVER_JSON_WRITER_H_
#define _CERVER_JSON_WRITER_H_

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
 * @defgroup json_writer Writer
 * @ingroup json
 * @brief A text written value by value, without an allocation for each one.
 *
 * By default it writes a space after each comma and each colon, `{"name": "cerver", "values": [1, 2, 3]}`;
 * #CERVER_JSON_WRITE_COMPACT writes none and cerver_json_writer_set_indent () writes each value in its line.
 * A writer that fails stays with its first error and writes nothing more: the methods can be called without a
 * check after each one, and the error is asked once with cerver_json_writer_end ().
 * @{
 */

/**
 * @brief The nested arrays and objects that a writer can have open.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_WRITER_DEPTH			(1 << 11)

/**
 * @brief The bits of each word of CerverJsonWriter::objects.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_WRITER_WORD_BITS		64

/**
 * @brief The words of CerverJsonWriter::objects.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_WRITER_WORDS			(CERVER_JSON_WRITER_DEPTH / CERVER_JSON_WRITER_WORD_BITS)

/**
 * @brief The state of a text that is being written, in the memory of its caller.
 *
 * It is started with cerver_json_writer_init () or cerver_json_writer_init_arena () and it has nothing to
 * release. Its fields are read with its methods.
 *
 * @threadsafety Not thread safe: a writer is used by one thread.
 * @since 3.0b-6
 */
typedef struct CerverJsonWriter {

	/** @brief Where the text is written: the buffer of the caller, or the current one of the arena. */
	char *buffer;

	/** @brief The bytes of the buffer, with the one of the NUL at the end of the text. */
	size_t capacity;

	/** @brief The bytes of the text until now, also the ones that did not fit in a buffer of the caller. */
	size_t length;

	/** @brief The arena where the buffer grows; NULL for a buffer of the caller, that never grows. */
	Arena *arena;

	/** @brief The `CERVER_JSON_WRITE_<name>` flags. */
	unsigned int flags;

	/** @brief The spaces of each level when every value is written in its line, 0 for a text in one line. */
	unsigned int indent;

	/** @brief The first error, CERVER_JSON_ERROR_NONE while there is none. */
	CerverJsonErrorCode error;

	/** @brief The arrays and the objects that are open. */
	size_t depth;

	/** @brief A bit for each level that is open: 1 for an object, 0 for an array. */
	u64 objects[CERVER_JSON_WRITER_WORDS];

	/** @brief The array or the object that is open has no value yet; at the top, the text has no value yet. */
	bool empty;

	/** @brief The key of a member was written and its value is next. */
	bool keyed;

} CerverJsonWriter;

/**
 * @brief Starts a writer into a buffer of the caller.
 *
 * The text is cut when it does not fit, always with a NUL at its end, and the writer keeps counting: like
 * snprintf (), cerver_json_writer_length () answers the bytes that the whole text needs.
 *
 * @param[out] writer the writer
 * @param[out] buffer the buffer; it can be NULL with a capacity of 0, to only count the bytes of a text
 * @param[in] capacity the bytes of the buffer, with the one of the NUL
 * @param[in] flags the `CERVER_JSON_WRITE_<name>` flags, #CERVER_JSON_FLAGS_NONE for the defaults
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_init (
	CerverJsonWriter *writer,
	char *buffer, const size_t capacity,
	const unsigned int flags
);

/**
 * @brief Starts a writer into an arena: its buffer grows there as the text needs it.
 *
 * @param[out] writer the writer
 * @param[in,out] arena the arena; the text is valid until it is reset or deleted
 * @param[in] flags the `CERVER_JSON_WRITE_<name>` flags, #CERVER_JSON_FLAGS_NONE for the defaults
 * @note Each time that the buffer grows the previous one stays in the arena until its reset.
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_init_arena (
	CerverJsonWriter *writer,
	Arena *arena,
	const unsigned int flags
);

/**
 * @brief Writes each value in its own line, indented by its level.
 *
 * @param[in,out] writer the writer, before its first value
 * @param[in] spaces the spaces of each level; 0 writes the text in one line, the default
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_set_indent (
	CerverJsonWriter *writer, const unsigned int spaces
);

/**
 * @brief Starts an object, as the value of the text, of a member or of an element.
 *
 * @param[in,out] writer the writer
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_object_begin (
	CerverJsonWriter *writer
);

/**
 * @brief Ends the object that is open.
 *
 * @param[in,out] writer the writer; CERVER_JSON_ERROR_STATE when what is open is not an object or a key waits
 * for its value
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_object_end (
	CerverJsonWriter *writer
);

/**
 * @brief Starts an array, as the value of the text, of a member or of an element.
 *
 * @param[in,out] writer the writer
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_array_begin (
	CerverJsonWriter *writer
);

/**
 * @brief Ends the array that is open.
 *
 * @param[in,out] writer the writer; CERVER_JSON_ERROR_STATE when what is open is not an array
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_array_end (
	CerverJsonWriter *writer
);

/**
 * @brief Writes the key of a member of the object that is open; its value is written next.
 *
 * @param[in,out] writer the writer; CERVER_JSON_ERROR_STATE outside an object and after another key,
 * CERVER_JSON_ERROR_ENCODING for a key that is not valid UTF-8
 * @param[in] key the key, ended by a NUL; it is escaped as it needs
 * @note The writer does not check that an object has each key once.
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_key (
	CerverJsonWriter *writer, const char *key
);

/**
 * @brief Works as cerver_json_writer_key (), with a key that is given by its length.
 *
 * @param[in,out] writer the writer
 * @param[in] key the key, it can have a NUL inside
 * @param[in] length the bytes of the key
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_key_bytes (
	CerverJsonWriter *writer, const char *key, const size_t length
);

/**
 * @brief Writes a string as a value.
 *
 * @param[in,out] writer the writer; CERVER_JSON_ERROR_ENCODING for a string that is not valid UTF-8
 * @param[in] string the string, it can have a NUL inside; it is escaped as it needs
 * @param[in] length the bytes of the string
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_string (
	CerverJsonWriter *writer, const char *string, const size_t length
);

/**
 * @brief Writes a null as a value.
 *
 * @param[in,out] writer the writer
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_null (
	CerverJsonWriter *writer
);

/**
 * @brief Writes a true or a false as a value.
 *
 * @param[in,out] writer the writer
 * @param[in] value the boolean
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_boolean (
	CerverJsonWriter *writer, const bool value
);

/**
 * @brief Writes an integer as a value.
 *
 * @param[in,out] writer the writer
 * @param[in] value the integer
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_integer (
	CerverJsonWriter *writer, const i64 value
);

/**
 * @brief Writes an integer without sign as a value.
 *
 * @param[in,out] writer the writer
 * @param[in] value the integer
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_unsigned (
	CerverJsonWriter *writer, const u64 value
);

/**
 * @brief Writes a real as a value, with the least digits that read back the same double.
 *
 * It always has a fraction or an exponent (`1.0`, `0.1`, `1e21`, `1.5e-7`), so it is read back as a real.
 *
 * @param[in,out] writer the writer; CERVER_JSON_ERROR_RANGE for a NaN and for an infinite, that JSON does not
 * have
 * @param[in] value the real
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_real (
	CerverJsonWriter *writer, const double value
);

/**
 * @brief Writes a text that is already JSON as a value, as it is.
 *
 * @param[in,out] writer the writer
 * @param[in] text the text of one value
 * @param[in] length the bytes of the text
 * @warning The text is not checked: one that is not a JSON value breaks the text of the writer.
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_raw (
	CerverJsonWriter *writer, const char *text, const size_t length
);

/**
 * @brief Writes a value of a document, with everything that it has inside.
 *
 * @param[in,out] writer the writer; CERVER_JSON_ERROR_ARGUMENTS for a NULL value, CERVER_JSON_ERROR_DEPTH when
 * the value does not fit in the levels of the writer
 * @param[in] value the value
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_writer_value (
	CerverJsonWriter *writer, const CerverJsonValue *value
);

/**
 * @brief Ends the text of a writer and returns how it went.
 *
 * @param[in,out] writer the writer
 * @return CERVER_JSON_ERROR_NONE for a complete text; the first error of the writer;
 * CERVER_JSON_ERROR_STATE for a text without a value or with an array or an object that is still open;
 * CERVER_JSON_ERROR_TRUNCATED when the text did not fit in the buffer of the caller;
 * CERVER_JSON_ERROR_ARGUMENTS for a NULL writer
 * @since 3.0b-6
 */
CERVER_EXPORT CerverJsonErrorCode cerver_json_writer_end (
	CerverJsonWriter *writer
);

/**
 * @brief Returns the text of a writer.
 *
 * @param[in] writer the writer
 * @return its buffer, with a NUL after what was written; NULL for a writer that only counts, for one of an arena
 * that wrote nothing and for a NULL writer
 * @since 3.0b-6
 */
CERVER_EXPORT const char *cerver_json_writer_text (
	const CerverJsonWriter *writer
);

/**
 * @brief Returns the length of the text of a writer.
 *
 * @param[in] writer the writer
 * @return the bytes of the whole text without its NUL, also the ones that did not fit in a buffer of the caller;
 * 0 for a NULL writer
 * @since 3.0b-6
 */
CERVER_EXPORT size_t cerver_json_writer_length (
	const CerverJsonWriter *writer
);

/**
 * @brief Writes a value of a document into a buffer, with everything that it has inside.
 *
 * @param[in] value the value
 * @param[out] buffer the buffer; it can be NULL with a capacity of 0, to ask for the bytes of the text
 * @param[in] capacity the bytes of the buffer, with the one of the NUL
 * @param[in] flags the `CERVER_JSON_WRITE_<name>` flags, #CERVER_JSON_FLAGS_NONE for the defaults
 * @return the bytes of the whole text without its NUL, like snprintf (): one that is not below the capacity
 * means that the text was cut; 0 for a NULL value and for a value that could not be written
 * @threadsafety Safe from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT size_t cerver_json_write (
	const CerverJsonValue *value,
	char *buffer, const size_t capacity,
	const unsigned int flags
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
