#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>
#include <cerver/http/status.h>

#include <cerver/http/json/json.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "curl.h"

static const char *address = { "127.0.0.1:8080/api/users" };

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

// POST api/users/register
static unsigned int api_request_register (
	CURL *curl, const char *actual_address
) {

	unsigned int retval = 1;

	char data[4096] = { 0 };
	(void) snprintf (data, 4096, "name=%s&username=%s&password=%s", name, username, password);

	char *encoded_data = http_url_encode (data);
	if (encoded_data) {
		retval = curl_simple_post (
			curl, actual_address,
			HTTP_STATUS_OK,
			encoded_data, strlen (encoded_data)
		);
		
		free (encoded_data);
	}

	return retval;

}

// POST api/users/login
static unsigned int api_request_login (
	CURL *curl, const char *actual_address, char *data_buffer
) {

	char data[4096] = { 0 };
	(void) snprintf (data, 4096, "username=%s&password=%s", username, password);

	char *encoded_data = http_url_encode (data);
	if (encoded_data) {
		if (!curl_simple_post_handle_data (
			curl, actual_address,
			HTTP_STATUS_OK,
			encoded_data, strlen (encoded_data),
			api_request_all_data_handler, data_buffer
		)) {
			// get token
			json_error_t json_error = { 0 };
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
		
		free (encoded_data);
	}

	return token ? 0 : 1;

}

static unsigned int api_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	// GET /api/users
	errors |= curl_simple_handle_data (
		curl, address,
		HTTP_STATUS_OK,
		api_request_all_data_handler, data_buffer
	);

	// POST api/users/register
	(void) snprintf (actual_address, 128, "%s/register", address);
	errors |= api_request_register (curl, actual_address);

	// POST api/users/login
	(void) snprintf (actual_address, 128, "%s/login", address);
	errors |= api_request_login (curl, actual_address, data_buffer);

	// GET api/users/profile
	(void) snprintf (actual_address, 128, "%s/profile", address);
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

int main (int argc, char **argv) {

	int code = 0;

	cerver_log_init ();

	code = (int) api_request_all ();

	cerver_log_end ();

	return code;

}
