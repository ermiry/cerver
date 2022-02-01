#ifndef _CERVER_HTTP_MULTIPART_H_
#define _CERVER_HTTP_MULTIPART_H_

#include <stdlib.h>
#include <stdbool.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#include "cerver/http/headers.h"

#define HTTP_MULTI_PART_BOUNDARY_MAX_SIZE				128
#define HTTP_MULTI_PART_WEBKIT_BOUNDARY_ID_SIZE			16

#define HTTP_MULTI_PART_TEMP_HEADER_SIZE				32

#define HTTP_MULTI_PART_DIRNAME_SIZE					1024
#define HTTP_MULTI_PART_FILENAME_SIZE					256
#define HTTP_MULTI_PART_GENERATED_FILENAME_SIZE			512
#define HTTP_MULTI_PART_SAVED_FILENAME_SIZE				1024
#define HTTP_MULTI_PART_VALUE_SIZE						256

#define HTTP_MULTI_PART_GENERATED_DESTINATION_SIZE		512

#ifdef __cplusplus
extern "C" {
#endif

#pragma region parts

typedef enum MultiPartType {

	MULTI_PART_TYPE_NONE						= 0,
	MULTI_PART_TYPE_FILE						= 1,
	MULTI_PART_TYPE_VALUE						= 2

} MultiPartType;

typedef enum MultiPartHeader {

	MULTI_PART_HEADER_CONTENT_DISPOSITION		= 0,
	MULTI_PART_HEADER_CONTENT_LENGTH			= 1,
	MULTI_PART_HEADER_CONTENT_TYPE				= 2,

	MULTI_PART_HEADER_INVALID					= 4

} MultiPartHeader;

#define MULTI_PART_HEADERS_SIZE					4

struct _MultiPart {

	MultiPartType type;

	MultiPartHeader next_header;
	HttpHeader headers[MULTI_PART_HEADERS_SIZE];

	HttpHeader temp_header;

	DoubleList *params;

	// taken from params - do not delete
	const String *name;
	// const String *filename;

	// sanitized original filename
	int filename_len;
	char filename[HTTP_MULTI_PART_FILENAME_SIZE];

	int generated_filename_len;
	char generated_filename[HTTP_MULTI_PART_GENERATED_FILENAME_SIZE];

	int fd;
	u32 n_reads;				// amount of loops it took to read the file - based on cerver receive value
	u32 total_wrote;			// the total ammount of bytes written to the file

	// how the file got saved (uploads path + filename)
	int saved_filename_len;
	char saved_filename[HTTP_MULTI_PART_SAVED_FILENAME_SIZE];

	bool moved_file;

	int value_len;
	char value[HTTP_MULTI_PART_VALUE_SIZE];

};

typedef struct _MultiPart MultiPart;

CERVER_PRIVATE void *http_multi_part_new (void);

CERVER_PRIVATE void http_multi_part_delete (
	void *multi_part_ptr
);

CERVER_PRIVATE void http_multi_part_reset (
	MultiPart *multi_part
);

// returns the multi-part's type
CERVER_PUBLIC const MultiPartType http_multi_part_get_type (
	const MultiPart *multi_part
);

// returns true if the multi-part's type is MULTI_PART_TYPE_FILE
CERVER_PUBLIC bool http_multi_part_is_file (
	const MultiPart *multi_part
);

// returns true if the multi-part's type is MULTI_PART_TYPE_VALUE
CERVER_PUBLIC bool http_multi_part_is_value (
	const MultiPart *multi_part
);

// returns the multi-part's name
CERVER_PUBLIC const String *http_multi_part_get_name (
	const MultiPart *multi_part
);

// returns the multi-part's sanitized original filename
CERVER_PUBLIC const char *http_multi_part_get_filename (
	const MultiPart *multi_part
);

// returns the multi-part's sanitized original filename length
CERVER_PUBLIC const int http_multi_part_get_filename_len (
	const MultiPart *multi_part
);

// returns the multi-part's generated filename
CERVER_PUBLIC const char *http_multi_part_get_generated_filename (
	const MultiPart *multi_part
);

// returns the multi-part's generated filename length
CERVER_PUBLIC const int http_multi_part_get_generated_filename_len (
	const MultiPart *multi_part
);

// sets the HTTP request's multi-part generated filename
CERVER_PUBLIC void http_multi_part_set_generated_filename (
	const MultiPart *multi_part, const char *format, ...
);

// returns the multi-part's number of reads value
// the amount of loops it took to read the file
CERVER_PUBLIC const u32 http_multi_part_get_n_reads (
	const MultiPart *multi_part
);

// returns the multi-part's total ammount of bytes written to the file
CERVER_PUBLIC const u32 http_multi_part_get_total_wrote (
	const MultiPart *multi_part
);

// returns the multi-part's saved filename
CERVER_PUBLIC const char *http_multi_part_get_saved_filename (
	const MultiPart *multi_part
);

// returns the multi-part's saved filename length
CERVER_PUBLIC const int http_multi_part_get_saved_filename_len (
	const MultiPart *multi_part
);

CERVER_PUBLIC const bool http_multi_part_get_moved_file (
	const MultiPart *multi_part
);

// returns the multi-part's value
CERVER_PUBLIC const char *http_multi_part_get_value (
	const MultiPart *multi_part
);

// returns the multi-part's value length
CERVER_PUBLIC const int http_multi_part_get_value_len (
	const MultiPart *multi_part
);

// moves multi-part's saved file to the destination path
// returns 0 on success, 1 on error
CERVER_PUBLIC unsigned int http_multi_part_move_file (
	MultiPart *mpart, const char *destination_path
);

// works like http_multi_part_move_file ()
// but with the ability to generate dest on the fly
CERVER_PUBLIC unsigned int http_multi_part_move_file_generate_destination (
	MultiPart *mpart, const char *format, ...
);

CERVER_PUBLIC void http_multi_part_headers_print (
	const MultiPart *mpart
);

CERVER_PUBLIC void http_multi_part_print (
	const MultiPart *mpart
);

#pragma endregion

#pragma region parser

struct multipart_parser;

typedef int (*multipart_data_cb) (
	struct multipart_parser *,
	const char *at,
	size_t length
);

typedef int (*multipart_notify_cb) (
	struct multipart_parser *
);

struct multipart_parser_settings {

	multipart_data_cb on_header_field;
	multipart_data_cb on_header_value;
	multipart_data_cb on_part_data;

	multipart_notify_cb on_part_data_begin;
	multipart_notify_cb on_headers_complete;
	multipart_notify_cb on_part_data_end;
	multipart_notify_cb on_body_end;
	
};

typedef struct multipart_parser_settings multipart_parser_settings;

struct multipart_parser {

	void *data;

	size_t index;
	size_t boundary_length;

	unsigned char state;

	const multipart_parser_settings *settings;

	char *lookbehind;
	char multipart_boundary[1];

};

typedef struct multipart_parser multipart_parser;

CERVER_PRIVATE multipart_parser *multipart_parser_init (
	const char *boundary, const multipart_parser_settings *settings
);

CERVER_PRIVATE void multipart_parser_free (
	multipart_parser *p
);

CERVER_PRIVATE size_t multipart_parser_execute (
	multipart_parser *p, const char *buf, size_t len
);

#pragma endregion

#ifdef __cplusplus
}
#endif

#endif