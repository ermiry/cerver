#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/url.h>

#include "../test.h"

// bugs these tests fixed:
// - a NULL string was passed to strlen () by http_url_encode () and http_url_decode ()
// - http_url_decode () dropped the '%' of an incomplete escape and decoded an invalid one
// - http_url_encode () passed a negative char, a byte over 0x7f, to isalnum ()

#define TEST_URL_BYTES			256

static const char *to_encode = "http://www.mysite.com/a file with spaces.html";
// static const char *encoded = "http://www.mysite.com/a%20file%20with%20spaces.html";
static const char *encoded = "http%3a%2f%2fwww.mysite.com%2fa+file+with+spaces.html";

static void test_http_url_encode (void) {

	char *result = http_url_encode (to_encode);

	test_check_ptr (result);

	test_check_str_eq (result, encoded, NULL);

	free (result);

}

static void test_http_url_decode (void) {

	char *result = http_url_decode (encoded);

	test_check_ptr (result);

	test_check_str_eq (result, to_encode, NULL);

	free (result);

}

static void test_http_url_null (void) {

	test_check_null_ptr (http_url_encode (NULL));
	test_check_null_ptr (http_url_decode (NULL));

}

// an escape that is incomplete or that is not hex is copied as it is
static void test_http_url_decode_invalid_escapes (void) {

	static const char *values[] = { "%", "abc%", "%a", "%zz", "%g1", "100%25%", "a%2" };
	static const char *expected[] = { "%", "abc%", "%a", "%zz", "%g1", "100%%", "a%2" };

	for (size_t i = 0; i < (sizeof (values) / sizeof (values[0])); i++) {
		char *result = http_url_decode (values[i]);
		test_check_ptr (result);
		test_check_str_eq (result, expected[i], values[i]);
		free (result);
	}

}

// every byte is encoded and decoded back as it was
static void test_http_url_every_byte (void) {

	char value[TEST_URL_BYTES];
	for (size_t i = 1; i < TEST_URL_BYTES; i++) value[i - 1] = (char) i;
	value[TEST_URL_BYTES - 1] = '\0';

	char *result = http_url_encode (value);
	test_check_ptr (result);

	// a byte over 0x7f is escaped, never copied as if it were alphanumeric
	test_check_ptr (strstr (result, "%ff"));

	char *decoded = http_url_decode (result);
	test_check_ptr (decoded);
	test_check_str_eq (decoded, value, NULL);

	free (decoded);
	free (result);

}

void http_tests_urls (void) {

	(void) printf ("Testing HTTP URLs...\n");

	test_http_url_encode ();
	test_http_url_decode ();
	test_http_url_null ();
	test_http_url_decode_invalid_escapes ();
	test_http_url_every_byte ();

	(void) printf ("Done!\n");

}
