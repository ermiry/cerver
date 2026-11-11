/*
 * llhttp 9.4.3 (https://github.com/nodejs/llhttp), split and restyled for cerver
 * the API of the parser and the callbacks that the generated parser invokes,
 * without the wasm build, llhttp_alloc (), llhttp_free (), llhttp_method_name () and llhttp_status_name ()
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

#include <stdlib.h>
#include <string.h>

#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/parser/llhttp.h"

#define LLHTTP_PAUSED_REASON					"Paused"
#define LLHTTP_INVALID_EOF_STATE_REASON			"Invalid EOF state"
#define LLHTTP_UNKNOWN_ERRNO_NAME				"HPE_UNKNOWN"

// the callbacks that the generated parser invokes, with the bytes of the span
// (llhttp declares them with unsigned bytes in the parser and signed ones in its api)
typedef const unsigned char *llhttp_span;

static inline const llhttp_settings_t *llhttp_settings_get (const llhttp_t *parser) {

	return (const llhttp_settings_t *) parser->settings;

}

#define CALLBACK_MAYBE(PARSER, NAME)											\
	do {																		\
		const llhttp_settings_t *settings = llhttp_settings_get (PARSER);		\
		if (settings == NULL || settings->NAME == NULL) {						\
			err = 0;															\
			break;																\
		}																		\
		err = settings->NAME ((PARSER));										\
	} while (0)

#define SPAN_CALLBACK_MAYBE(PARSER, NAME, START, LEN)							\
	do {																		\
		const llhttp_settings_t *settings = llhttp_settings_get (PARSER);		\
		if (settings == NULL || settings->NAME == NULL) {						\
			err = 0;															\
			break;																\
		}																		\
		err = settings->NAME ((PARSER), (const char *) (START), (LEN));			\
		if (err == -1) {														\
			err = HPE_USER;														\
			llhttp_set_error_reason ((PARSER), "Span callback error in " #NAME);	\
		}																		\
	} while (0)

#pragma region api

void llhttp_init (
	llhttp_t *parser, llhttp_type_t type,
	const llhttp_settings_t *settings
) {

	(void) llhttp__internal_init (parser);

	parser->type = (uint8_t) type;
	parser->settings = (void *) settings;

}

uint8_t llhttp_get_type (const llhttp_t *parser) {

	return parser->type;

}

uint8_t llhttp_get_http_major (const llhttp_t *parser) {

	return parser->http_major;

}

uint8_t llhttp_get_http_minor (const llhttp_t *parser) {

	return parser->http_minor;

}

uint8_t llhttp_get_method (const llhttp_t *parser) {

	return parser->method;

}

int llhttp_get_status_code (const llhttp_t *parser) {

	return parser->status_code;

}

uint8_t llhttp_get_upgrade (const llhttp_t *parser) {

	return parser->upgrade;

}

void llhttp_reset (llhttp_t *parser) {

	const uint8_t type = parser->type;
	void *settings = parser->settings;
	void *data = parser->data;
	const uint16_t lenient_flags = parser->lenient_flags;

	(void) llhttp__internal_init (parser);

	parser->type = type;
	parser->settings = settings;
	parser->data = data;
	parser->lenient_flags = lenient_flags;

}

llhttp_errno_t llhttp_execute (
	llhttp_t *parser, const char *data, size_t len
) {

	const int err = llhttp__internal_execute (parser, data, data + len);

	return (llhttp_errno_t) err;

}

void llhttp_settings_init (llhttp_settings_t *settings) {

	(void) memset (settings, 0, sizeof (*settings));

}

llhttp_errno_t llhttp_finish (llhttp_t *parser) {

	int err = HPE_OK;

	// in an error state, nothing is done
	if (parser->error != 0) return HPE_OK;

	switch (parser->finish) {
		case HTTP_FINISH_SAFE_WITH_CB:
			CALLBACK_MAYBE (parser, on_message_complete);
			if (err != HPE_OK) return (llhttp_errno_t) err;

			/* FALLTHROUGH */
		case HTTP_FINISH_SAFE:
			return HPE_OK;

		case HTTP_FINISH_UNSAFE:
			parser->reason = LLHTTP_INVALID_EOF_STATE_REASON;
			return HPE_INVALID_EOF_STATE;

		default:
			abort ();
	}

}

void llhttp_pause (llhttp_t *parser) {

	if (parser->error != HPE_OK) return;

	parser->error = HPE_PAUSED;
	parser->reason = LLHTTP_PAUSED_REASON;

}

void llhttp_resume (llhttp_t *parser) {

	if (parser->error != HPE_PAUSED) return;

	parser->error = 0;

}

void llhttp_resume_after_upgrade (llhttp_t *parser) {

	if (parser->error != HPE_PAUSED_UPGRADE) return;

	parser->error = 0;

}

