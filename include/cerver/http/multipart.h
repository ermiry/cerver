#ifndef _CERVER_HTTP_MULTIPART_H_
#define _CERVER_HTTP_MULTIPART_H_

#include <stdlib.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/config.h"

#define HTTP_MULTI_PART_FILENAME_LEN					256
#define HTTP_MULTI_PART_GENERATED_FILENAME_LEN			512
#define HTTP_MULTI_PART_SAVED_FILENAME_LEN				1024

#ifdef __cplusplus
extern "C" {
#endif

#pragma region parts

typedef enum MultiPartHeader {

	MULTI_PART_HEADER_CONTENT_DISPOSITION				= 0,
	MULTI_PART_HEADER_CONTENT_LENGTH					= 1,
	MULTI_PART_HEADER_CONTENT_TYPE						= 2,

	MULTI_PART_HEADER_INVALID							= 4

} MultiPartHeader;

#define MULTI_PART_HEADERS_SIZE				4

struct _MultiPart {

	MultiPartHeader next_header;
	String *headers[MULTI_PART_HEADERS_SIZE];

	DoubleList *params;

	// taken from params - do not delete
	const String *name;
	// const String *filename;

	// sanitized original filename
	int filename_len;
	char filename[HTTP_MULTI_PART_FILENAME_LEN];

	int generated_filename_len;
	char generated_filename[HTTP_MULTI_PART_GENERATED_FILENAME_LEN];

	int fd;
	// how the file got saved (uploads path + filename)
	int saved_filename_len;
	char saved_filename[HTTP_MULTI_PART_SAVED_FILENAME_LEN];
	u32 n_reads;				// amount to loops it took to read the file - based on cerver receive value
	u32 total_wrote;			// the total ammount of bytes wrote to the file

	String *value;

};

typedef struct _MultiPart MultiPart;

CERVER_PUBLIC MultiPart *http_multi_part_new (void);

CERVER_PUBLIC void http_multi_part_delete (void *multi_part_ptr);

CERVER_PUBLIC void http_multi_part_headers_print (MultiPart *mpart);

CERVER_PUBLIC void http_multi_part_print (MultiPart *mpart);

#pragma endregion

#pragma region parser

struct multipart_parser;

typedef int (*multipart_data_cb) (struct multipart_parser *, const char *at, size_t length);
typedef int (*multipart_notify_cb) (struct multipart_parser *);

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