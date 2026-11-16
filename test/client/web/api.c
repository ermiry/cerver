#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/http.h>
#include <cerver/http/requests/url.h>
#include <cerver/http/responses/status.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>

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

// keeps a copy of the token of the json of a login
static void api_request_login_token (const char *data) {

	CerverJsonError json_error = CERVER_ZERO_INITIALIZER;
	CerverJsonDocument *document = cerver_json_parse (
		NULL, data, strlen (data), CERVER_JSON_FLAGS_NONE, NULL, &json_error
	);

	const CerverJsonValue *value = cerver_json_object_get (cerver_json_document_root (document), "token");
	if (cerver_json_is_string (value)) {
		token = strdup (cerver_json_string_value (value));
	}

	else {
		cerver_log_error (
			"api_request_login () - failed to get the token of the json: %s",
			cerver_json_error_to_string (json_error.code)
		);
	}

	cerver_json_document_delete (document);

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
			api_request_login_token (data_buffer);
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
