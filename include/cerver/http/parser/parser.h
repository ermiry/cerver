/**
 * @file cerver/http/parser/parser.h
 * @brief The HTTP/1.1 parser of cerver (RFC 9112): the types of a message, its head, the lenient flags, the errors,
 * the limits and the state that its parts share.
 */

#ifndef _CERVER_HTTP_PARSER_H_
#define _CERVER_HTTP_PARSER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "cerver/config.h"

#include "cerver/http/headers/types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_parser Parser
 * @ingroup http_core
 * @brief The HTTP/1.1 parser of cerver (RFC 9112), strict by default.
 *
 * A message is read in two steps. Its head (the start line and the headers) is read with
 * cerver_http_parser_head () when its buffer has the whole head: the caller gives each time the bytes of the head
 * from its first one, and the parser goes on from the last line that it read, so a head that arrives in pieces is
 * read once. The head is a set of spans (an offset and a length) over that buffer: moving the head to other memory
 * only changes its base (cerver_http_parser_rebase ()). Then its body (Content-Length, chunked with its extensions
 * and its trailers, or until the end of the connection in a response) is read with cerver_http_parser_body () in
 * any piece, as views of the buffer of each call.
 *
 * The parser has no allocations and no global state: it lives in the memory of its caller, with the array of the
 * fields of its head, and every limit and every lenient flag is its own.
 */

/**
 * @defgroup http_parser_types Types
 * @ingroup http_parser
 * @brief The lenient flags, the errors, the results and the limits.
 * @{
 */

/**
 * @brief The X-macro table of the lenient flags of the parser: the value, the name, the string and the description
 * of each one.
 *
 * Every flag is disabled by default (the strict mode); each one accepts a single case that RFC 9112 rejects.
 *
 * @since 3.0b-9
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
 */
