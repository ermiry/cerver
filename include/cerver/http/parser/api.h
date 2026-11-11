/*
 * llhttp 9.4.3 (https://github.com/nodejs/llhttp), split and restyled for cerver
 * the API of the parser, the parser is embedded in HttpReceive
 *
 * This software is licensed under the MIT License.
 *
 * Copyright Fedor Indutny, 2018.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to permit
 * persons to whom the Software is furnished to do so, subject to the
 * following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 * NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef _CERVER_HTTP_PARSER_API_H_
#define _CERVER_HTTP_PARSER_API_H_

#include <stddef.h>
#include <stdint.h>

#include "cerver/config.h"

#include "cerver/http/parser/helpers.h"
#include "cerver/http/parser/llhttp.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef llhttp__internal_t llhttp_t;
typedef struct llhttp_settings_s llhttp_settings_t;

typedef int (*llhttp_data_cb)(llhttp_t *, const char *at, size_t length);
typedef int (*llhttp_cb)(llhttp_t *);

struct llhttp_settings_s {

	// possible return values 0, -1, HPE_PAUSED
	llhttp_cb on_message_begin;

	// possible return values 0, -1, HPE_USER
	llhttp_data_cb on_protocol;
	llhttp_data_cb on_url;
	llhttp_data_cb on_status;
	llhttp_data_cb on_method;
	llhttp_data_cb on_version;
	llhttp_data_cb on_header_field;
	llhttp_data_cb on_header_value;
	llhttp_data_cb on_chunk_extension_name;
	llhttp_data_cb on_chunk_extension_value;

	// possible return values:
	// 0 - proceed normally
	// 1 - assume that the request has no body, and proceed to parse the next message
	// 2 - assume the absence of a body (as above) and make llhttp_execute () return HPE_PAUSED_UPGRADE
	// -1 - error
	// HPE_PAUSED
	llhttp_cb on_headers_complete;

	// possible return values 0, -1, HPE_USER
	llhttp_data_cb on_body;

	// possible return values 0, -1, HPE_PAUSED
	llhttp_cb on_message_complete;
	llhttp_cb on_protocol_complete;
	llhttp_cb on_url_complete;
	llhttp_cb on_status_complete;
	llhttp_cb on_method_complete;
	llhttp_cb on_version_complete;
	llhttp_cb on_header_field_complete;
	llhttp_cb on_header_value_complete;
	llhttp_cb on_chunk_extension_name_complete;
	llhttp_cb on_chunk_extension_value_complete;

	// when on_chunk_header is called, the current chunk length is stored in parser->content_length
	// possible return values 0, -1, HPE_PAUSED
	llhttp_cb on_chunk_header;
	llhttp_cb on_chunk_complete;
	llhttp_cb on_reset;

};

// initializes the parser with its type and the settings
// the settings must live at least as long as the parser
CERVER_PUBLIC void llhttp_init (
	llhttp_t *parser, llhttp_type_t type,
	const llhttp_settings_t *settings
);

CERVER_PUBLIC uint8_t llhttp_get_type (const llhttp_t *parser);

CERVER_PUBLIC uint8_t llhttp_get_http_major (const llhttp_t *parser);

CERVER_PUBLIC uint8_t llhttp_get_http_minor (const llhttp_t *parser);

CERVER_PUBLIC uint8_t llhttp_get_method (const llhttp_t *parser);

CERVER_PUBLIC int llhttp_get_status_code (const llhttp_t *parser);

CERVER_PUBLIC uint8_t llhttp_get_upgrade (const llhttp_t *parser);

// resets an already initialized parser back to the start state,
// preserving its type, its settings, its data and its lenient flags
CERVER_PUBLIC void llhttp_reset (llhttp_t *parser);

// initializes the settings without callbacks
CERVER_PUBLIC void llhttp_settings_init (llhttp_settings_t *settings);

// parses a full or a partial request, invoking the callbacks of the settings
// if a callback returns an error, the parsing stops and that error is returned,
// after HPE_PAUSED the parsing can be resumed with llhttp_resume ()
// a CONNECT or an Upgrade request returns HPE_PAUSED_UPGRADE after it was parsed
// after any other error, it returns the same error until llhttp_init () is called again
CERVER_PUBLIC llhttp_errno_t llhttp_execute (
	llhttp_t *parser, const char *data, size_t len
);

// called when the other side has no further bytes to send (EOF)
// it invokes on_message_complete () if the message ended safely,
// returns HPE_INVALID_EOF_STATE if the message was incomplete
CERVER_PUBLIC llhttp_errno_t llhttp_finish (llhttp_t *parser);

// returns 1 if the message has to be completed by calling llhttp_finish () on EOF
CERVER_PUBLIC int llhttp_message_needs_eof (const llhttp_t *parser);

// returns 1 if there might be other messages after the last one that was parsed
CERVER_PUBLIC int llhttp_should_keep_alive (const llhttp_t *parser);

// makes the next calls of llhttp_execute () return HPE_PAUSED
// it must not be called from a callback, a callback returns HPE_PAUSED instead
CERVER_PUBLIC void llhttp_pause (llhttp_t *parser);

// resumes the parser after llhttp_execute () returned HPE_PAUSED
CERVER_PUBLIC void llhttp_resume (llhttp_t *parser);

// resumes the parser after llhttp_execute () returned HPE_PAUSED_UPGRADE
CERVER_PUBLIC void llhttp_resume_after_upgrade (llhttp_t *parser);

// returns the latest error
CERVER_PUBLIC llhttp_errno_t llhttp_get_errno (const llhttp_t *parser);

// returns the explanation of the latest error
CERVER_PUBLIC const char *llhttp_get_error_reason (const llhttp_t *parser);

// sets the explanation of the error that a callback returns,
// it must be called right before the callback returns the error
CERVER_PUBLIC void llhttp_set_error_reason (
	llhttp_t *parser, const char *reason
);

// returns the position of the last byte that was parsed before the error,
// relative to the data of llhttp_execute (), so it can count the parsed bytes
CERVER_PUBLIC const char *llhttp_get_error_pos (const llhttp_t *parser);

// returns the name of an error ("HPE_OK", "HPE_INVALID_METHOD", ...)
CERVER_PUBLIC const char *llhttp_errno_name (const llhttp_errno_t err);

// each lenient flag can open the parser to request smuggling or cache poisoning attacks,
// they are disabled by default (strict mode)
CERVER_PUBLIC void llhttp_set_lenient_headers (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_chunked_length (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_keep_alive (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_transfer_encoding (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_version (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_data_after_close (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_optional_lf_after_cr (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_optional_crlf_after_chunk (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_optional_cr_before_lf (llhttp_t *parser, int enabled);

CERVER_PUBLIC void llhttp_set_lenient_spaces_after_chunk_size (llhttp_t *parser, int enabled);

// llhttp_set_lenient_header_value_relaxed () is not included:
// in 9.4.3 a NUL byte in a header value makes llhttp_execute () loop forever with it

// sets all the lenient flags (llhttp_lenient_flags) at once,
// a flag outside llhttp_lenient_flags is ignored
CERVER_PUBLIC void llhttp_set_lenient_flags (llhttp_t *parser, uint16_t flags);

// all the flags of llhttp_lenient_flags
#define LLHTTP_LENIENT_ALL				0x3FF

#ifdef __cplusplus
}
#endif

#endif
