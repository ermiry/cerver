/**
 * @file cerver/json/json.h
 * @brief The JSON of cerver (RFC 8259): the types of a value, the errors, the limits and the flags that its parts
 * share.
 */

#ifndef _CERVER_JSON_H_
#define _CERVER_JSON_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/types/types.h"

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup json JSON
 * @brief The JSON of cerver (RFC 8259): a reader into an immutable document that lives in an arena, a reader by
 * events, a writer and the queries by pointer (RFC 6901).
 *
 * Every limit and every flag is an argument of its call: nothing here has a state that a caller sets, global or
 * per thread. The only state of the process is the seed of the hash of the keys of the big objects, that the
 * first reader takes once from the random bytes of the kernel.
 */

/**
 * @defgroup json_types Types
 * @ingroup json
 * @brief The types of a value and the values themselves.
 * @{
 */

/**
 * @brief The X-macro table of the types of a value: the number, the name and the string of each one.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_TYPE_MAP(XX)			\
	XX(0,	NONE,		None)				\
	XX(1,	NULL,		Null)				\
	XX(2,	BOOLEAN,	Boolean)			\
	XX(3,	INTEGER,	Integer)			\
	XX(4,	UNSIGNED,	Unsigned)			\
	XX(5,	REAL,		Real)				\
	XX(6,	STRING,		String)				\
	XX(7,	ARRAY,		Array)				\
	XX(8,	OBJECT,		Object)

/**
 * @brief The type of a value, one `CERVER_JSON_TYPE_<name>` for each row of #CERVER_JSON_TYPE_MAP.
 *
 * A number is an INTEGER when it was written without a fraction and without an exponent and it fits in 64 bits
 * with sign, an UNSIGNED when it only fits in 64 bits without sign, and a REAL otherwise. NONE is the type of a
 * NULL pointer.
 *
 * @since 3.0b-6
 */
typedef enum CerverJsonType {

	#define XX(num, name, string) CERVER_JSON_TYPE_##name = num,
	CERVER_JSON_TYPE_MAP (XX)
	#undef XX

} CerverJsonType;

/**
 * @brief Returns the string of a type of value.
 *
 * @param[in] type the type
 * @return the string of the type, the one of CERVER_JSON_TYPE_NONE for a value that is not in the table
 * @threadsafety Safe from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT const char *cerver_json_type_to_string (
	const CerverJsonType type
);

struct CerverJsonValue;

/**
 * @brief A value of a document: a null, a boolean, a number, a string, an array or an object.
 *
 * It is read with the methods of cerver/json/value.h, it is never changed and it lives as long as its document.
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonValue CerverJsonValue;

struct CerverJsonDocument;

/**
 * @brief A text that was parsed: its values, created with the methods of cerver/json/document.h.
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonDocument CerverJsonDocument;

/** @} */

/**
 * @defgroup json_errors Errors
 * @ingroup json
 * @brief What a reader or a writer answers when it fails.
 * @{
 */

/**
 * @brief The X-macro table of the errors: the number, the name and the description of each one.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_ERROR_MAP(XX)																	\
	XX(0,	NONE,				No error)															\
	XX(1,	ARGUMENTS,			An argument is not valid)											\
	XX(2,	MEMORY,				The memory could not be allocated)									\
	XX(3,	SIZE,				The text is bigger than its limit)									\
	XX(4,	DEPTH,				The arrays and the objects are nested over their limit)			\
	XX(5,	VALUES,				The text has more values than its limit)							\
	XX(6,	END,				The text ended before its value did)								\
	XX(7,	CHARACTER,			A character is not expected in its position)						\
	XX(8,	TRAILING,			There is content after the value)									\
	XX(9,	LITERAL,			A literal is not true or false or null)							\
	XX(10,	NUMBER,				A number is not written as the grammar says)						\
	XX(11,	RANGE,				A number does not fit in its type)									\
	XX(12,	CONTROL,			A string has a control character that is not escaped)				\
	XX(13,	ESCAPE,				A string has an escape that is not valid)							\
	XX(14,	SURROGATE,			A string has a surrogate without its pair)							\
	XX(15,	ENCODING,			A string is not valid UTF-8)										\
	XX(16,	NUL,				A string has a NUL character that was not allowed)					\
	XX(17,	DUPLICATE,			An object has a key more than once and it was not allowed)			\
	XX(18,	STOPPED,			A method of the events asked to stop)								\
	XX(19,	STATE,				The writer was used out of order)									\
	XX(20,	TRUNCATED,			The buffer of the writer is smaller than its text)					\
	XX(21,	UNSUPPORTED,		A flag is not supported by the method)

/**
 * @brief The error of a reader or of a writer, one `CERVER_JSON_ERROR_<name>` for each row of
 * #CERVER_JSON_ERROR_MAP.
 *
 * @since 3.0b-6
 */