CERVER_PUBLIC const char *http_parser_lenient_string (
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
 * @brief The X-macro table of the errors of the parser: the number, the name, the string and the reason of each
 * one.
 *
 * `METHOD_UNKNOWN` is a method that is a token but not one of #REQUEST_METHOD_MAP, that cerver answers with 501
 * (since the 3.0b-10; before, `METHOD` and 400).
 *
 * @since 3.0b-9
 */
#define CERVER_HTTP_PARSER_ERROR_MAP(XX)																	\
	XX(0,	NONE,					none,					No error)										\
	XX(1,	METHOD,					method,					Invalid method)									\
	XX(2,	TARGET,					target,					Invalid request target)							\
	XX(3,	PROTOCOL,				protocol,				Expected HTTP)									\
	XX(4,	VERSION,				version,				Invalid HTTP version)							\
	XX(5,	VERSION_UNSUPPORTED,	version_unsupported,	A version that is not HTTP/1.0 nor HTTP/1.1)	\
	XX(6,	LINE,					line,					Invalid end of a line)							\
	XX(7,	STATUS,					status,					Invalid response status)						\
	XX(8,	FIELD_NAME,				field_name,				Invalid header name)							\
	XX(9,	FIELD_VALUE,			field_value,			Invalid header value)							\
	XX(10,	CONTENT_LENGTH,			content_length,			Invalid Content-Length)							\
	XX(11,	TRANSFER_ENCODING,		transfer_encoding,		Invalid Transfer-Encoding)						\
	XX(12,	CHUNK_SIZE,				chunk_size,				Invalid chunk size)								\
	XX(13,	CHUNK_EXTENSION,		chunk_extension,		Invalid chunk extension)						\
	XX(14,	CHUNK_DATA,				chunk_data,				Expected CRLF after the data of a chunk)		\
	XX(15,	TARGET_SIZE,			target_size,			The request target is too long)					\
	XX(16,	HEADERS_SIZE,			headers_size,			The headers are too large)						\
	XX(17,	CLOSED,					closed,					Data after Connection close)					\
	XX(18,	INCOMPLETE,				incomplete,				The message is not complete)					\
	XX(19,	METHOD_UNKNOWN,			method_unknown,			A method that is not implemented)

/**
 * @brief An error of the parser, one `CERVER_HTTP_PARSER_ERROR_<name>` for each row of
 * #CERVER_HTTP_PARSER_ERROR_MAP.
 *
 * @since 3.0b-9
 */
typedef enum CerverHttpParserError {

	#define XX(num, name, string, reason) CERVER_HTTP_PARSER_ERROR_##name = num,
	CERVER_HTTP_PARSER_ERROR_MAP (XX)
	#undef XX

} CerverHttpParserError;

/**
 * @brief Returns the string of an error of the parser ("none", "method", ...).
 *
 * @param[in] error the error
 * @return the string of its row in #CERVER_HTTP_PARSER_ERROR_MAP, the one of CERVER_HTTP_PARSER_ERROR_NONE for a
 * value out of the table; a static string that must not be freed
 * @threadsafety Safe from any thread.
 * @since 3.0b-9
 */
CERVER_EXPORT const char *cerver_http_parser_error_string (
	const CerverHttpParserError error
);

/**
 * @brief Returns the reason of an error of the parser, a sentence for a log.
 *
 * @param[in] error the error
 * @return the reason of its row in #CERVER_HTTP_PARSER_ERROR_MAP, the one of CERVER_HTTP_PARSER_ERROR_NONE for a
 * value out of the table; a static string that must not be freed
 * @threadsafety Safe from any thread.
 * @since 3.0b-9
 */
CERVER_EXPORT const char *cerver_http_parser_error_reason (
	const CerverHttpParserError error
);

/**
 * @brief What a call to the parser found.
 *
 * @since 3.0b-9
 */
typedef enum CerverHttpParserResult {

	/** @brief Every byte was read and more are needed. */
	CERVER_HTTP_PARSER_RESULT_MORE		= 0,
	/** @brief The head is complete: the consumed bytes are its bytes. */
	CERVER_HTTP_PARSER_RESULT_HEAD		= 1,
	/** @brief The array of the fields is full: the parser goes on with a bigger one (cerver_http_parser_fields_set ()). */
	CERVER_HTTP_PARSER_RESULT_FIELDS	= 2,
	/** @brief A piece of the body: its data is a view of the buffer of the call. */
	CERVER_HTTP_PARSER_RESULT_BODY		= 3,
	/** @brief The message is complete: the consumed bytes end in its last byte. */
	CERVER_HTTP_PARSER_RESULT_COMPLETE	= 4,
	/** @brief The message is not valid: the error of the parser says why. */
	CERVER_HTTP_PARSER_RESULT_ERROR		= 5

} CerverHttpParserResult;

/**
 * @brief The type of the messages of a parser.
 *
 * @since 3.0b-9
 */
typedef enum CerverHttpParserType {

	/** @brief Requests, the messages that a server reads. */
	CERVER_HTTP_PARSER_TYPE_REQUEST		= 0,
	/** @brief Responses, the messages that a client reads. */
	CERVER_HTTP_PARSER_TYPE_RESPONSE	= 1

} CerverHttpParserType;

/** @brief The bytes of a request target by default, 8 KiB. */
#define CERVER_HTTP_PARSER_DEFAULT_TARGET_SIZE		(1 << 13)
/** @brief The bytes of the names and the values of the headers by default, 64 KiB. */
#define CERVER_HTTP_PARSER_DEFAULT_FIELDS_SIZE		(1 << 16)
/** @brief The headers by default. */
#define CERVER_HTTP_PARSER_DEFAULT_FIELDS			(1 << 7)

/**
 * @brief The limits of a parser, 0 disables a limit.
 *
 * The trailers of a chunked body and the extensions of its chunks count as headers: a body can not grow them
 * without an end.
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpParserLimits {

	/** @brief The bytes of the request target (`CERVER_HTTP_PARSER_ERROR_TARGET_SIZE`). */
	size_t target_size;

	/** @brief The bytes of the names and the values of the headers (`CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE`). */
	size_t fields_size;

	/** @brief The headers (`CERVER_HTTP_PARSER_ERROR_HEADERS_SIZE`). */
	unsigned int fields;

} CerverHttpParserLimits;

/**
 * @brief Sets the default limits.
 *
 * @param[out] limits the limits
 * @threadsafety Safe from any thread.
 * @since 3.0b-9
 */
CERVER_EXPORT void cerver_http_parser_limits_default (
	CerverHttpParserLimits *limits
);

/** @} */

/**
 * @defgroup http_parser_head_types Head
 * @ingroup http_parser
 * @brief The head of a message: its start line and its fields, as spans of its buffer.
 * @{
 */

/**
 * @brief A part of a head: its offset from the base of the head and its length.
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpSpan {

	/** @brief The offset of its first byte from the base of the head. */
	uint32_t offset;

	/** @brief Its bytes. */
	uint32_t length;

} CerverHttpSpan;

/** @brief The value of a field was folded in several lines (only with `HTTP_PARSER_LENIENT_HEADERS`). */
#define CERVER_HTTP_FIELD_FOLDED			0x1u

