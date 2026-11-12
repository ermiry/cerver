#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/http/websockets/text.h"

// the bytes that are checked at once while the text is ASCII
#define HTTP_WEB_SOCKET_TEXT_WORD_SIZE				sizeof (u64)

// the high bit of every byte of a word, set in any byte that is not ASCII
#define HTTP_WEB_SOCKET_TEXT_ASCII_MASK				0x8080808080808080ULL

#define HTTP_WEB_SOCKET_TEXT_ASCII_LIMIT			0x80

// the range of a continuation byte, 10xxxxxx
#define HTTP_WEB_SOCKET_TEXT_CONTINUATION_LOWER		0x80
#define HTTP_WEB_SOCKET_TEXT_CONTINUATION_UPPER		0xBF

// the lead bytes of two bytes, C0 and C1 would be overlong forms of ASCII
#define HTTP_WEB_SOCKET_TEXT_TWO_LOWER				0xC2
#define HTTP_WEB_SOCKET_TEXT_TWO_UPPER				0xDF

// the lead bytes of three bytes
#define HTTP_WEB_SOCKET_TEXT_THREE_LOWER			0xE0
#define HTTP_WEB_SOCKET_TEXT_THREE_UPPER			0xEF

// E0 needs A0 or more next (smaller would be overlong)
#define HTTP_WEB_SOCKET_TEXT_THREE_OVERLONG			0xE0
#define HTTP_WEB_SOCKET_TEXT_THREE_OVERLONG_LOWER	0xA0

// ED needs 9F or less next (larger are the surrogates, U+D800 to U+DFFF)
#define HTTP_WEB_SOCKET_TEXT_SURROGATES				0xED
#define HTTP_WEB_SOCKET_TEXT_SURROGATES_UPPER		0x9F

// the lead bytes of four bytes, F5 and above would be above U+10FFFF
#define HTTP_WEB_SOCKET_TEXT_FOUR_LOWER				0xF0
#define HTTP_WEB_SOCKET_TEXT_FOUR_UPPER				0xF4

// F0 needs 90 or more next (smaller would be overlong)
#define HTTP_WEB_SOCKET_TEXT_FOUR_OVERLONG_LOWER	0x90

// F4 needs 8F or less next (larger would be above U+10FFFF)
#define HTTP_WEB_SOCKET_TEXT_FOUR_LIMIT_UPPER		0x8F

#define HTTP_WEB_SOCKET_TEXT_TWO_CONTINUATIONS		1
#define HTTP_WEB_SOCKET_TEXT_THREE_CONTINUATIONS	2
#define HTTP_WEB_SOCKET_TEXT_FOUR_CONTINUATIONS		3

void http_web_socket_text_init (HttpWebSocketText *text) {

	if (text) {
		text->remaining = 0;
		text->lower = HTTP_WEB_SOCKET_TEXT_CONTINUATION_LOWER;
		text->upper = HTTP_WEB_SOCKET_TEXT_CONTINUATION_UPPER;
	}

}

static void http_web_socket_text_expect (
	HttpWebSocketText *text,
	const u8 remaining, const u8 lower, const u8 upper
) {

	text->remaining = remaining;
	text->lower = lower;
	text->upper = upper;

}

// a byte of three bytes, with the ranges of E0 and ED
static void http_web_socket_text_lead_three (
	HttpWebSocketText *text, const u8 byte
) {

	u8 lower = HTTP_WEB_SOCKET_TEXT_CONTINUATION_LOWER;
	u8 upper = HTTP_WEB_SOCKET_TEXT_CONTINUATION_UPPER;

	if (byte == HTTP_WEB_SOCKET_TEXT_THREE_OVERLONG) lower = HTTP_WEB_SOCKET_TEXT_THREE_OVERLONG_LOWER;
	else if (byte == HTTP_WEB_SOCKET_TEXT_SURROGATES) upper = HTTP_WEB_SOCKET_TEXT_SURROGATES_UPPER;

	http_web_socket_text_expect (text, HTTP_WEB_SOCKET_TEXT_THREE_CONTINUATIONS, lower, upper);

}

