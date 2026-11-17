/**
 * @file cerver/http/parser/parser.h
 * @brief The parser of the requests of an HttpCerver: its lenient flags and the memory that a receive keeps for it.
 */

#ifndef _CERVER_HTTP_PARSER_H_
#define _CERVER_HTTP_PARSER_H_

#include <stdint.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_parser Parser
 * @ingroup http_core
 * @brief The lenient flags of the parser of the requests, strict by default, and the memory of its state.
 *
 * The parser (llhttp) is internal: its headers are not installed and its symbols are not exported, so an application
 * that includes cerver can also use another llhttp. A receive keeps the state and the callbacks of its parser in
 * opaque memory of a fixed size (#HttpParserState and #HttpParserSettings).
 * @{
 */

/**
 * @brief The X-macro table of the lenient flags of the parser: the value, the name, the string and the description
 * of each one.
 *
 * Every flag is disabled by default (the strict mode); each one accepts a single case that RFC 9112 rejects. The
 * values are the ones of the parser, which cerver checks when it is built.
 *
 * @since 2.1b-21
 */
#define HTTP_PARSER_LENIENT_MAP(XX)																										\
	XX(0x1,		HEADERS,					headers,					invalid header values without checks)						\
	XX(0x2,		CHUNKED_LENGTH,				chunked_length,				Transfer-Encoding with Content-Length)						\
	XX(0x4,		KEEP_ALIVE,					keep_alive,					the data after a request with Connection close)				\
	XX(0x8,		TRANSFER_ENCODING,			transfer_encoding,			a value after chunked in Transfer-Encoding)					\
	XX(0x10,	VERSION,					version,					any HTTP version)											\
	XX(0x20,	DATA_AFTER_CLOSE,			data_after_close,			and discards the data after a request with Connection close)	\
	XX(0x40,	OPTIONAL_LF_AFTER_CR,		optional_lf_after_cr,		a CR without LF at the end of a line)						\
	XX(0x80,	OPTIONAL_CRLF_AFTER_CHUNK,	optional_crlf_after_chunk,	a chunk right after the previous one without CRLF)			\
	XX(0x100,	OPTIONAL_CR_BEFORE_LF,		optional_cr_before_lf,		a LF without CR at the end of a line)						\
	XX(0x200,	SPACES_AFTER_CHUNK_SIZE,	spaces_after_chunk_size,	spaces after the size of a chunk)

/**
 * @brief The lenient flags of the parser, one `HTTP_PARSER_LENIENT_<name>` for each row of
 * #HTTP_PARSER_LENIENT_MAP.
 *
 * All of them are disabled by default (the strict mode).
 *
 * @warning Each one can open the cerver to request smuggling or cache poisoning attacks.
 */
typedef enum HttpParserLenient {

	/** @brief No lenient flag: the strict mode. */
	HTTP_PARSER_LENIENT_NONE = 0,

	#define XX(num, name, string, description) HTTP_PARSER_LENIENT_##name = num,
	HTTP_PARSER_LENIENT_MAP(XX)
	#undef XX

} HttpParserLenient;

/** @brief Every lenient flag of #HTTP_PARSER_LENIENT_MAP. */
#define HTTP_PARSER_LENIENT_ALL				0x3FFu

/**
 * @brief Returns the name of a lenient flag ("headers", "chunked_length", ...).
 *
 * @param[in] lenient a single flag
 * @return the name of its row in #HTTP_PARSER_LENIENT_MAP, or "none" for any other value; a static string that must
 * not be freed
 * @threadsafety Safe from any thread.
 * @since 2.1b-21 (it was http_parser_lenient_string ())
 */
CERVER_PUBLIC const char *http_parser_lenient_to_string (
	const HttpParserLenient lenient
);

/**
 * @brief Returns what a lenient flag accepts.
 *
 * @param[in] lenient a single flag
 * @return the description of its row in #HTTP_PARSER_LENIENT_MAP, or "none" for any other value; a static string
 * that must not be freed
 * @threadsafety Safe from any thread.
 */
CERVER_PUBLIC const char *http_parser_lenient_description (
	const HttpParserLenient lenient
);

/**
 * @brief The bytes that a receive keeps for the state of its parser.
 *
 * @since 2.1b-21
 */
#define HTTP_PARSER_STATE_SIZE				(1 << 7)

/**
 * @brief The bytes that a receive keeps for the callbacks of its parser.
 *
 * @since 2.1b-21
 */
#define HTTP_PARSER_SETTINGS_SIZE			(1 << 8)

/**
 * @brief The opaque memory of the state of a parser, aligned for any of its fields.
 *
 * Only cerver reads it: its layout is the one of the parser, which can change in any version.
 *
 * @since 2.1b-21
 */
typedef union HttpParserState {

	/** @brief The bytes of the state. */
	unsigned char bytes[HTTP_PARSER_STATE_SIZE];

	/** @brief Aligns the state for its integers of 64 bits. */
	uint64_t integer_alignment;

	/** @brief Aligns the state for its pointers. */
	void *pointer_alignment;

} HttpParserState;

/**
 * @brief The opaque memory of the callbacks of a parser, aligned for its pointers to functions.
 *
 * Only cerver reads it: its layout is the one of the parser, which can change in any version.
 *
 * @since 2.1b-21
 */
typedef union HttpParserSettings {

	/** @brief The bytes of the callbacks. */
	unsigned char bytes[HTTP_PARSER_SETTINGS_SIZE];

	/** @brief Aligns the callbacks for their pointers to functions. */
	void (*function_alignment)(void);

	/** @brief Aligns the callbacks for their pointers. */
	void *pointer_alignment;

} HttpParserSettings;

/** @} */

#ifdef __cplusplus
}
#endif

#endif
