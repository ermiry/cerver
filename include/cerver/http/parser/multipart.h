/**
 * @file cerver/http/parser/multipart.h
 * @brief The parser of a multipart body (RFC 2046 section 5.1, RFC 7578): its boundaries, the headers of its parts
 * and their data.
 */

#ifndef _CERVER_HTTP_PARSER_MULTIPART_H_
#define _CERVER_HTTP_PARSER_MULTIPART_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_parser_multipart Multipart
 * @ingroup http_parser
 * @brief Reads a multipart body in any piece and gives its parts to callbacks.
 *
 * The body starts with its boundary; each part has its headers (a name of letters and '-', a value until its CR)
 * and its data until the CRLF and the boundary that follow it, and the body ends with the boundary and "--". The
 * parser has no allocations: it lives in the memory of its caller, with a copy of its boundary.
 * @{
 */

/** @brief The bytes of a boundary with its leading "--" that a parser takes (RFC 2046: at most 70 after them). */
#define CERVER_HTTP_MULTIPART_BOUNDARY_SIZE		(1 << 7)

struct CerverHttpMultipart;

/**
 * @brief A callback with a piece of a body: a piece of the name or of the value of a header, or of the data.
 *
 * @return 0 to continue, not 0 to stop the parser
 * @since 3.0b-9
 */
typedef int (*CerverHttpMultipartData) (struct CerverHttpMultipart *, const char *at, size_t length);

/**
 * @brief A callback of an event of a body: a part begins, its headers are complete, its data ends, the body ends.
 *
 * @return 0 to continue, not 0 to stop the parser
 * @since 3.0b-9
 */
typedef int (*CerverHttpMultipartEvent) (struct CerverHttpMultipart *);

/**
 * @brief The callbacks of a parser, any of them can be NULL.
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpMultipartCallbacks {

	/** @brief A piece of the name of a header of a part. */
	CerverHttpMultipartData on_header_field;
	/** @brief A piece of the value of a header of a part. */
	CerverHttpMultipartData on_header_value;
	/** @brief A piece of the data of a part. */
	CerverHttpMultipartData on_part_data;

	/** @brief A part begins. */
	CerverHttpMultipartEvent on_part_data_begin;
	/** @brief The headers of a part are complete. */
	CerverHttpMultipartEvent on_headers_complete;
	/** @brief The data of a part ends. */
	CerverHttpMultipartEvent on_part_data_end;
	/** @brief The body ends. */
	CerverHttpMultipartEvent on_body_end;

} CerverHttpMultipartCallbacks;

/**
 * @brief A parser of a multipart body, in the memory of its caller.
 *
 * @since 3.0b-9
 */
typedef struct CerverHttpMultipart {

	/** @brief The data of its owner, for the callbacks. */
	void *data;

	/** @brief The callbacks, they live as long as the parser. */
	const CerverHttpMultipartCallbacks *callbacks;

	/** @brief Where the parser is, for its internal methods. */
	unsigned int state;

	/** @brief The bytes of the boundary that were matched. */
	size_t index;

	/** @brief The bytes of the boundary. */
	size_t boundary_length;
	/** @brief The boundary, with its leading "--". */
	char boundary[CERVER_HTTP_MULTIPART_BOUNDARY_SIZE];

	/** @brief The bytes that can be a boundary after the data of a part: CRLF and the bytes of the boundary. */
	char lookbehind[CERVER_HTTP_MULTIPART_BOUNDARY_SIZE + 2];

} CerverHttpMultipart;

/**
 * @brief Starts a parser for a boundary.
 *
 * @param[out] multipart the parser
 * @param[in] boundary the boundary with its leading "--", it is copied
 * @param[in] length the bytes of the boundary
 * @param[in] callbacks the callbacks, they must live as long as the parser
 * @param[in] data the data of its owner
 * @retval true the parser is ready
 * @retval false the boundary is empty or does not fit in #CERVER_HTTP_MULTIPART_BOUNDARY_SIZE
 * @since 3.0b-9
 */
CERVER_EXPORT bool cerver_http_multipart_init (
	CerverHttpMultipart *multipart,
	const char *boundary, const size_t length,
	const CerverHttpMultipartCallbacks *callbacks, void *data
);

/**
 * @brief Reads a piece of a body and calls the callbacks.
 *
 * @param[in,out] multipart the parser
 * @param[in] buffer the piece
 * @param[in] length its bytes
 * @return the bytes that were read: less than length when a callback stopped the parser or the body is not valid
 * @since 3.0b-9
 */
CERVER_EXPORT size_t cerver_http_multipart_execute (
	CerverHttpMultipart *multipart, const char *buffer, const size_t length
);

/**
 * @brief Returns whether the parser read the end of the body (its last boundary).
 *
 * @param[in] multipart the parser
 * @return true after the last boundary
 * @since 3.0b-9
 */
CERVER_EXPORT bool cerver_http_multipart_complete (
	const CerverHttpMultipart *multipart
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
