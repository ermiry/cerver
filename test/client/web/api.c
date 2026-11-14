#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/http.h>
#include <cerver/http/json/json.h>
#include <cerver/http/requests/url.h>
#include <cerver/http/responses/status.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "curl.h"

#define API_CLIENT_PATH			"/api/users"

// the body of a form, its pairs are "key=value" separated by '&'
#define API_CLIENT_FORM_SIZE			4096
#define API_CLIENT_FORM_PAIR_FORMAT		"%s%s=%s"
#define API_CLIENT_FORM_SEPARATOR		"&"

static char address[WEB_TEST_ADDRESS_SIZE] = { 0 };

static const char *name = { "Erick Salas" };
static const char *username = { "ermiry" };
static const char *password = { "049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b" };

static char *token = NULL;

static size_t api_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

// adds a pair to the body of a form: only its value is encoded,
// the separators of the pairs ('=' and '&') are never encoded
// returns true on success
static bool api_form_add (char *form, const char *key, const char *value) {

	bool added = false;

	char *encoded = http_url_encode (value);
	if (encoded) {
		const size_t length = strlen (form);
		const int written = snprintf (
			form + length, API_CLIENT_FORM_SIZE - length,
			API_CLIENT_FORM_PAIR_FORMAT, length ? API_CLIENT_FORM_SEPARATOR : "", key, encoded
		);

		added = (written > 0) && ((size_t) written < (API_CLIENT_FORM_SIZE - length));

		free (encoded);
	}

	return added;

}

// POST api/users/register
static unsigned int api_request_register (
	CURL *curl, const char *actual_address
) {

	unsigned int retval = 1;

	char data[API_CLIENT_FORM_SIZE] = { 0 };
	if (
		api_form_add (data, "name", name)
		&& api_form_add (data, "username", username)
		&& api_form_add (data, "password", password)
	) {
		retval = curl_simple_post (
			curl, actual_address,
			HTTP_STATUS_OK,
			data, strlen (data)
		);
	}

	return retval;

}

// POST api/users/login
static unsigned int api_request_login (
	CURL *curl, const char *actual_address, char *data_buffer
) {

	char data[API_CLIENT_FORM_SIZE] = { 0 };
	if (
		api_form_add (data, "username", username)
		&& api_form_add (data, "password", password)
	) {
		if (!curl_simple_post_handle_data (
			curl, actual_address,
			HTTP_STATUS_OK,
			data, strlen (data),
			api_request_all_data_handler, data_buffer
		)) {
			// get token
			json_error_t json_error = CERVER_ZERO_INITIALIZER;
			json_t *json = json_loads (data_buffer, 0, &json_error);
			if (json) {
				const char *key = NULL;
				json_t *value = NULL;
				json_object_foreach (json, key, value) {
					if (!strcmp (key, "token")) {
						token = strdup (json_string_value (value));
						// (void) printf ("token: \"%s\"\n", *title);
					}
				}

				json_decref (json);
			}

			else {
				cerver_log_error (
					"api_request_login () - failed to load json: %s",
					json_error.text
				);
			}
		}
	}

	return token ? 0 : 1;

}

static unsigned int api_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[WEB_TEST_URL_SIZE] = { 0 };

	// GET /api/users
	errors |= curl_simple_handle_data (
		curl, address,
		HTTP_STATUS_OK,
		api_request_all_data_handler, data_buffer
	);

	// POST api/users/register
	(void) snprintf (actual_address, WEB_TEST_URL_SIZE,"%s/register", address);
	errors |= api_request_register (curl, actual_address);

	// POST api/users/login
	(void) snprintf (actual_address, WEB_TEST_URL_SIZE,"%s/login", address);
	errors |= api_request_login (curl, actual_address, data_buffer);

	// GET api/users/profile
	(void) snprintf (actual_address, WEB_TEST_URL_SIZE,"%s/profile", address);
	errors |= curl_simple_with_auth (
		curl, actual_address,
		HTTP_STATUS_OK,
		token
	);

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

int main (void) {

	int code = 0;

	cerver_log_init ();

	web_test_address (address, API_CLIENT_PATH);

	code = (int) api_request_all ();

	cerver_log_end ();

	return code;

}
