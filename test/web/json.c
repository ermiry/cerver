#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/json/json.h>
#include <cerver/json/value.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "../address.h"
#include "../handler.h"

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

static void json_finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

}

#pragma endregion

#pragma region routes

// GET /json
static void json_fetch_handler (CerverHttpContext *context) {

	const char *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	if (cerver_http_response_send_json (context, json, json_len)) {
		cerver_log_error ("json_handler () has failed!");
	}

}

// the body of the request is an array or an object: it is read once, in the arena of the request
static unsigned int json_load_from_body (
	const CerverHttpContext *context
) {

	unsigned int retval = 1;

	CerverJsonError json_error = CERVER_ZERO_INITIALIZER;
	const CerverJsonValue *json_body = cerver_http_request_json (context, &json_error);
	if (cerver_json_is_array (json_body) || cerver_json_is_object (json_body)) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"cerver_http_request_json () - json error at byte %zu: %s\n",
			json_error.offset, cerver_json_error_to_string (json_error.code)
		);
	}

	return retval;

}

// POST /json
static void json_upload_handler (CerverHttpContext *context) {

	if (!json_load_from_body (context)) {
		cerver_log_success ("JSON is valid!");

		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// POST /json/big
static void json_upload_big_handler (CerverHttpContext *context) {

	if (!json_load_from_body (context)) {
		cerver_log_success ("Big JSON is valid!");

		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// GET /json/create
static void json_create_handler (CerverHttpContext *context) {

	const char *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	(void) cerver_http_response_send_json (context, json, json_len);

}

// GET /json/int
static void json_int_handler (CerverHttpContext *context) {

	static const int value = 18;

	(void) cerver_http_response_send_json_integer (
		context, HTTP_STATUS_OK,
		"integer", value
	);

}

// GET /json/int/create
static void json_int_create_handler (CerverHttpContext *context) {

	static const int value = 18;

	(void) cerver_http_response_send_json_integer (
		context, HTTP_STATUS_OK, "integer", value
	);

}

// GET /json/large
static void json_large_handler (CerverHttpContext *context) {

	static const long value = 1800000;

	(void) cerver_http_response_send_json_integer (
		context, HTTP_STATUS_OK,
		"large", value
	);

}

// GET /json/large/create
static void json_large_create_handler (CerverHttpContext *context) {

	static const long value = 1800000;

	(void) cerver_http_response_send_json_integer (
		context, HTTP_STATUS_OK, "large", value
	);

}

// GET /json/real
static void json_real_handler (CerverHttpContext *context) {

	static const double value = 18.123;

	(void) cerver_http_response_send_json_real (
		context, HTTP_STATUS_OK,
		"real", value
	);

}

// GET /json/real/create
static void json_real_create_handler (CerverHttpContext *context) {

	static const double value = 18.123;

	(void) cerver_http_response_send_json_real (
		context, HTTP_STATUS_OK, "real", value
	);

}

// GET /json/bool
static void json_bool_handler (CerverHttpContext *context) {

	static const bool value = true;

	(void) cerver_http_response_send_json_boolean (
		context, HTTP_STATUS_OK,
		"bool", value
	);

}

// GET /json/bool/create
static void json_bool_create_handler (CerverHttpContext *context) {

	static const bool value = true;

	(void) cerver_http_response_send_json_boolean (
		context, HTTP_STATUS_OK, "bool", value
	);

}

// GET /json/msg
static void msg_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "oki"
	);

}

// GET /json/msg/create
static void msg_create_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "okay"
	);

}

// GET /json/error
static void error_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_error (
		context, HTTP_STATUS_BAD_REQUEST, "bad"
	);

}

// GET /json/error/create
static void error_create_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_error (
		context, HTTP_STATUS_BAD_REQUEST, "bad request"
	);

}

// GET /json/key
static void key_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_key_value (
		context,
		HTTP_STATUS_OK, "key", "value"
	);

}

// GET /json/custom
static void custom_handler (CerverHttpContext *context) {

	const char *json = "{\"oki\": \"doki\"}";

	(void) cerver_http_response_send_json (
		context, json, strlen (json)
	);

}

