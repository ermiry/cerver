#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/http/websockets/text.h>

#include "../test.h"
#include "websocket.h"

// the last code point and the range of the surrogates
#define TEXT_TEST_LAST_CODE_POINT		0x10FFFF
#define TEXT_TEST_SURROGATES_FIRST		0xD800
#define TEXT_TEST_SURROGATES_LAST		0xDFFF

#define TEXT_TEST_MAX_BYTES				4

// the lead bytes of each length and the continuation bytes (RFC 3629)
#define TEXT_TEST_ASCII_LAST			0x7F
#define TEXT_TEST_LEAD_TWO_MASK			0xE0
#define TEXT_TEST_LEAD_TWO				0xC0
#define TEXT_TEST_LEAD_THREE_MASK		0xF0
#define TEXT_TEST_LEAD_THREE			0xE0
#define TEXT_TEST_LEAD_FOUR_MASK		0xF8
#define TEXT_TEST_LEAD_FOUR				0xF0
#define TEXT_TEST_CONTINUATION_MASK		0xC0
#define TEXT_TEST_CONTINUATION			0x80
#define TEXT_TEST_CONTINUATION_BITS		6

// the random texts that are compared with the reference validator
#define TEXT_TEST_RANDOM_TEXTS			(1 << 16)
#define TEXT_TEST_RANDOM_MAX_LENGTH		(1 << 5)
#define TEXT_TEST_RANDOM_SEED			7

// an ASCII text that crosses several words of the fast path
#define TEXT_TEST_ASCII_LENGTH			(1 << 7)

// encodes a code point without checking it, so the surrogates can be encoded too
static size_t text_test_encode (const u32 code_point, u8 *bytes) {

	size_t length = 0;

	if (code_point < 0x80) {
		bytes[0] = (u8) code_point;
		length = 1;
	}

	else if (code_point < 0x800) {
		bytes[0] = (u8) (0xC0 | (code_point >> 6));
		bytes[1] = (u8) (0x80 | (code_point & 0x3F));
		length = 2;
	}

	else if (code_point < 0x10000) {
		bytes[0] = (u8) (0xE0 | (code_point >> 12));
		bytes[1] = (u8) (0x80 | ((code_point >> 6) & 0x3F));
		bytes[2] = (u8) (0x80 | (code_point & 0x3F));
		length = 3;
	}

	else {
		bytes[0] = (u8) (0xF0 | (code_point >> 18));
		bytes[1] = (u8) (0x80 | ((code_point >> 12) & 0x3F));
		bytes[2] = (u8) (0x80 | ((code_point >> 6) & 0x3F));
		bytes[3] = (u8) (0x80 | (code_point & 0x3F));
		length = 4;
	}

	return length;

}

// the smallest code point of each length, a smaller one is an overlong form
static const u32 text_test_minimum_code_point[TEXT_TEST_MAX_BYTES + 1] = { 0, 0, 0x80, 0x800, 0x10000 };

// the length of the sequence that a byte starts and the bits of the code point that it has,
// 0 for a byte that can not start a sequence
static size_t text_test_reference_lead (const u8 lead, u32 *code_point) {

	size_t length = 0;

	if (lead <= TEXT_TEST_ASCII_LAST) {
		*code_point = lead;
		length = 1;
	}

	else if ((lead & TEXT_TEST_LEAD_TWO_MASK) == TEXT_TEST_LEAD_TWO) {
		*code_point = lead & (u8) ~TEXT_TEST_LEAD_TWO_MASK;
		length = 2;
	}

	else if ((lead & TEXT_TEST_LEAD_THREE_MASK) == TEXT_TEST_LEAD_THREE) {
		*code_point = lead & (u8) ~TEXT_TEST_LEAD_THREE_MASK;
		length = 3;
	}

	else if ((lead & TEXT_TEST_LEAD_FOUR_MASK) == TEXT_TEST_LEAD_FOUR) {
		*code_point = lead & (u8) ~TEXT_TEST_LEAD_FOUR_MASK;
		length = 4;
	}

	return length;

}

