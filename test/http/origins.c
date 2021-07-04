#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/cerver.h>

#include <cerver/http/http.h>
#include <cerver/http/response.h>

#include "http.h"

#include "../test.h"

static const char *domain_1 = { "https://ermiry.com" };
static const char *domain_2 = { "https://cerver.ermiry.com" };
static const char *domain_3 = { "https://pocket.ermiry.com" };

static const char *oki_doki_json = { "{\"oki\": \"doki\"}" };

static u8 test_create_response (
	HttpCerver *http_cerver, const char *domain
) {

	u8 retval = 1;

	HttpResponse *response = http_response_create (
		HTTP_STATUS_OK, oki_doki_json, strlen (oki_doki_json)
	);

	if (response) {
		(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_JSON);
		(void) http_response_add_content_length_header (response, strlen (oki_doki_json));

		retval = http_response_add_whitelist_cors_header (response, domain);

		(void) http_response_compile (response);
		
		http_response_delete (response);
	}

	return retval;

}

void http_tests_origins_whitelist (void) {

	(void) printf ("Testing HTTP origins integration...\n");

	srand ((unsigned int) time (NULL));

	Cerver *test_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"test-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	test_check_ptr (test_cerver);

	/*** web cerver configuration ***/
	test_check_ptr (test_cerver->cerver_data);
	HttpCerver *http_cerver = (HttpCerver *) test_cerver->cerver_data;

	test_check_unsigned_eq (http_cerver_add_origin_to_whitelist (http_cerver, domain_1), 0, NULL);
	test_check_unsigned_eq (http_cerver_add_origin_to_whitelist (http_cerver, domain_2), 0, NULL);

	http_cerver_print_origins_whitelist (http_cerver);

	http_cerver_init (http_cerver);

	/*** test ***/
	test_check_unsigned_eq (test_create_response (http_cerver, domain_1), 0, NULL);
	test_check_unsigned_eq (test_create_response (http_cerver, domain_2), 0, NULL);
	test_check_unsigned_eq (test_create_response (http_cerver, domain_3), 1, NULL);

	cerver_teardown (test_cerver);

	(void) printf ("Done!\n");

}