// GET /json/custom/create
static void custom_create_handler (CerverHttpContext *context) {

	const char *json = "{\"oki\": \"doki\"}";

	(void) cerver_http_response_send_json (
		context, json, strlen (json)
	);

}

// GET /json/reference
static void reference_handler (CerverHttpContext *context) {

	char *json = (char *) calloc (256, sizeof (char));
	if (json) {
		(void) strncpy (json, "{\"oki\": \"doki\"}", 256);

		(void) cerver_http_response_send_json (
			context, json, strlen (json)
		);

		free (json);
	}

}

// GET /json/reference/create
static void reference_create_handler (CerverHttpContext *context) {

	char *json = (char *) calloc (256, sizeof (char));
	if (json) {
		(void) strncpy (json, "{\"oki\": \"doki\"}", 256);

		(void) cerver_http_response_send_json (
			context, json, strlen (json)
		);

		free (json);
	}

}

#pragma endregion

#pragma region start

int main (void) {

	srand (time (NULL));

	// register to the quit signal
	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Simple Web Cerver Example");
	printf ("\n");

	web_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"json-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	if (web_cerver) {
		/*** cerver configuration ***/
		(void) cerver_set_bind_address (web_cerver, test_address ());
		cerver_set_alias (web_cerver, "json");

		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_events_backend (web_cerver, test_events_backend ());
		cerver_set_io_threads (web_cerver, test_io_threads ());

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (web_cerver);

		CerverHttpGroup *json_group = cerver_http_group_create (http_cerver, "/json");

		// GET /json
		(void) cerver_http_group_root_add (json_group, REQUEST_METHOD_GET, json_fetch_handler);

		// POST /json
		(void) cerver_http_group_root_add (json_group, REQUEST_METHOD_POST, json_upload_handler);

		// POST /json/big
		(void) cerver_http_group_post (json_group, "big", json_upload_big_handler);

		// GET /json/create
		(void) cerver_http_group_get (json_group, "create", json_create_handler);

		// GET /json/int
		(void) cerver_http_group_get (json_group, "int", json_int_handler);

		// GET /json/int/create
		(void) cerver_http_group_get (json_group, "int/create", json_int_create_handler);

		// GET /json/large
		(void) cerver_http_group_get (json_group, "large", json_large_handler);

		// GET /json/large/create
		(void) cerver_http_group_get (json_group, "large/create", json_large_create_handler);

		// GET /json/real
		(void) cerver_http_group_get (json_group, "real", json_real_handler);

		// GET /json/real/create
		(void) cerver_http_group_get (json_group, "real/create", json_real_create_handler);

		// GET /json/bool
		(void) cerver_http_group_get (json_group, "bool", json_bool_handler);

		// GET /json/bool/create
		(void) cerver_http_group_get (json_group, "bool/create", json_bool_create_handler);

		// GET /json/msg
		(void) cerver_http_group_get (json_group, "msg", msg_handler);

		// GET /json/msg/create
		(void) cerver_http_group_get (json_group, "msg/create", msg_create_handler);

		// GET /json/error
		(void) cerver_http_group_get (json_group, "error", error_handler);

		// GET /json/error/create
		(void) cerver_http_group_get (json_group, "error/create", error_create_handler);

		// GET /json/key
		(void) cerver_http_group_get (json_group, "key", key_handler);

		// GET /json/custom
		(void) cerver_http_group_get (json_group, "custom", custom_handler);

		// GET /json/custom/create
		(void) cerver_http_group_get (json_group, "custom/create", custom_create_handler);

		// GET /json/reference
		(void) cerver_http_group_get (json_group, "reference", reference_handler);

		// GET /json/reference/create
		(void) cerver_http_group_get (json_group, "reference/create", reference_create_handler);

		if (!cerver_start (web_cerver)) {
			json_finish ();
		}

		else {
			cerver_log_error (
				"Failed to start %s!",
				web_cerver->info->name
			);

			(void) cerver_teardown (web_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion
