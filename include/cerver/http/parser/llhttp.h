#ifndef _CERVER_HTTP_PARSER_LLHTTP_H_
#define _CERVER_HTTP_PARSER_LLHTTP_H_

#include <stdint.h>

#include "cerver/config.h"

#define LLHTTP_VERSION_MAJOR	6
#define LLHTTP_VERSION_MINOR	0
#define LLHTTP_VERSION_PATCH	6

#ifdef __cplusplus
extern "C" {
#endif

struct llhttp__internal_s {

	int32_t _index;
	void *_span_pos0;
	void *_span_cb0;
	int32_t error;
	const char *reason;
	const char *error_pos;
	void *data;
	void *_current;
	uint64_t content_length;
	uint8_t type;
	uint8_t method;
	uint8_t http_major;
	uint8_t http_minor;
	uint8_t header_state;
	uint8_t lenient_flags;
	uint8_t upgrade;
	uint8_t finish;
	uint16_t flags;
	uint16_t status_code;
	void *settings;

};

typedef struct llhttp__internal_s llhttp__internal_t;

CERVER_PRIVATE int llhttp__internal_init (llhttp__internal_t *s);

CERVER_PRIVATE int llhttp__internal_execute (
	llhttp__internal_t *s, const char *p, const char *endp
);

#ifdef __cplusplus
}
#endif

#endif
