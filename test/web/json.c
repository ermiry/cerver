#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/http/json/json.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	if (web_cerver) {
		cerver_stats_print (web_cerver, false, false);
		cerver_log_msg ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) web_cerver->cerver_data);
		cerver_log_line_break ();
		cerver_teardown (web_cerver);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region routes

// GET /json
static void json_fetch_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const char *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	if (http_response_render_json (http_receive, HTTP_STATUS_OK, json, json_len)) {
		cerver_log_error ("json_handler () has failed!");
	}

}

static unsigned int json_load_from_body (
	const String *request_body
) {

	unsigned int retval = 1;

	json_error_t json_error =  { 0 };
	json_t *json_body = json_loads (request_body->str, 0, &json_error);
	if (json_body) {
		json_decref (json_body);

		retval = 0;
	}

	else {
		cerver_log_error (
			"json_loads () - json error on line %d: %s\n",
			json_error.line, json_error.text
		);
	}

	return retval;

}

// POST /json
static void json_upload_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (!json_load_from_body (request->body)) {
		cerver_log_success ("JSON is valid!");

		http_response_send (oki_doki, http_receive);
	}

	else {
		http_response_send (bad_request_error, http_receive);
	}

}

// POST /json/big
static void json_upload_big_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (!json_load_from_body (request->body)) {
		cerver_log_success ("Big JSON is valid!");

		http_response_send (oki_doki, http_receive);
	}

	else {
		http_response_send (bad_request_error, http_receive);
	}

}

// GET /json/create
// test http_response_create_json ()
static void json_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const char *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	HttpResponse *res = http_response_create_json (
		HTTP_STATUS_OK, json, json_len
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/int
static void json_int_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const int value = 18;

	(void) http_response_json_int_value_send (
		http_receive, HTTP_STATUS_OK,
		"integer", value
	);

}

// GET /json/int/create
static void json_int_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const int value = 18;

	HttpResponse *res = http_response_json_int_value (
		HTTP_STATUS_OK, "integer", value
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/large
static void json_large_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const long value = 1800000;

	(void) http_response_json_large_int_value_send (
		http_receive, HTTP_STATUS_OK,
		"large", value
	);

}

// GET /json/large/create
static void json_large_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const long value = 1800000;

	HttpResponse *res = http_response_json_large_int_value (
		HTTP_STATUS_OK, "large", value
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/real
static void json_real_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const double value = 18.123;

	(void) http_response_json_real_value_send (
		http_receive, HTTP_STATUS_OK,
		"real", value
	);

}

// GET /json/real/create
static void json_real_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const double value = 18.123;

	HttpResponse *res = http_response_json_real_value (
		HTTP_STATUS_OK, "real", value
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/bool
static void json_bool_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const bool value = true;

	(void) http_response_json_bool_value_send (
		http_receive, HTTP_STATUS_OK,
		"bool", value
	);

}

// GET /json/bool/create
static void json_bool_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	static const bool value = true;

	HttpResponse *res = http_response_json_bool_value (
		HTTP_STATUS_OK, "bool", value
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/msg
static void msg_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_msg_send (
		http_receive, HTTP_STATUS_OK, "oki"
	);

}

// GET /json/msg/create
// test http_response_json_msg ()
static void msg_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "okay"
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/error
static void error_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_error_send (
		http_receive, HTTP_STATUS_BAD_REQUEST, "bad"
	);

}

// GET /json/error/create
// test http_response_json_error ()
static void error_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_error (
		HTTP_STATUS_BAD_REQUEST, "bad request"
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/key
static void key_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_key_value_send (
		http_receive,
		HTTP_STATUS_OK, "key", "value"
	);

}

// GET /json/custom
static void custom_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_custom_send (
		http_receive,
		HTTP_STATUS_OK, "{\"oki\": \"doki\"}"
	);

}

// GET /json/custom/create
// test http_response_json_custom ()
static void custom_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_custom (
		HTTP_STATUS_OK, "{\"oki\": \"doki\"}"
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/reference
static void reference_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	char *json = (char *) calloc (256, sizeof (char));
	if (json) {
		(void) strncpy (json, "{\"oki\": \"doki\"}", 256);

		(void) http_response_json_custom_reference_send (
			http_receive,
			HTTP_STATUS_OK, json, strlen (json)
		);

		free (json);
	}

}