/**
 * @brief A field of a head: a header.
 *
 * Its name is a token (RFC 9110 section 5.1); its value is without the spaces and the tabs around it. A folded
 * value spans its lines with their line breaks.
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpField {

	/** @brief The name, as it arrived. */
	CerverHttpSpan name;

	/** @brief The value. */
	CerverHttpSpan value;

	/** @brief The known header of the name (case insensitive), `HTTP_HEADER_UNDEFINED` for any other one. */
	http_header type;

	/** @brief Its flags, #CERVER_HTTP_FIELD_FOLDED. */
	unsigned int flags;

} CerverHttpField;

/** @brief The head has a Content-Length. */
#define CERVER_HTTP_HEAD_CONTENT_LENGTH			0x1u
/** @brief The last coding of its Transfer-Encoding is chunked. */
#define CERVER_HTTP_HEAD_CHUNKED				0x2u
/** @brief The head has a Transfer-Encoding. */
#define CERVER_HTTP_HEAD_TRANSFER_ENCODING		0x4u
/** @brief Its Connection (or Proxy-Connection) has close. */
#define CERVER_HTTP_HEAD_CONNECTION_CLOSE		0x8u
/** @brief Its Connection (or Proxy-Connection) has keep-alive. */
#define CERVER_HTTP_HEAD_CONNECTION_KEEP_ALIVE	0x10u
/** @brief Its Connection (or Proxy-Connection) has upgrade. */
#define CERVER_HTTP_HEAD_CONNECTION_UPGRADE		0x20u
/** @brief The head has an Upgrade with a value. */
#define CERVER_HTTP_HEAD_UPGRADE				0x40u

/** @brief The method of the preface of HTTP/2 (RFC 9113 section 3.4), PRI, that #REQUEST_METHOD_MAP does not have. */
#define CERVER_HTTP_PARSER_METHOD_PRI			34

/**
 * @brief The head of a message.
 *
 * Every span is relative to `base`, the buffer of the call that completed the head (or the one of
 * cerver_http_parser_rebase ()).
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpHead {

	/** @brief The bytes that the spans point to. */
	const char *base;

	/** @brief The bytes of the head from `base`, with the empty lines before it and the empty line that ends it. */
	size_t length;

	/** @brief The method of a request, a value of #REQUEST_METHOD_MAP or #CERVER_HTTP_PARSER_METHOD_PRI. */
	unsigned int method;

	/** @brief The request target. */
	CerverHttpSpan target;

	/** @brief The major version. */
	unsigned int major;
	/** @brief The minor version. */
	unsigned int minor;

	/** @brief The status of a response. */
	unsigned int status;
	/** @brief The reason of a response. */
	CerverHttpSpan reason;

	/** @brief The fields, in the array of the caller. */
	CerverHttpField *fields;
	/** @brief The fields that were read. */
	unsigned int fields_count;
	/** @brief The fields that the array takes. */
	unsigned int fields_capacity;

	/** @brief The bytes of the names and the values of the fields, with the trailers and the chunk extensions. */
	size_t fields_size;
	/** @brief The fields and the trailers that count for the limit. */
	unsigned int fields_counted;

	/** @brief The value of its Content-Length. */
	uint64_t content_length;

	/** @brief What its fields say, the `CERVER_HTTP_HEAD_*` flags. */
	unsigned int flags;

} CerverHttpHead;

/** @} */

/**
 * @defgroup http_parser_state State
 * @ingroup http_parser
 * @brief A parser and what its messages share.
 * @{
 */

/** @brief The bytes that a parser keeps of a name or a token of a trailer: the longest one that it looks for, with room. */
#define CERVER_HTTP_PARSER_TOKEN_SIZE		24

/**
 * @brief A parser of the messages of a connection, in the memory of its caller.
 *
 * Its fields are read with its methods; the head can be read directly.
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpParser {

	/** @brief Requests or responses. */
	CerverHttpParserType type;

	/** @brief Its lenient flags, `HTTP_PARSER_LENIENT_*`. */
	unsigned int lenient;

	/** @brief Its limits. */
	CerverHttpParserLimits limits;

	/** @brief Where the parser is, for its internal methods. */
	unsigned int state;

	/** @brief The error, CERVER_HTTP_PARSER_ERROR_NONE until one is found. */
	CerverHttpParserError error;

	/** @brief The offset of the next line of the head to read, for its internal methods. */
	size_t offset;

	/** @brief The bytes that are left of the body or of the chunk. */
	uint64_t remaining;

	/** @brief The bytes of the line of the body that is being read (a chunk size, a trailer), for the limits. */
	size_t line;

	/** @brief The state of the field of a trailer that is being read, for its internal methods. */
	unsigned int trailer;

	/** @brief The lower case name of a trailer, or the token of its Connection, for its internal methods. */
	char token[CERVER_HTTP_PARSER_TOKEN_SIZE];
	/** @brief The bytes of #token. */
	unsigned int token_length;

	/** @brief The message is an upgrade: what follows it is not HTTP. */
	bool upgrade;

	/** @brief The message of a response has no body (a response to a HEAD), set before its head. */
	bool skip_body;

	/** @brief The head has a field that changes how the message is read (Connection, Content-Length, …). */
	bool specials;

	/** @brief The messages that were completed. */
	unsigned int messages;

	/** @brief The head of the message. */
	CerverHttpHead head;

} CerverHttpParser;

