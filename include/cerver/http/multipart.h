#ifndef _CERVER_HTTP_MULTIPART_H_
#define _CERVER_HTTP_MULTIPART_H_

#include <stdlib.h>

#include "cerver/config.h"

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

CERVER_PRIVATE multipart_parser *multipart_parser_init (const char *boundary, const multipart_parser_settings *settings);

CERVER_PRIVATE void multipart_parser_free (multipart_parser* p);

CERVER_PRIVATE size_t multipart_parser_execute (multipart_parser *p, const char *buf, size_t len);

#endif