/**
 * @file cerver/http/requests/multipart.h
 * @brief The parts of a multipart body, their files and their values, and the parser of the body.
 */

#ifndef _CERVER_HTTP_MULTIPART_H_
#define _CERVER_HTTP_MULTIPART_H_

#include <stdbool.h>
#include <stdlib.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/headers/header.h"

/**
 * @defgroup http_multipart Multipart
 * @ingroup http_requests
 * @brief The parts of a multipart/form-data body.
 *
 * The getters of a part take a part that is not NULL.
 * @{
 */

/** @brief The size of the boundary of a body, with its leading "--" and its terminator. */
#define HTTP_MULTI_PART_BOUNDARY_MAX_SIZE				128
/**
 * @brief The size of the id of a boundary of WebKit.
 *
 * @note cerver does not use it.
 */
#define HTTP_MULTI_PART_WEBKIT_BOUNDARY_ID_SIZE			16

/**
 * @brief The size of a temporary header of a part.
 *
 * @note cerver does not use it.
 */
#define HTTP_MULTI_PART_TEMP_HEADER_SIZE				32

/** @brief The size of the directory of the uploads of a request, with the uploads path. */
#define HTTP_MULTI_PART_DIRNAME_SIZE					1024
/** @brief The size of the sanitized original filename of a part. */
#define HTTP_MULTI_PART_FILENAME_SIZE					256
/** @brief The size of the filename that a generator gives to a part. */
#define HTTP_MULTI_PART_GENERATED_FILENAME_SIZE			512
/** @brief The size of the path where the file of a part is saved. */
#define HTTP_MULTI_PART_SAVED_FILENAME_SIZE				1024
/** @brief The size of the value of a part that is not a file. */
#define HTTP_MULTI_PART_VALUE_SIZE						256

/** @brief The size of the destination that http_multi_part_move_file_generate_destination () generates. */
#define HTTP_MULTI_PART_GENERATED_DESTINATION_SIZE		512

/** @brief The numbers that are tried when a saved filename already exists ("name-1.png", ...). */
#define HTTP_MULTI_PART_FILENAME_NUMBERS				(1 << 6)

/** @brief The name of a file whose name is empty after it was sanitized, or that refers to a directory. */
#define HTTP_MULTI_PART_DEFAULT_FILENAME				"file"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief What a part has: a file or a value.
 */
typedef enum MultiPartType {

	/** @brief The headers of the part have not been read. */
	MULTI_PART_TYPE_NONE						= 0,
	/** @brief A file. */
	MULTI_PART_TYPE_FILE						= 1,
	/** @brief A value. */
	MULTI_PART_TYPE_VALUE						= 2

} MultiPartType;

/**
 * @brief The headers of a part that are kept.
 */
typedef enum MultiPartHeader {

	/** @brief Content-Disposition. */
	MULTI_PART_HEADER_CONTENT_DISPOSITION		= 0,
	/** @brief Content-Length. */
	MULTI_PART_HEADER_CONTENT_LENGTH			= 1,
	/** @brief Content-Type. */
	MULTI_PART_HEADER_CONTENT_TYPE				= 2,

	/** @brief Any other header, that is not kept. */
	MULTI_PART_HEADER_INVALID					= 4

} MultiPartHeader;

/** @brief The headers of a part. */
#define MULTI_PART_HEADERS_SIZE					4

/**
 * @brief A part of a multipart body.
 */
struct _MultiPart {

	/** @brief What the part has. */
	MultiPartType type;

	/** @brief The header whose value is being received. */
	MultiPartHeader next_header;
	/** @brief The headers of the part, by MultiPartHeader. */
	HttpHeader headers[MULTI_PART_HEADERS_SIZE];

	/** @brief The name of the header that is being received. */
	HttpHeader temp_header;

	/** @brief The params of its Content-Disposition, as key value pairs. */
	DoubleList *params;