// GET /json/reference/create
// test http_response_json_custom_reference ()
static void reference_create_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	char *json = (char *) calloc (256, sizeof (char));
	if (json) {
		(void) strncpy (json, "{\"oki\": \"doki\"}", 256);

		HttpResponse *res = http_response_json_custom_reference (
			HTTP_STATUS_OK, json, strlen (json)
		);

		if (res) {
			#ifdef TEST_DEBUG
			http_response_print (res);
			#endif

			http_response_send (res, http_receive);
			http_response_delete (res);
		}

		free (json);
	}

}

#pragma endregion

#pragma region start

int main (int argc, char **argv) {

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
		cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

		// GET /json
		HttpRoute *json_route = http_route_create (REQUEST_METHOD_GET, "json", json_fetch_handler);
		http_cerver_route_register (http_cerver, json_route);

		// POST /json
		http_route_set_handler (json_route, REQUEST_METHOD_POST, json_upload_handler);

		// POST /json/big
		HttpRoute *json_big_route = http_route_create (REQUEST_METHOD_POST, "big", json_upload_big_handler);
		http_route_child_add (json_route, json_big_route);

		// GET /json/create
		HttpRoute *create_route = http_route_create (REQUEST_METHOD_GET, "create", json_create_handler);
		http_route_child_add (json_route, create_route);

		// GET /json/int
		HttpRoute *int_route = http_route_create (REQUEST_METHOD_GET, "int", json_int_handler);
		http_route_child_add (json_route, int_route);

		// GET /json/int/create
		HttpRoute *int_create_route = http_route_create (REQUEST_METHOD_GET, "int/create", json_int_create_handler);
		http_route_child_add (json_route, int_create_route);

		// GET /json/large
		HttpRoute *large_route = http_route_create (REQUEST_METHOD_GET, "large", json_large_handler);
		http_route_child_add (json_route, large_route);

		// GET /json/large/create
		HttpRoute *large_create_route = http_route_create (REQUEST_METHOD_GET, "large/create", json_large_create_handler);
		http_route_child_add (json_route, large_create_route);

		// GET /json/real
		HttpRoute *real_route = http_route_create (REQUEST_METHOD_GET, "real", json_real_handler);
		http_route_child_add (json_route, real_route);

		// GET /json/real/create
		HttpRoute *real_create_route = http_route_create (REQUEST_METHOD_GET, "real/create", json_real_create_handler);
		http_route_child_add (json_route, real_create_route);

		// GET /json/bool
		HttpRoute *bool_route = http_route_create (REQUEST_METHOD_GET, "bool", json_bool_handler);
		http_route_child_add (json_route, bool_route);

		// GET /json/bool/create
		HttpRoute *bool_create_route = http_route_create (REQUEST_METHOD_GET, "bool/create", json_bool_create_handler);
		http_route_child_add (json_route, bool_create_route);

		// GET /json/msg
		HttpRoute *msg_route = http_route_create (REQUEST_METHOD_GET, "msg", msg_handler);
		http_route_child_add (json_route, msg_route);

		// GET /json/msg/create
		HttpRoute *msg_create_route = http_route_create (REQUEST_METHOD_GET, "msg/create", msg_create_handler);
		http_route_child_add (json_route, msg_create_route);

		// GET /json/error
		HttpRoute *error_route = http_route_create (REQUEST_METHOD_GET, "error", error_handler);
		http_route_child_add (json_route, error_route);

		// GET /json/error/create
		HttpRoute *error_create_route = http_route_create (REQUEST_METHOD_GET, "error/create", error_create_handler);
		http_route_child_add (json_route, error_create_route);

		// GET /json/key
		HttpRoute *key_route = http_route_create (REQUEST_METHOD_GET, "key", key_handler);
		http_route_child_add (json_route, key_route);

		// GET /json/custom
		HttpRoute *custom_route = http_route_create (REQUEST_METHOD_GET, "custom", custom_handler);
		http_route_child_add (json_route, custom_route);

		// GET /json/custom/create
		HttpRoute *custom_create_route = http_route_create (REQUEST_METHOD_GET, "custom/create", custom_create_handler);
		http_route_child_add (json_route, custom_create_route);

		// GET /json/reference
		HttpRoute *reference_route = http_route_create (REQUEST_METHOD_GET, "reference", reference_handler);
		http_route_child_add (json_route, reference_route);

		// GET /json/reference/create
		HttpRoute *reference_create_route = http_route_create (REQUEST_METHOD_GET, "reference/create", reference_create_handler);
		http_route_child_add (json_route, reference_create_route);

		if (cerver_start (web_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				web_cerver->info->name
			);

			cerver_delete (web_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (web_cerver);
	}

	cerver_end ();

	return 0;

}

#pragma endregion
