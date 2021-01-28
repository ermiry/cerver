#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/utils/log.h>

#include "curl.h"

static const char *address = { "localhost.com:8080" };

static size_t api_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int api_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	// char actual_address[128] = { 0 };

	// GET /api/users
	errors |= curl_simple_handle_data (
		curl, address,
		api_request_all_data_handler, data_buffer
	);

	// GET /test
	// (void) snprintf (actual_address, 128, "%s/test", address);
	// errors |= curl_simple_handle_data (
	// 	curl, actual_address,
	// 	api_request_all_data_handler, data_buffer
	// );

	return errors;

}

// perform requests to every route
static unsigned int api_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!api_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"api_request_all () - All requests succeeded!"
			);

			cerver_log_line_break ();
			cerver_log_line_break ();

			retval = 0;
		}
	}

	curl_easy_cleanup (curl);

	return retval;

}

int main (int argc, char **argv) {

	cerver_log_init ();

	(void) api_request_all ();

	cerver_log_end ();

	return 0;

}