// a code point in its shortest form, that is not a surrogate and that is not above the last one
static bool text_test_reference_code_point (const u32 code_point, const size_t length) {

	return (code_point >= text_test_minimum_code_point[length])
		&& (code_point <= TEXT_TEST_LAST_CODE_POINT)
		&& ((code_point < TEXT_TEST_SURROGATES_FIRST) || (code_point > TEXT_TEST_SURROGATES_LAST));

}

// the reference validator, a decoder of RFC 3629 that takes one code point at a time,
// written apart from the one of cerver so that the two can be compared
static bool text_test_reference (const u8 *bytes, const size_t length) {

	bool valid = true;

	size_t i = 0;
	while (valid && (i < length)) {
		u32 code_point = 0;
		const size_t sequence = text_test_reference_lead (bytes[i], &code_point);

		valid = sequence && ((i + sequence) <= length);
		for (size_t j = 1; valid && (j < sequence); j++) {
			valid = (bytes[i + j] & TEXT_TEST_CONTINUATION_MASK) == TEXT_TEST_CONTINUATION;
			code_point = (code_point << TEXT_TEST_CONTINUATION_BITS) | (bytes[i + j] & (u8) ~TEXT_TEST_CONTINUATION_MASK);
		}

		valid = valid && text_test_reference_code_point (code_point, sequence);
		i += sequence;
	}

	return valid;

}

// validates the bytes split at the offset, the state continues between the two calls
static bool text_test_split (const u8 *bytes, const size_t length, const size_t offset) {

	HttpWebSocketText text;
	http_web_socket_text_init (&text);

	return http_web_socket_text_validate (&text, bytes, offset)
		&& http_web_socket_text_validate (&text, bytes + offset, length - offset)
		&& http_web_socket_text_complete (&text);

}

// every code point is valid, but the surrogates, whole and split at each byte
static void test_text_code_points (void) {

	u8 bytes[TEXT_TEST_MAX_BYTES];
	for (u32 code_point = 0; code_point <= TEXT_TEST_LAST_CODE_POINT; code_point++) {
		const size_t length = text_test_encode (code_point, bytes);
		const bool surrogate = (code_point >= TEXT_TEST_SURROGATES_FIRST) && (code_point <= TEXT_TEST_SURROGATES_LAST);

		if (http_web_socket_text_check (bytes, length) == surrogate) {
			(void) fprintf (stderr, "code point U+%04X\n", code_point);
			test_check (false, "a code point was not validated as expected");
		}

		for (size_t offset = 1; !surrogate && (offset < length); offset++) {
			test_check (text_test_split (bytes, length, offset), "a code point split between two pieces failed");
		}
	}

}

static void text_test_invalid (const char *bytes, const char *description) {

	if (http_web_socket_text_check (bytes, strlen (bytes))) {
		(void) fprintf (stderr, "%s\n", description);
		test_check (false, "an invalid text was accepted");
	}

}

// the overlong forms, the code points above U+10FFFF and the bytes that never appear
static void test_text_invalid (void) {

	text_test_invalid ("\xC0\x80", "overlong NUL");
	text_test_invalid ("\xC1\xBF", "overlong of two bytes");
	text_test_invalid ("\xE0\x80\x80", "overlong of three bytes");
	text_test_invalid ("\xE0\x9F\xBF", "overlong U+07FF");
	text_test_invalid ("\xF0\x80\x80\x80", "overlong of four bytes");
	text_test_invalid ("\xF0\x8F\xBF\xBF", "overlong U+FFFF");
	text_test_invalid ("\xF4\x90\x80\x80", "U+110000");
	text_test_invalid ("\xF5\x80\x80\x80", "lead F5");
	text_test_invalid ("\xFE", "byte FE");
	text_test_invalid ("\xFF", "byte FF");
	text_test_invalid ("\x80", "a lone continuation");
	text_test_invalid ("a\xBF" "b", "a continuation after ASCII");
	text_test_invalid ("\xC2" "a", "a lead without its continuation");
	text_test_invalid ("\xED\xA0\x80", "the first surrogate");
	text_test_invalid ("\xED\xBF\xBF", "the last surrogate");

}

