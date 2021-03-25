#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>

#include <cerver/http/json/json.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "curl.h"

static const char *address = { "127.0.0.1:8080" };

static size_t jobs_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

// POST /jobs
static unsigned int jobs_request_register (
	CURL *curl, const char *actual_address
) {

	unsigned int retval = 1;

	retval = curl_post_form_value (
		curl, actual_address,
		"value", "hola"
	);

	return retval;

}

static unsigned int jobs_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	// GET /
	errors |= curl_simple_handle_data (
		curl, address,
		jobs_request_all_data_handler, data_buffer
	);

	// POST /jobs
	(void) snprintf (actual_address, 128, "%s/jobs", address);
	errors |= jobs_request_register (curl, actual_address);

	return errors;

}

// perform requests to every route
static unsigned int jobs_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!jobs_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"jobs_request_all () - All requests succeeded!"
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

	int code = 0;

	cerver_log_init ();

	code = (int) jobs_request_all ();

	cerver_log_end ();

	return code;

}