	/** @brief The name of the part, taken from #params; it must not be deleted. */
	const String *name;
	// const String *filename;

	/** @brief The length of #filename. */
	int filename_len;
	/** @brief The sanitized original filename. */
	char filename[HTTP_MULTI_PART_FILENAME_SIZE];

	/** @brief The length of #generated_filename. */
	int generated_filename_len;
	/** @brief The filename that the uploads filename generator gave to the part. */
	char generated_filename[HTTP_MULTI_PART_GENERATED_FILENAME_SIZE];

	/** @brief The fd of the file that is being written, -1 when there is none. */
	int fd;
	/** @brief A file input without a file, its data is ignored. */
	bool discard;
	/** @brief The receives that it took to read the file. */
	u32 n_reads;
	/** @brief The bytes written to the file. */
	u32 total_wrote;

	/** @brief The length of #saved_filename. */
	int saved_filename_len;
	/** @brief Where the file got saved: the uploads path, the directory of the request and the filename. */
	char saved_filename[HTTP_MULTI_PART_SAVED_FILENAME_SIZE];

	/** @brief The file was moved with http_multi_part_move_file (). */
	bool moved_file;

	/** @brief The length of #value. */
	int value_len;
	/** @brief The value of a part that is not a file. */
	char value[HTTP_MULTI_PART_VALUE_SIZE];

	/** @brief The errno of the open () or write () that failed to save the file, 0 if the file was saved completely. */
	int error;

};

/** @brief A part of a multipart body, see struct _MultiPart. */
typedef struct _MultiPart MultiPart;

/**
 * @brief Allocates an empty part.
 *
 * @return the new part, NULL if it could not be allocated
 * @note For the internal methods of cerver: the parts come from a pool.
 */
CERVER_PRIVATE void *http_multi_part_new (void);

/**
 * @brief Releases a part and its params.
 *
 * @param[in] multi_part_ptr the part, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_multi_part_delete (
	void *multi_part_ptr
);

/**
 * @brief Empties a part to be used again; an open file is closed.
 *
 * @param[in,out] multi_part the part, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void http_multi_part_reset (
	MultiPart *multi_part
);

/**
 * @brief Returns whether a part has something: a file part with a name, a filename or a saved filename; a value part
 * with a name or a value.
 *
 * @param[in] multi_part the part
 * @return true if it is not empty, false for a part of type MULTI_PART_TYPE_NONE
 */
CERVER_EXPORT bool http_multi_part_is_not_empty (
	const MultiPart *multi_part
);

/**
 * @brief Returns the type of a part.
 *
 * @param[in] multi_part the part
 * @return its type
 */
CERVER_PUBLIC MultiPartType http_multi_part_get_type (
	const MultiPart *multi_part
);

/**
 * @brief Returns whether a part is of type MULTI_PART_TYPE_FILE.
 *
 * @param[in] multi_part the part
 * @return true for a file
 */
CERVER_PUBLIC bool http_multi_part_is_file (
	const MultiPart *multi_part
);

/**
 * @brief Returns whether a part is of type MULTI_PART_TYPE_VALUE.
 *
 * @param[in] multi_part the part
 * @return true for a value
 */
CERVER_PUBLIC bool http_multi_part_is_value (
	const MultiPart *multi_part
);

/**
 * @brief Returns the name of a part.
 *
 * @param[in] multi_part the part
 * @return the name, that belongs to the part; NULL if it has none
 */
CERVER_PUBLIC const String *http_multi_part_get_name (
	const MultiPart *multi_part
);

/**
 * @brief Returns the sanitized original filename of a part.
 *
 * @param[in] multi_part the part
 * @return the filename, that belongs to the part
 */
CERVER_PUBLIC const char *http_multi_part_get_filename (
	const MultiPart *multi_part
);

/**
 * @brief Returns the length of the sanitized original filename of a part.
 *
 * @param[in] multi_part the part
 * @return the length
 */