// an incomplete code point is valid until the text ends
static void test_text_incomplete (void) {

	HttpWebSocketText text;
	http_web_socket_text_init (&text);

	test_check_true (http_web_socket_text_validate (&text, "\xE2\x82", 2));
	test_check_false (http_web_socket_text_complete (&text));
	test_check_true (http_web_socket_text_validate (&text, "\xAC", 1));
	test_check_true (http_web_socket_text_complete (&text));

	test_check_false (http_web_socket_text_check ("\xF0\x9F\x98", 3));
	test_check_true (http_web_socket_text_check ("", 0));
	test_check_true (http_web_socket_text_check (NULL, 0));
	test_check_false (http_web_socket_text_validate (NULL, "a", 1));

}

// the text of autobahn 6.4.1 fails at the byte 0x90 of its fourth code point,
// fed one byte at a time, before the rest of the text arrives
static void test_text_fail_fast (void) {

	const char *bytes = "\xce\xba\xe1\xbd\xb9\xcf\x83\xce\xbc\xce\xb5\xf4\x90\x80\x80\x65\x64\x69\x74\x65\x64";
	const size_t failing = 12;

	HttpWebSocketText text;
	http_web_socket_text_init (&text);

	for (size_t i = 0; i < failing; i++) {
		test_check_true (http_web_socket_text_validate (&text, bytes + i, 1));
	}

	test_check_false (http_web_socket_text_validate (&text, bytes + failing, 1));

}

// a code point or an invalid byte at each position of a long ASCII text,
// so every alignment of the words of the fast path is checked
static void test_text_ascii (void) {

	u8 bytes[TEXT_TEST_ASCII_LENGTH + TEXT_TEST_MAX_BYTES];

	for (size_t position = 0; position < TEXT_TEST_ASCII_LENGTH; position++) {
		(void) memset (bytes, 'a', sizeof (bytes));

		bytes[position] = 0x80;
		test_check_false (http_web_socket_text_check (bytes, TEXT_TEST_ASCII_LENGTH));

		(void) memset (bytes, 'a', sizeof (bytes));
		const size_t length = text_test_encode (0x1F600, bytes + position);
		test_check_true (http_web_socket_text_check (bytes, TEXT_TEST_ASCII_LENGTH + length));
	}

}

// random bytes biased to the lead and the continuation bytes,
// compared with the reference validator
static void test_text_random (void) {

	static const u8 alphabet[] = {
		'a', 0x00, 0x7F, 0x80, 0x8F, 0x90, 0x9F, 0xA0, 0xBF, 0xC0, 0xC1, 0xC2, 0xDF,
		0xE0, 0xE1, 0xEC, 0xED, 0xEE, 0xEF, 0xF0, 0xF1, 0xF3, 0xF4, 0xF5, 0xFF
	};

	unsigned int seed = TEXT_TEST_RANDOM_SEED;

	u8 bytes[TEXT_TEST_RANDOM_MAX_LENGTH];
	for (unsigned int n = 0; n < TEXT_TEST_RANDOM_TEXTS; n++) {
		const size_t length = (size_t) (rand_r (&seed) % TEXT_TEST_RANDOM_MAX_LENGTH);
		for (size_t i = 0; i < length; i++) {
			bytes[i] = alphabet[(size_t) rand_r (&seed) % sizeof (alphabet)];
		}

		const bool expected = text_test_reference (bytes, length);
		if (http_web_socket_text_check (bytes, length) != expected) {
			(void) fprintf (stderr, "random text %u of %zu bytes\n", n, length);
			test_check (false, "the validator does not agree with the reference");
		}

		const size_t offset = length ? (size_t) (rand_r (&seed) % length) : 0;
		test_check_bool_eq (text_test_split (bytes, length, offset), expected, NULL);
	}

}

void web_socket_tests_text (void) {

	(void) printf ("Testing WebSocket text...\n");

	test_run (test_text_code_points);
	test_run (test_text_invalid);
	test_run (test_text_incomplete);
	test_run (test_text_fail_fast);
	test_run (test_text_ascii);
	test_run (test_text_random);

	(void) printf ("Done!\n");

}