/**
 * @brief Starts a parser.
 *
 * @param[out] parser the parser
 * @param[in] type requests or responses
 * @param[in] lenient its lenient flags, `HTTP_PARSER_LENIENT_*` (the bits outside #HTTP_PARSER_LENIENT_ALL are ignored)
 * @param[in] limits its limits, NULL for the defaults
 * @param[in] fields the array of the fields of its heads, it stays with the parser
 * @param[in] capacity the fields that the array takes
 * @threadsafety A parser is used by one thread at a time.
 * @since 3.0b-9
 */
CERVER_EXPORT void cerver_http_parser_init (
	CerverHttpParser *parser,
	const CerverHttpParserType type, const unsigned int lenient,
	const CerverHttpParserLimits *limits,
	CerverHttpField *fields, const unsigned int capacity
);

/**
 * @brief Starts the next message of a parser: its type, its lenient flags, its limits and its array are kept.
 *
 * @param[in,out] parser the parser
 * @since 3.0b-9
 */
CERVER_EXPORT void cerver_http_parser_reset (
	CerverHttpParser *parser
);

/**
 * @brief Gives a parser another array for the fields, after #CERVER_HTTP_PARSER_RESULT_FIELDS: the fields that it
 * read are copied to it.
 *
 * @param[in,out] parser the parser
 * @param[in] fields the array
 * @param[in] capacity the fields that it takes, at least the ones that were read
 * @since 3.0b-9
 */
CERVER_EXPORT void cerver_http_parser_fields_set (
	CerverHttpParser *parser,
	CerverHttpField *fields, const unsigned int capacity
);

/**
 * @brief Returns the error of a parser.
 *
 * @param[in] parser the parser
 * @return the error, CERVER_HTTP_PARSER_ERROR_NONE if it did not find one
 * @since 3.0b-9
 */
CERVER_EXPORT CerverHttpParserError cerver_http_parser_get_error (
	const CerverHttpParser *parser
);

/**
 * @brief Returns whether the start line of the message was read: its fields are next.
 *
 * @param[in] parser the parser
 * @return true from the start line until the next message
 * @since 3.0b-9
 */
CERVER_EXPORT bool cerver_http_parser_started (
	const CerverHttpParser *parser
);

/**
 * @brief Returns whether the head of the message was read: its body is next or it is being read.
 *
 * @param[in] parser the parser
 * @return true from the head until the end of the message
 * @since 3.0b-9
 */
CERVER_EXPORT bool cerver_http_parser_head_read (
	const CerverHttpParser *parser
);

/**
 * @brief Returns whether the connection of a message can take another one (RFC 9112 section 9.3): an HTTP/1.1
 * message without close in its Connection, or an HTTP/1.0 one with keep-alive, whose end is not the end of the
 * connection.
 *
 * @param[in] parser the parser, after the head of the message
 * @return true if the connection can be kept
 * @since 3.0b-9
 */
CERVER_EXPORT bool cerver_http_parser_keep_alive (
	const CerverHttpParser *parser
);

/**
 * @brief Returns whether a message has a body to read: chunked, a Content-Length that is not 0, or a response
 * whose body ends with the connection.
 *
 * @param[in] parser the parser, after the head of the message
 * @return true if the message has a body
 * @since 3.0b-9
 */
CERVER_EXPORT bool cerver_http_parser_has_body (
	const CerverHttpParser *parser
);

/**
 * @brief Moves the head of a parser: its spans are relative to the new base, a copy of the bytes of the head.
 *
 * @param[in,out] parser the parser
 * @param[in] base the copy of the head
 * @since 3.0b-9
 */
CERVER_EXPORT void cerver_http_parser_rebase (
	CerverHttpParser *parser, const char *base
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
