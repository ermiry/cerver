/**
 * @file cerver/json/document.h
 * @brief The reader of the JSON of cerver into a document: an immutable tree of values that lives in an arena.
 */

#ifndef _CERVER_JSON_DOCUMENT_H_
#define _CERVER_JSON_DOCUMENT_H_

#include <stddef.h>

#include "cerver/collections/arena.h"

#include "cerver/config.h"

#include "cerver/json/json.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup json_document Document
 * @ingroup json
 * @brief A text parsed into its values.
 *
 * A document takes all its memory from an arena, in three allocations and some more when its values or the
 * members of its objects outgrow the room that they started with, each time for twice as many: a text that fits
 * in a chunk of the arena is parsed without a call to malloc ().
 * @{
 */

/**
 * @brief Parses a text into a document.
 *
 * The text is copied once into the arena and it is not used after the call. The strings of the document are the
 * ones of that copy, without their escapes and each one with a NUL after it.
 *
 * @param[in,out] arena the arena of the document, that is released with it (arena_reset () or arena_delete ());
 * with NULL the document creates its own one, released by cerver_json_document_delete ()
 * @param[in] text the text, it does not need a NUL at its end
 * @param[in] length the bytes of the text
 * @param[in] flags the `CERVER_JSON_PARSE_<name>` flags, #CERVER_JSON_FLAGS_NONE for the defaults
 * @param[in] limits the limits of the text, NULL for the defaults
 * @param[out] error why the text was not parsed and where, with CERVER_JSON_ERROR_NONE and the position 0 when
 * it was; it can be NULL
 * @return the document; NULL when the text is not valid, it is over a limit or the memory could not be allocated
 * @note What a text that failed took from an arena of the caller stays there until the arena is reset.
 * @threadsafety Safe from any thread that owns the arena; without an arena, from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT CerverJsonDocument *cerver_json_parse (
	Arena *arena,
	const char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	CerverJsonError *error
);

/**
 * @brief Parses a text into a document without a copy: the strings of the document are inside the text.
 *
 * @param[in,out] arena the arena of the document, like in cerver_json_parse ()
 * @param[in,out] text the text, in a buffer with #CERVER_JSON_PADDING bytes after its end that the reader can
 * write
 * @param[in] length the bytes of the text, without the padding
 * @param[in] flags the `CERVER_JSON_PARSE_<name>` flags, #CERVER_JSON_FLAGS_NONE for the defaults
 * @param[in] limits the limits of the text, NULL for the defaults
 * @param[out] error why the text was not parsed and where; it can be NULL
 * @return the document; NULL like cerver_json_parse ()
 * @warning The text is changed, also when it is not valid: its escapes are replaced, a NUL is written after each
 * string and the padding is set to 0. It must stay as the reader left it while the document is used.
 * @threadsafety Safe from any thread that owns the arena and the text.
 * @since 3.0b-6
 */
CERVER_EXPORT CerverJsonDocument *cerver_json_parse_in_situ (
	Arena *arena,
	char *text, const size_t length,
	const unsigned int flags, const CerverJsonLimits *limits,
	CerverJsonError *error
);

/**
 * @brief Returns the value of a document.
 *
 * @param[in] document the document
 * @return its value, the one at the top of the text; NULL for a NULL document
 * @threadsafety Safe from any thread; a document is never changed.
 * @since 3.0b-6
 */
CERVER_EXPORT const CerverJsonValue *cerver_json_document_root (
	const CerverJsonDocument *document
);

/**
 * @brief Returns how many values a document has.
 *
 * @param[in] document the document
 * @return its values, the ones inside its arrays and objects and the keys of its members included; 0 for a NULL
 * document
 * @threadsafety Safe from any thread.
 * @since 3.0b-6
 */
CERVER_EXPORT size_t cerver_json_document_values (
	const CerverJsonDocument *document
);

/**
 * @brief Releases a document that created its own arena.
 *
 * A document in an arena of the caller is released with that arena: for it this method does nothing.
 *
 * @param[in] document the document, it can be NULL
 * @threadsafety Not safe while another thread reads the document.
 * @since 3.0b-6
 */
CERVER_EXPORT void cerver_json_document_delete (
	CerverJsonDocument *document
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