llhttp_errno_t llhttp_get_errno (const llhttp_t *parser) {

	return (llhttp_errno_t) parser->error;

}

const char *llhttp_get_error_reason (const llhttp_t *parser) {

	return parser->reason;

}

void llhttp_set_error_reason (llhttp_t *parser, const char *reason) {

	parser->reason = reason;

}

const char *llhttp_get_error_pos (const llhttp_t *parser) {

	return parser->error_pos;

}

// a value outside HTTP_ERRNO_MAP (a callback that returns its own error) has a name too
const char *llhttp_errno_name (const llhttp_errno_t err) {

	switch (err) {
		#define XX(num, name, string) case HPE_##name: return "HPE_" #string;
		HTTP_ERRNO_MAP(XX)
		#undef XX

		default: break;
	}

	return LLHTTP_UNKNOWN_ERRNO_NAME;

}

static void llhttp_set_lenient (
	llhttp_t *parser, const llhttp_lenient_flags_t flag, const int enabled
) {

	if (enabled) parser->lenient_flags |= (uint16_t) flag;
	else parser->lenient_flags &= (uint16_t) ~flag;

}

void llhttp_set_lenient_headers (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_HEADERS, enabled);

}

void llhttp_set_lenient_chunked_length (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_CHUNKED_LENGTH, enabled);

}

void llhttp_set_lenient_keep_alive (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_KEEP_ALIVE, enabled);

}

void llhttp_set_lenient_transfer_encoding (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_TRANSFER_ENCODING, enabled);

}

void llhttp_set_lenient_version (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_VERSION, enabled);

}

void llhttp_set_lenient_data_after_close (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_DATA_AFTER_CLOSE, enabled);

}

void llhttp_set_lenient_optional_lf_after_cr (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_OPTIONAL_LF_AFTER_CR, enabled);

}

void llhttp_set_lenient_optional_crlf_after_chunk (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_OPTIONAL_CRLF_AFTER_CHUNK, enabled);

}

void llhttp_set_lenient_optional_cr_before_lf (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_OPTIONAL_CR_BEFORE_LF, enabled);

}

void llhttp_set_lenient_spaces_after_chunk_size (llhttp_t *parser, int enabled) {

	llhttp_set_lenient (parser, LENIENT_SPACES_AFTER_CHUNK_SIZE, enabled);

}

void llhttp_set_lenient_flags (llhttp_t *parser, uint16_t flags) {

	parser->lenient_flags = flags & LLHTTP_LENIENT_ALL;

}

#pragma endregion

#pragma region callbacks

// the prototypes of the callbacks are declared by the generated parser
int llhttp__on_message_begin (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_protocol (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_protocol_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_url (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_url_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_status (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_status_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_method (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_method_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_version (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_version_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_header_field (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_header_field_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_header_value (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_header_value_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_headers_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_message_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_body (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_chunk_header (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_chunk_extension_name (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_chunk_extension_name_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_chunk_extension_value (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_chunk_extension_value_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_chunk_complete (llhttp_t *s, llhttp_span p, llhttp_span endp);
int llhttp__on_reset (llhttp_t *s, llhttp_span p, llhttp_span endp);

int llhttp__on_message_begin (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_message_begin);
	return err;

}

int llhttp__on_protocol (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_protocol, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_protocol_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_protocol_complete);
	return err;

}

int llhttp__on_url (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_url, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_url_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_url_complete);
	return err;

}

int llhttp__on_status (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_status, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_status_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_status_complete);
	return err;

}

int llhttp__on_method (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_method, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_method_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_method_complete);
	return err;

}

int llhttp__on_version (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_version, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_version_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_version_complete);
	return err;

}

int llhttp__on_header_field (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_header_field, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_header_field_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_header_field_complete);
	return err;

}

int llhttp__on_header_value (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_header_value, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_header_value_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_header_value_complete);
	return err;

}

int llhttp__on_headers_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_headers_complete);
	return err;

}

int llhttp__on_message_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_message_complete);
	return err;

}

int llhttp__on_body (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_body, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_chunk_header (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_chunk_header);
	return err;

}

int llhttp__on_chunk_extension_name (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_chunk_extension_name, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_chunk_extension_name_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_chunk_extension_name_complete);
	return err;

}

int llhttp__on_chunk_extension_value (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	int err = 0;
	SPAN_CALLBACK_MAYBE (s, on_chunk_extension_value, p, (size_t) (endp - p));
	return err;

}

int llhttp__on_chunk_extension_value_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_chunk_extension_value_complete);
	return err;

}

int llhttp__on_chunk_complete (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_chunk_complete);
	return err;

}

int llhttp__on_reset (llhttp_t *s, llhttp_span p, llhttp_span endp) {

	(void) p;
	(void) endp;

	int err = 0;
	CALLBACK_MAYBE (s, on_reset);
	return err;

}

#pragma endregion
