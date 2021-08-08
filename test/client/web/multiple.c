#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

static const char *address = { "127.0.0.1:8080" };

static size_t multiple_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int multiple_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	// POST /upload
	(void) snprintf (actual_address, 128, "%s/upload", address);
	errors |= curl_upload_file (
		curl, actual_address,
		HTTP_STATUS_OK,
		multiple_request_all_data_handler, data_buffer,
		"./test/web/img/ermiry.png"
	);

	// POST /multiple
	(void) snprintf (actual_address, 128, "%s/multiple", address);
	errors |= curl_upload_two_files (
		curl, actual_address,
		HTTP_STATUS_OK,
		multiple_request_all_data_handler, data_buffer,
		"./test/web/img/ermiry.png",
		"./test/web/img/github.jpeg"
	);

	// POST /iter/good
	(void) snprintf (actual_address, 128, "%s/iter/good", address);
	errors |= curl_upload_file_with_extra_value (
		curl, actual_address,
		HTTP_STATUS_OK,
		"./test/web/img/ermiry.png",
		"key", "value"
	);

	// POST /iter/empty
	static const char *json = { "{ \"key\": \"value\" }" };
	const size_t json_len = strlen (json);

	(void) snprintf (actual_address, 128, "%s/iter/empty", address);
	errors |= curl_simple_post (
		curl, actual_address,
		HTTP_STATUS_OK,
		json, json_len
	);

	// POST /discard - keep
	(void) snprintf (actual_address, 128, "%s/discard", address);
	errors |= curl_upload_file_with_extra_value (
		curl, actual_address,
		HTTP_STATUS_OK,
		"./test/web/img/ermiry.png",
		"key", "value"
	);

	// POST /discard - discard
	errors |= curl_upload_file_with_extra_value (
		curl, actual_address,
		HTTP_STATUS_BAD_REQUEST,
		"./test/web/img/ermiry.png",
		"key", "discard"
	);

	return errors;

}

// perform requests to every route
static unsigned int multiple_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!multiple_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"multiple_request_all () - All requests succeeded!"
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

	code = multiple_request_all ();

	cerver_log_end ();

	return code;

}
