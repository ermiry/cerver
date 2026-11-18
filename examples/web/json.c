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

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

// the signals only stop the cerver, it is released when cerver_start () returns
static void finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

}

#pragma endregion

#pragma region routes

// GET /json
void json_fetch_handler (CerverHttpContext *context) {

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
void json_upload_handler (CerverHttpContext *context) {

	if (!json_load_from_body (context)) {
		cerver_log_success ("JSON is valid!");

		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// POST /json/big
void json_upload_big_handler (CerverHttpContext *context) {

	if (!json_load_from_body (context)) {
		cerver_log_success ("Big JSON is valid!");

		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);
	}

	else {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
	}

}

// GET /json/int
void json_int_handler (CerverHttpContext *context) {

	static const int value = 18;

	(void) cerver_http_response_send_json_integer (
		context, HTTP_STATUS_OK,
		"integer", value
	);

}

// GET /json/large
void json_large_handler (CerverHttpContext *context) {

	static const long value = 1800000;

	(void) cerver_http_response_send_json_integer (
		context, HTTP_STATUS_OK,
		"large", value
	);

}

// GET /json/real
void json_real_handler (CerverHttpContext *context) {

	static const double value = 18.123;

	(void) cerver_http_response_send_json_real (
		context, HTTP_STATUS_OK,
		"real", value
	);

}

// GET /json/bool
void json_bool_handler (CerverHttpContext *context) {

	static const bool value = true;

	(void) cerver_http_response_send_json_boolean (
		context, HTTP_STATUS_OK,
		"bool", value
	);

}

// GET /json/msg
void msg_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "oki"
	);

}

// GET /json/error
void error_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_error (
		context, HTTP_STATUS_BAD_REQUEST, "bad"
	);

}

// GET /json/key
void key_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_key_value (
		context,
		HTTP_STATUS_OK, "key", "value"
	);

}

// GET /json/custom
void custom_handler (CerverHttpContext *context) {

	const char *json = "{\"oki\": \"doki\"}";

	(void) cerver_http_response_send_json (
		context, json, strlen (json)
	);

}

// GET /json/reference
void reference_handler (CerverHttpContext *context) {

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
		cerver_set_alias (web_cerver, "json");

		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);

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

		// GET /json/int
		(void) cerver_http_group_get (json_group, "int", json_int_handler);

		// GET /json/large
		(void) cerver_http_group_get (json_group, "large", json_large_handler);

		// GET /json/real
		(void) cerver_http_group_get (json_group, "real", json_real_handler);

		// GET /json/bool
		(void) cerver_http_group_get (json_group, "bool", json_bool_handler);

		// GET /json/msg
		(void) cerver_http_group_get (json_group, "msg", msg_handler);

		// GET /json/error
		(void) cerver_http_group_get (json_group, "error", error_handler);

		// GET /json/key
		(void) cerver_http_group_get (json_group, "key", key_handler);

		// GET /json/custom
		(void) cerver_http_group_get (json_group, "custom", custom_handler);

		// GET /json/reference
		(void) cerver_http_group_get (json_group, "reference", reference_handler);

		if (!cerver_start (web_cerver)) {
			finish ();
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
