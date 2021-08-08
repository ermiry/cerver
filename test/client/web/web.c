#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define ADDRESS_SIZE			128

static const char *address = { "127.0.0.1:8080" };

static size_t web_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int web_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[ADDRESS_SIZE] = { 0 };

	// GET /render
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/render", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /render/text
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/render/text", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /render/json
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/render/json", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/create/key
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create/key", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/create/message
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create/message", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/send/message
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/send/message", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/create/error
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create/error", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/send/error
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/send/error", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/create/custom
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create/custom", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/send/custom
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/send/custom", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/create/reference
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create/reference", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	// GET /json/send/reference
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/send/reference", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		web_request_all_data_handler, data_buffer
	);

	return errors;

}

// perform requests to every route
static unsigned int web_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!web_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"web_request_all () - All requests succeeded!"
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

	code = (int) web_request_all ();

	cerver_log_end ();

	return code;

}
