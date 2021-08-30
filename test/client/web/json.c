#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define ADDRESS_SIZE			128

static const char *address = { "127.0.0.1:8080" };

static size_t json_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int json_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[ADDRESS_SIZE] = { 0 };

	// GET /json
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// POST /json
	// TODO:

	// POST /json/big
	// TODO:

	// GET /json/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/int
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/int", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/int/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/int/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/large
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/large", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/large/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/large/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/real
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/real", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/real/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/real/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/bool
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/bool", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/bool/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/bool/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/msg
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/msg", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/msg/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/msg/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/error
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/error", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_BAD_REQUEST,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/error/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/error/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_BAD_REQUEST,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/key
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/key", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/custom
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/custom", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/custom/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/custom/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/reference
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/reference", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	// GET /json/reference/create
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/json/reference/create", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		json_request_all_data_handler, data_buffer
	);

	return errors;

}

// perform requests to every route
static unsigned int json_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!json_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"json_request_all () - All requests succeeded!"
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

	code = (int) json_request_all ();

	cerver_log_end ();

	return code;

}
