#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/url.h>

#include "../test.h"

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

void http_tests_urls (void) {

	(void) printf ("Testing HTTP URLs...\n");

	test_http_url_encode ();
	test_http_url_decode ();

	(void) printf ("Done!\n");

}
