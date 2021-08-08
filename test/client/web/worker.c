#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

static const char *address = { "127.0.0.1:8080" };

static size_t worker_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int worker_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	// GET /
	errors |= curl_simple_handle_data (
		curl, address,
		HTTP_STATUS_OK,
		worker_request_all_data_handler, data_buffer
	);

	// POST /work
	(void) snprintf (actual_address, 128, "%s/work", address);
	errors |= curl_post_two_form_values (
		curl, actual_address,
		HTTP_STATUS_OK,
		worker_request_all_data_handler, data_buffer,
		"name", "test",
		"value", "this is a test"
	);

	// GET /work/start
	(void) snprintf (actual_address, 128, "%s/work/start", address);
	errors |= curl_full_handle_data (
		actual_address,
		HTTP_STATUS_INTERNAL_SERVER_ERROR,
		worker_request_all_data_handler, data_buffer
	);

	// GET /work/stop
	(void) snprintf (actual_address, 128, "%s/work/stop", address);
	errors |= curl_full_handle_data (
		actual_address,
		HTTP_STATUS_OK,
		worker_request_all_data_handler, data_buffer
	);

	// GET /work/stop
	(void) snprintf (actual_address, 128, "%s/work/stop", address);
	errors |= curl_full_handle_data (
		actual_address,
		HTTP_STATUS_INTERNAL_SERVER_ERROR,
		worker_request_all_data_handler, data_buffer
	);

	// GET /work/start
	(void) snprintf (actual_address, 128, "%s/work/start", address);
	errors |= curl_full_handle_data (
		actual_address,
		HTTP_STATUS_OK,
		worker_request_all_data_handler, data_buffer
	);

	// GET /cerver/stats/workers
	(void) snprintf (actual_address, 128, "%s/cerver/stats/workers", address);
	errors |= curl_full_handle_data (
		actual_address,
		HTTP_STATUS_OK,
		worker_request_all_data_handler, data_buffer
	);

	return errors;

}

// perform requests to every route
static unsigned int worker_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!worker_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"worker_request_all () - All requests succeeded!"
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

	code = worker_request_all ();

	cerver_log_end ();

	return code;

}