CERVER_PUBLIC int http_multi_part_get_filename_len (
	const MultiPart *multi_part
);

/**
 * @brief Returns the filename that the uploads filename generator gave to a part.
 *
 * @param[in] multi_part the part
 * @return the filename, that belongs to the part; empty if it has none
 */
CERVER_PUBLIC const char *http_multi_part_get_generated_filename (
	const MultiPart *multi_part
);

/**
 * @brief Returns the length of the generated filename of a part.
 *
 * @param[in] multi_part the part
 * @return the length
 */
CERVER_PUBLIC int http_multi_part_get_generated_filename_len (
	const MultiPart *multi_part
);

/**
 * @brief Sets the generated filename of a part from a format; the uploads filename generators call it.
 *
 * @param[in] multi_part the part, NULL is ignored
 * @param[in] format the printf () format of the filename, NULL is ignored
 * @param[in] ... the arguments of the format
 * @note A filename that does not fit is truncated and its length is the one that was kept; a format that fails
 * leaves it empty, so the sanitized filename is used.
 */
CERVER_PUBLIC void http_multi_part_set_generated_filename (
	const MultiPart *multi_part, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Returns the receives that it took to read the file of a part.
 *
 * @param[in] multi_part the part
 * @return the receives
 */
CERVER_PUBLIC u32 http_multi_part_get_n_reads (
	const MultiPart *multi_part
);

/**
 * @brief Returns the bytes written to the file of a part.
 *
 * @param[in] multi_part the part
 * @return the bytes
 */
CERVER_PUBLIC u32 http_multi_part_get_total_wrote (
	const MultiPart *multi_part
);

/**
 * @brief Returns the errno of the open () or write () that failed to save the file of a part.
 *
 * @param[in] multi_part the part
 * @return the errno, 0 if the file was saved completely
 */
CERVER_PUBLIC int http_multi_part_get_error (
	const MultiPart *multi_part
);

/**
 * @brief Returns the path where the file of a part was saved.
 *
 * @param[in] multi_part the part
 * @return the path, that belongs to the part; empty if it was not saved
 */
CERVER_PUBLIC const char *http_multi_part_get_saved_filename (
	const MultiPart *multi_part
);

/**
 * @brief Returns the length of the saved filename of a part.
 *
 * @param[in] multi_part the part
 * @return the length
 */
CERVER_PUBLIC int http_multi_part_get_saved_filename_len (
	const MultiPart *multi_part
);

/**
 * @brief Returns whether the file of a part was moved with http_multi_part_move_file ().
 *
 * @param[in] multi_part the part
 * @return true if it was moved
 */
CERVER_PUBLIC bool http_multi_part_get_moved_file (
	const MultiPart *multi_part
);

/**
 * @brief Returns the value of a part that is not a file.
 *
 * @param[in] multi_part the part
 * @return the value, that belongs to the part
 */
CERVER_PUBLIC const char *http_multi_part_get_value (
	const MultiPart *multi_part
);

/**
 * @brief Returns the length of the value of a part.
 *
 * @param[in] multi_part the part
 * @return the length
 */
CERVER_PUBLIC int http_multi_part_get_value_len (
	const MultiPart *multi_part
);

/**
 * @brief Moves the saved file of a part to a destination path.
 *
 * @param[in,out] mpart the part, it is marked as moved
 * @param[in] destination_path the destination
 * @retval 0 the file was moved
 * @retval 1 a NULL argument or the file could not be moved
 */
CERVER_PUBLIC unsigned int http_multi_part_move_file (
	MultiPart *mpart, const char *destination_path
);

/**
 * @brief Works like http_multi_part_move_file () with a destination generated from a format.
 *
 * @param[in,out] mpart the part
 * @param[in] format the printf () format of the destination, truncated to
 * #HTTP_MULTI_PART_GENERATED_DESTINATION_SIZE - 1 characters
 * @param[in] ... the arguments of the format
 * @retval 0 the file was moved
 * @retval 1 a NULL argument or the file could not be moved
 */
CERVER_PUBLIC unsigned int http_multi_part_move_file_generate_destination (
	MultiPart *mpart, const char *format, ...
) CERVER_PRINTF_FORMAT (2, 3);

/**
 * @brief Logs the headers of a part.
 *
 * @param[in] mpart the part, NULL prints nothing
 */
CERVER_PUBLIC void http_multi_part_headers_print (
	const MultiPart *mpart
);

/**
 * @brief Prints the values of a part to stdout.
 *
 * @param[in] mpart the part, NULL prints nothing
 */
CERVER_PUBLIC void http_multi_part_print (
	const MultiPart *mpart
);

/** @} */

/**
 * @defgroup http_multipart_parser Multipart parser
 * @ingroup http_requests
 * @brief The parser of a multipart body, for the internal methods of cerver.
 * @{
 */

struct multipart_parser;

/**
 * @brief A callback of the parser with a piece of data.
 *
 * @return 0 to continue, not 0 to stop the parser
 */
typedef int (*multipart_data_cb) (
	struct multipart_parser *,
	const char *at,
	size_t length
);

/**
 * @brief A callback of the parser that notifies an event.
 *
 * @return 0 to continue, not 0 to stop the parser
 */
typedef int (*multipart_notify_cb) (
	struct multipart_parser *
);

/**
 * @brief The callbacks of the parser.
 */
struct multipart_parser_settings {

	/** @brief A piece of the name of a header of a part. */
	multipart_data_cb on_header_field;
	/** @brief A piece of the value of a header of a part. */
	multipart_data_cb on_header_value;
	/** @brief A piece of the data of a part. */
	multipart_data_cb on_part_data;

	/** @brief A part begins. */
	multipart_notify_cb on_part_data_begin;
	/** @brief The headers of a part are complete. */
	multipart_notify_cb on_headers_complete;
	/** @brief The data of a part ends. */
	multipart_notify_cb on_part_data_end;
	/** @brief The body ends. */
	multipart_notify_cb on_body_end;

};

/** @brief The callbacks of the parser, see struct multipart_parser_settings. */
typedef struct multipart_parser_settings multipart_parser_settings;

/**
 * @brief The parser of a multipart body.
 */
struct multipart_parser {

	/** @brief The data of its owner, the receive of the request. */
	void *data;

	/** @brief The index in the boundary that is being matched. */
	size_t index;
	/** @brief The length of the boundary. */
	size_t boundary_length;

	/** @brief The state of the parser. */
	unsigned char state;

	/** @brief The callbacks. */
	const multipart_parser_settings *settings;

	/** @brief The bytes that could be the boundary, after the boundary in the same allocation. */
	char *lookbehind;
	/** @brief The boundary, allocated with the parser. */
	char multipart_boundary[1];

};

/** @brief The parser of a multipart body, see struct multipart_parser. */
typedef struct multipart_parser multipart_parser;

/**
 * @brief Creates a parser for a boundary.
 *
 * @param[in] boundary the boundary, with its leading "--"; it must not be NULL
 * @param[in] settings the callbacks, they must live as long as the parser
 * @return the new parser, released with multipart_parser_free (); NULL if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE multipart_parser *multipart_parser_init (
	const char *boundary, const multipart_parser_settings *settings
);

/**
 * @brief Releases a parser.
 *
 * @param[in] p the parser
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void multipart_parser_free (
	multipart_parser *p
);

/**
 * @brief Parses a piece of a body and calls the callbacks.
 *
 * @param[in,out] p the parser
 * @param[in] buf the piece
 * @param[in] len its bytes
 * @return the bytes that were parsed, less than len when a callback stopped the parser or the body is invalid
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE size_t multipart_parser_execute (
	multipart_parser *p, const char *buf, size_t len
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
