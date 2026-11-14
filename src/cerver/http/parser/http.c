/*
 * llhttp 9.4.3 (https://github.com/nodejs/llhttp), split and restyled for cerver
 * the decisions of the parser after the headers and after a message,
 * the method CONNECT is the one of cerver/http/method.h
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

#include <stdbool.h>

#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/parser/llhttp.h"
#include "cerver/http/requests/method.h"

// the statuses of the responses that change how their body is read
#define LLHTTP_STATUS_CONTINUE					100
#define LLHTTP_STATUS_SWITCHING_PROTOCOLS		101
#define LLHTTP_STATUS_PROCESSING				102
#define LLHTTP_STATUS_EARLY_HINTS				103
#define LLHTTP_STATUS_NO_CONTENT				204
#define LLHTTP_STATUS_NOT_MODIFIED				304
#define LLHTTP_STATUS_CLASS						100
#define LLHTTP_STATUS_INFORMATIONAL_CLASS		1

// the values that llhttp__after_headers_complete () returns to the generated parser
#define LLHTTP_AFTER_HEADERS_NO_BODY					0
#define LLHTTP_AFTER_HEADERS_UPGRADE					1
#define LLHTTP_AFTER_HEADERS_CHUNKED					2
#define LLHTTP_AFTER_HEADERS_IDENTITY					3
#define LLHTTP_AFTER_HEADERS_IDENTITY_EOF				4
#define LLHTTP_AFTER_HEADERS_INVALID_TRANSFER_ENCODING	5

typedef const unsigned char *llhttp_span;

// the prototypes are declared by the generated parser
int llhttp__before_headers_complete (llhttp_t *parser, llhttp_span p, llhttp_span endp);
int llhttp__after_headers_complete (llhttp_t *parser, llhttp_span p, llhttp_span endp);
int llhttp__after_message_complete (llhttp_t *parser, llhttp_span p, llhttp_span endp);

int llhttp__before_headers_complete (
	llhttp_t *parser, llhttp_span p, llhttp_span endp
) {

	(void) p;
	(void) endp;

	// set here, so the on_headers_complete () callbacks can see it
	if ((parser->flags & F_UPGRADE) && (parser->flags & F_CONNECTION_UPGRADE)) {
		// for the responses, "Upgrade: foo" and "Connection: upgrade" are mandatory
		// only in a 101 Switching Protocols response, otherwise they only announce the support
		parser->upgrade = (uint8_t) (
			(parser->type == HTTP_REQUEST) || (parser->status_code == LLHTTP_STATUS_SWITCHING_PROTOCOLS)
		);
	}

	else {
		parser->upgrade = (uint8_t) (parser->method == REQUEST_METHOD_CONNECT);
	}

	return 0;

}

static bool llhttp_response_without_body (const llhttp_t *parser) {

	return (parser->type == HTTP_RESPONSE) && (
		(parser->status_code == LLHTTP_STATUS_PROCESSING)
		|| (parser->status_code == LLHTTP_STATUS_EARLY_HINTS)
		|| (parser->status_code == LLHTTP_STATUS_NO_CONTENT)
		|| (parser->status_code == LLHTTP_STATUS_NOT_MODIFIED)
	);

}

// RFC 7230 3.3.3: a request whose Transfer-Encoding does not end with chunked
// can not tell the length of its body, it is answered with 400 and closed,
// a response like that is read until the connection is closed
static int llhttp_after_headers_transfer_encoding (const llhttp_t *parser) {

	return (
		(parser->type == HTTP_REQUEST)
		&& !(parser->lenient_flags & LENIENT_CHUNKED_LENGTH)
		&& !(parser->lenient_flags & LENIENT_TRANSFER_ENCODING)
	) ? LLHTTP_AFTER_HEADERS_INVALID_TRANSFER_ENCODING : LLHTTP_AFTER_HEADERS_IDENTITY_EOF;

}

static int llhttp_after_headers_identity (const llhttp_t *parser) {

	int retval = LLHTTP_AFTER_HEADERS_IDENTITY;

	// without a Content-Length, a body is read until the connection is closed if the message needs it
	if (!(parser->flags & F_CONTENT_LENGTH)) {
		retval = llhttp_message_needs_eof (parser) ?
			LLHTTP_AFTER_HEADERS_IDENTITY_EOF : LLHTTP_AFTER_HEADERS_NO_BODY;
	}

	// "Content-Length: 0"
	else if (parser->content_length == 0) {
		retval = LLHTTP_AFTER_HEADERS_NO_BODY;
	}

	return retval;

}

// returns how the body is read:
// 0 - no body, restart, message complete
// 1 - CONNECT request, restart, message complete and pause
// 2 - chunk size start
// 3 - body identity
// 4 - body identity until EOF
// 5 - invalid transfer encoding for a request
int llhttp__after_headers_complete (
	llhttp_t *parser, llhttp_span p, llhttp_span endp
) {

	(void) p;
	(void) endp;

	const bool has_body = (parser->flags & F_CHUNKED) || (parser->content_length > 0);

	int retval = LLHTTP_AFTER_HEADERS_NO_BODY;

	// the rest of the message is in a different protocol
	if (
		(parser->upgrade && ((parser->method == REQUEST_METHOD_CONNECT) || (parser->flags & F_SKIPBODY) || !has_body))
		|| ((parser->type == HTTP_RESPONSE) && (parser->status_code == LLHTTP_STATUS_SWITCHING_PROTOCOLS))
	) {
		retval = LLHTTP_AFTER_HEADERS_UPGRADE;
	}

	// no body, the message is complete
	else if ((parser->type == HTTP_RESPONSE) && (parser->status_code == LLHTTP_STATUS_CONTINUE)) {
		retval = LLHTTP_AFTER_HEADERS_NO_BODY;
	}

	// a response to a HEAD request or a response without body (RFC 2616 section 4.4)
	else if ((parser->flags & F_SKIPBODY) || llhttp_response_without_body (parser)) {
		retval = LLHTTP_AFTER_HEADERS_NO_BODY;
	}

	// the Content-Length is ignored, the chunks are read
	else if (parser->flags & F_CHUNKED) {
		retval = LLHTTP_AFTER_HEADERS_CHUNKED;
	}

	else if (parser->flags & F_TRANSFER_ENCODING) {
		retval = llhttp_after_headers_transfer_encoding (parser);
	}

	else {
		retval = llhttp_after_headers_identity (parser);
	}

	return retval;

}

int llhttp__after_message_complete (
	llhttp_t *parser, llhttp_span p, llhttp_span endp
) {

	(void) p;
	(void) endp;

	const int should_keep_alive = llhttp_should_keep_alive (parser);

	parser->finish = HTTP_FINISH_SAFE;
	parser->flags = 0;

	// ignored in loose parsing mode
	return should_keep_alive;

}

int llhttp_message_needs_eof (const llhttp_t *parser) {

	int retval = 1;

	if (parser->type == HTTP_REQUEST) {
		retval = 0;
	}

	// 1xx, 204, 304 and the responses to a HEAD request (RFC 2616 section 4.4)
	else if (
		((parser->status_code / LLHTTP_STATUS_CLASS) == LLHTTP_STATUS_INFORMATIONAL_CLASS)
		|| (parser->status_code == LLHTTP_STATUS_NO_CONTENT)
		|| (parser->status_code == LLHTTP_STATUS_NOT_MODIFIED)
		|| (parser->flags & F_SKIPBODY)
	) {
		retval = 0;
	}

	// RFC 7230 3.3.3, see llhttp__after_headers_complete ()
	else if ((parser->flags & F_TRANSFER_ENCODING) && !(parser->flags & F_CHUNKED)) {
		retval = 1;
	}

	else if (parser->flags & (F_CHUNKED | F_CONTENT_LENGTH)) {
		retval = 0;
	}

	return retval;

}

int llhttp_should_keep_alive (const llhttp_t *parser) {

	int retval = !llhttp_message_needs_eof (parser);

	// HTTP/1.1
	if ((parser->http_major > 0) && (parser->http_minor > 0)) {
		if (parser->flags & F_CONNECTION_CLOSE) retval = 0;
	}

	// HTTP/1.0 or earlier
	else if (!(parser->flags & F_CONNECTION_KEEP_ALIVE)) {
		retval = 0;
	}

	return retval;

}
