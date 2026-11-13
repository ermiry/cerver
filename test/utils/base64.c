#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <cerver/utils/base64.h>
#include <cerver/utils/utils.h>

#include "../test.h"

static void utils_tests_base64_encode (void) {

	char string[8192] = { 0 };
	char encoded[16384] = { 0 };

	c_string_copy (string, "");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "", NULL);

	c_string_copy (string, "f");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "Zg==", NULL);

	c_string_copy (string, "fo");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "Zm8=", NULL);

	c_string_copy (string, "foo");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "Zm9v", NULL);

	c_string_copy (string, "foob");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "Zm9vYg==", NULL);

	c_string_copy (string, "fooba");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "Zm9vYmE=", NULL);

	c_string_copy (string, "foobar");
	base64_encode (encoded, string, strlen (string));
	test_check_str_eq (encoded, "Zm9vYmFy", NULL);

}

static void utils_tests_base64_decode (void) {

	char string[8192] = { 0 };
	char decoded[8192] = { 0 };

	c_string_copy (string, "");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "", NULL);

	c_string_copy (string, "Zg==");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "f", NULL);

	c_string_copy (string, "Zm8=");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "fo", NULL);

	c_string_copy (string, "Zm9v");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "foo", NULL);

	c_string_copy (string, "Zm9vYg==");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "foob", NULL);

	c_string_copy (string, "Zm9vYmE=");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "fooba", NULL);

	c_string_copy (string, "Zm9vYmFy");
	base64_decode (decoded, string, strlen (string));
	test_check_str_eq (decoded, "foobar", NULL);

}

// the inputs of every length up to this size are encoded and decoded back
#define BASE64URL_TEST_MAX_LENGTH		(1 << 7)
#define BASE64URL_TEST_BUFFER_SIZE		(1 << 9)

// the vectors of RFC 4648 §10 without padding, and the two characters of the url alphabet
static void utils_tests_base64url_vectors (void) {

	static const char *inputs[] = { "", "f", "fo", "foo", "foob", "fooba", "foobar" };
	static const char *outputs[] = { "", "Zg", "Zm8", "Zm9v", "Zm9vYg", "Zm9vYmE", "Zm9vYmFy" };

	char encoded[BASE64URL_TEST_BUFFER_SIZE] = { 0 };
	for (size_t i = 0; i < (sizeof (inputs) / sizeof (inputs[0])); i++) {
		const size_t length = strlen (inputs[i]);
		test_check_unsigned_eq (
			base64url_encode (encoded, (const unsigned char *) inputs[i], length),
			strlen (outputs[i]), NULL
		);

		test_check_str_eq (encoded, outputs[i], NULL);
		test_check_unsigned_eq (base64url_encode_length (length), strlen (outputs[i]), NULL);
	}

	// 0xfb 0xff are "+/8" in base64 and "-_8" in base64url
	const unsigned char url_bytes[] = { 0xfb, 0xff };
	(void) base64url_encode (encoded, url_bytes, sizeof (url_bytes));
	test_check_str_eq (encoded, "-_8", NULL);

}

static void utils_tests_base64url_round_trip (void) {

	unsigned char input[BASE64URL_TEST_MAX_LENGTH] = { 0 };
	char encoded[BASE64URL_TEST_BUFFER_SIZE] = { 0 };
	unsigned char decoded[BASE64URL_TEST_BUFFER_SIZE] = { 0 };

	for (size_t length = 0; length < BASE64URL_TEST_MAX_LENGTH; length++) {
		for (size_t i = 0; i < length; i++) input[i] = (unsigned char) ((i * 37 + length) & 0xFF);

		const size_t encoded_length = base64url_encode (encoded, input, length);

		size_t decoded_length = 0;
		test_check_unsigned_eq (base64url_decode (decoded, &decoded_length, encoded, encoded_length), 0, NULL);
		test_check_unsigned_eq (decoded_length, length, NULL);
		test_check (!memcmp (decoded, input, length), "base64url did not decode its own encoding");
		test_check (decoded_length <= base64url_decode_length (encoded_length), NULL);
	}

}

// every token has a single valid form
static void utils_tests_base64url_canonical (void) {

	static const char *invalid[] = {
		"Zm9v+", "Zm9v/", "Zg==", "Zg=", "Zm9", "Z", "Zm9vY", "Zh", "Zm9vYh", "Zm 9v", "Zm9v\n"
	};

	unsigned char decoded[BASE64URL_TEST_BUFFER_SIZE] = { 0 };
	size_t decoded_length = 0;

	// "Zm9", "Zh" and "Zm9vYh" have the unused bits of their last character set:
	// "Zm8", "Zg" and "Zm9vYg" are the valid forms of the same bytes
	for (size_t i = 0; i < (sizeof (invalid) / sizeof (invalid[0])); i++) {
		const char *encoded = invalid[i];
		test_check_unsigned_eq (base64url_decode (decoded, &decoded_length, encoded, strlen (encoded)), 1, encoded);
		test_check_unsigned_eq (decoded_length, 0, NULL);
	}

	test_check_unsigned_eq (base64url_decode (decoded, &decoded_length, "Zm8", 3), 0, NULL);
	test_check_unsigned_eq (decoded_length, 2, NULL);

	test_check_unsigned_eq (base64url_decode (NULL, &decoded_length, "Zg", 2), 1, NULL);
	test_check_unsigned_eq (base64url_decode (decoded, NULL, "Zg", 2), 1, NULL);

}

void utils_tests_base64 (void) {

	(void) printf ("Testing UTILS base64...\n");

	utils_tests_base64_encode ();

	utils_tests_base64_decode ();

	utils_tests_base64url_vectors ();

	utils_tests_base64url_round_trip ();

	utils_tests_base64url_canonical ();

	(void) printf ("Done!\n");

}
