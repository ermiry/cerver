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

void utils_tests_base64 (void) {

	(void) printf ("Testing UTILS base64...\n");

	utils_tests_base64_encode ();

	utils_tests_base64_decode ();

	(void) printf ("Done!\n");

}