typedef enum CerverJsonErrorCode {

	#define XX(num, name, description) CERVER_JSON_ERROR_##name = num,
	CERVER_JSON_ERROR_MAP (XX)
	#undef XX

} CerverJsonErrorCode;

/**
 * @brief Returns the description of an error.
 *
 * @param[in] code the error
 * @return its description, the one of CERVER_JSON_ERROR_NONE for a value that is not in the table
 * @threadsafety Safe from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT const char *cerver_json_error_to_string (
	const CerverJsonErrorCode code
);

/**
 * @brief Why a text was not read and where.
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonError {

	/** @brief The error, CERVER_JSON_ERROR_NONE when the text was read. */
	CerverJsonErrorCode code;

	/** @brief The position in bytes of the text where the error was found, from 0. */
	size_t offset;

} CerverJsonError;

/**
 * @brief Translates the offset of an error into its line and its column, both from 1.
 *
 * The column counts bytes, not characters.
 *
 * @param[in] text the text that was read, as it was before it was read
 * @param[in] length the bytes of the text
 * @param[in] offset the position, CerverJsonError::offset; one after the text is taken as its end
 * @param[out] line the line of the position, it can be NULL
 * @param[out] column the column of the position, it can be NULL
 * @threadsafety Safe from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_error_position (
	const char *text, const size_t length, const size_t offset,
	size_t *line, size_t *column
);

/** @} */

/**
 * @defgroup json_limits Limits
 * @ingroup json
 * @brief What a text can ask from a reader.
 * @{
 */

/**
 * @brief The default limit of nested arrays and objects of a text.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_LIMIT_DEPTH				(1 << 11)

/**
 * @brief The default limit of the bytes of a text.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_LIMIT_SIZE				(1 << 26)

/**
 * @brief The default limit of the values of a text; the key of a member of an object is also a value.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_LIMIT_VALUES			(1 << 22)

/**
 * @brief The bytes of the biggest text that a reader takes, whatever its limit says.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_SIZE_MAX				(1 << 30)

/**
 * @brief The bytes that a text read in situ must have after its end, see cerver_json_parse_in_situ ().
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_PADDING					(1 << 6)

/**
 * @brief The limits of a text, given to each call of a reader.
 *
 * The memory of a document is bounded by them: it takes its text once with #CERVER_JSON_PADDING bytes, and for
 * each value an amount that the text can not make bigger: its 16 bytes, its part of the directory and of the
 * table of the object that has it, and the room that the values left when they moved to twice as much.
 *
 * @since 3.0b-6
 */
typedef struct CerverJsonLimits {

	/** @brief The nested arrays and objects that a text can have; 0 uses #CERVER_JSON_LIMIT_DEPTH. */
	size_t depth;

	/** @brief The bytes that a text can have; 0 uses #CERVER_JSON_LIMIT_SIZE, and one over #CERVER_JSON_SIZE_MAX is that one. */
	size_t size;

	/** @brief The values that a text can have; 0 uses #CERVER_JSON_LIMIT_VALUES. */
	size_t values;

} CerverJsonLimits;

/** @} */

/**
 * @defgroup json_flags Flags
 * @ingroup json
 * @brief What changes how a text is read or written; the flags are combined with a bitwise or.
 *
 * A reader takes by default every text that RFC 8259 defines: any value at the top, an object with a key more
 * than once, a string with an escaped NUL. No flag makes it take what the grammar does not have.
 * @{
 */

/**
 * @brief Reads or writes with the defaults.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_FLAGS_NONE						0

/**
 * @brief An object with a key more than once is an error (CERVER_JSON_ERROR_DUPLICATE).
 *
 * Without it the object is taken with every member, and a search by key answers the last one.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_PARSE_REJECT_DUPLICATES			(1 << 0)

/**
 * @brief A string or a key with an escaped NUL is an error (CERVER_JSON_ERROR_NUL).
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_PARSE_REJECT_NUL				(1 << 1)

/**
 * @brief An integer that does not fit in 64 bits is read as a real, without its exact value.
 *
 * Without it that integer is an error (CERVER_JSON_ERROR_RANGE); a real that does not fit in a double is always
 * one.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_PARSE_BIG_AS_REAL				(1 << 2)

/**
 * @brief Writes without the space after each comma and each colon.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_WRITE_COMPACT					(1 << 0)

/**
 * @brief Writes each slash of a string as an escape.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_WRITE_ESCAPE_SLASH				(1 << 1)

/**
 * @brief Writes each character of a string that is not ASCII as an escape.
 *
 * @since 3.0b-6
 */
#define CERVER_JSON_WRITE_ENSURE_ASCII				(1 << 2)

/** @} */

#ifdef __cplusplus
}
#endif

#endif
