#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define ADDRESS_SIZE			128

static const char *address = { "127.0.0.1:8080" };

static size_t admin_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int admin_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[ADDRESS_SIZE] = { 0 };

	// GET /
	errors |= curl_simple_handle_data (
		curl, address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	// GET /cerver/info
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/cerver/info", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	// GET /cerver/stats
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/cerver/stats", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	// GET /cerver/stats/filesystems
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/cerver/stats/filesystems", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	return errors;

}

// perform requests to every route
static unsigned int admin_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!admin_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"admin_request_all () - All requests succeeded!"
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

	(void) admin_request_all ();

	cerver_log_end ();

	return 0;

}
