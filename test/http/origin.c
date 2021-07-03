#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/origin.h>

#include "../test.h"

static const char *domain_1 = { "https://ermiry.com" };
static const char *domain_2 = { "https://cerver.ermiry.com" };

static HttpOrigin *test_http_origin_new (void) {

	HttpOrigin *origin = http_origin_new ();

	test_check_ptr (origin);

	test_check_int_eq (origin->len, 0, NULL);
	test_check_str_len (origin->value, 0, NULL);

	return origin;

}

static void test_http_origin_init (void) {

	HttpOrigin *origin_1 = test_http_origin_new ();

	http_origin_init (origin_1, domain_1);

	test_check_ptr (origin_1);
	test_check_str_eq (http_origin_get_value (origin_1), domain_1, NULL);
	test_check_str_len (http_origin_get_value (origin_1), strlen (domain_1), NULL);
	test_check_int_eq (http_origin_get_len (origin_1), (int) strlen (domain_1), NULL);

	http_origin_delete (origin_1);

	HttpOrigin *origin_2 = test_http_origin_new ();

	http_origin_init (origin_2, domain_2);

	test_check_ptr (origin_2);
	test_check_str_eq (http_origin_get_value (origin_2), domain_2, NULL);
	test_check_str_len (http_origin_get_value (origin_2), strlen (domain_2), NULL);
	test_check_int_eq (http_origin_get_len (origin_2), (int) strlen (domain_2), NULL);

	http_origin_delete (origin_2);

}

static void test_http_origin_reset (void) {

	HttpOrigin *origin_1 = test_http_origin_new ();

	http_origin_init (origin_1, domain_1);

	test_check_ptr (origin_1);
	test_check_str_eq (http_origin_get_value (origin_1), domain_1, NULL);
	test_check_str_len (http_origin_get_value (origin_1), strlen (domain_1), NULL);
	test_check_int_eq (http_origin_get_len (origin_1), (int) strlen (domain_1), NULL);

	http_origin_reset (origin_1);

	test_check_ptr (origin_1);

	test_check_int_eq (origin_1->len, 0, NULL);
	test_check_str_len (origin_1->value, 0, NULL);

	http_origin_delete (origin_1);

}

void http_tests_origins (void) {

	(void) printf ("Testing HTTP origins...\n");

	test_http_origin_init ();
	test_http_origin_reset ();

	(void) printf ("Done!\n");

}