// a byte of four bytes, with the ranges of F0 and F4
static void http_web_socket_text_lead_four (
	HttpWebSocketText *text, const u8 byte
) {

	u8 lower = HTTP_WEB_SOCKET_TEXT_CONTINUATION_LOWER;
	u8 upper = HTTP_WEB_SOCKET_TEXT_CONTINUATION_UPPER;

	if (byte == HTTP_WEB_SOCKET_TEXT_FOUR_LOWER) lower = HTTP_WEB_SOCKET_TEXT_FOUR_OVERLONG_LOWER;
	else if (byte == HTTP_WEB_SOCKET_TEXT_FOUR_UPPER) upper = HTTP_WEB_SOCKET_TEXT_FOUR_LIMIT_UPPER;

	http_web_socket_text_expect (text, HTTP_WEB_SOCKET_TEXT_FOUR_CONTINUATIONS, lower, upper);

}

// the first byte of a code point that is not ASCII
// returns false when it can not start one
static bool http_web_socket_text_lead (
	HttpWebSocketText *text, const u8 byte
) {

	bool valid = true;

	if ((byte >= HTTP_WEB_SOCKET_TEXT_TWO_LOWER) && (byte <= HTTP_WEB_SOCKET_TEXT_TWO_UPPER)) {
		http_web_socket_text_expect (
			text, HTTP_WEB_SOCKET_TEXT_TWO_CONTINUATIONS,
			HTTP_WEB_SOCKET_TEXT_CONTINUATION_LOWER, HTTP_WEB_SOCKET_TEXT_CONTINUATION_UPPER
		);
	}

	else if ((byte >= HTTP_WEB_SOCKET_TEXT_THREE_LOWER) && (byte <= HTTP_WEB_SOCKET_TEXT_THREE_UPPER)) {
		http_web_socket_text_lead_three (text, byte);
	}

	else if ((byte >= HTTP_WEB_SOCKET_TEXT_FOUR_LOWER) && (byte <= HTTP_WEB_SOCKET_TEXT_FOUR_UPPER)) {
		http_web_socket_text_lead_four (text, byte);
	}

	else {
		valid = false;
	}

	return valid;

}

// a byte after the lead one, in the range that the code point allows
static bool http_web_socket_text_continuation (
	HttpWebSocketText *text, const u8 byte
) {

	bool valid = false;

	if ((byte >= text->lower) && (byte <= text->upper)) {
		text->remaining -= 1;
		text->lower = HTTP_WEB_SOCKET_TEXT_CONTINUATION_LOWER;
		text->upper = HTTP_WEB_SOCKET_TEXT_CONTINUATION_UPPER;
		valid = true;
	}

	return valid;

}

// the bytes from the position that are ASCII, checked a word at a time
static size_t http_web_socket_text_ascii (
	const u8 *bytes, const size_t position, const size_t size
) {

	size_t end = position;

	u64 word = 0;
	while ((size - end) >= HTTP_WEB_SOCKET_TEXT_WORD_SIZE) {
		(void) memcpy (&word, bytes + end, HTTP_WEB_SOCKET_TEXT_WORD_SIZE);
		if (word & HTTP_WEB_SOCKET_TEXT_ASCII_MASK) break;

		end += HTTP_WEB_SOCKET_TEXT_WORD_SIZE;
	}

	return end - position;

}

bool http_web_socket_text_validate (
	HttpWebSocketText *text, const void *data, const size_t size
) {

	bool valid = (text != NULL) && ((data != NULL) || !size);

	const u8 *bytes = (const u8 *) data;
	size_t position = 0;
	while (valid && (position < size)) {
		if (!text->remaining) {
			position += http_web_socket_text_ascii (bytes, position, size);
			if (position >= size) break;
		}

		const u8 byte = bytes[position];
		if (text->remaining) valid = http_web_socket_text_continuation (text, byte);
		else if (byte >= HTTP_WEB_SOCKET_TEXT_ASCII_LIMIT) valid = http_web_socket_text_lead (text, byte);

		position += 1;
	}

	return valid;

}

bool http_web_socket_text_complete (const HttpWebSocketText *text) {

	return text && !text->remaining;

}

bool http_web_socket_text_check (const void *data, const size_t size) {

	HttpWebSocketText text;
	http_web_socket_text_init (&text);

	return http_web_socket_text_validate (&text, data, size)
		&& http_web_socket_text_complete (&text);

}
