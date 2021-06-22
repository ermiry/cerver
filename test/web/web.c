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

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

static Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

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

// GET /render
static void main_render_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (http_response_render_file (
		http_receive, HTTP_STATUS_OK, "./public/index.html"
	)) {
		cerver_log_error (
			"Failed to send ./public/index.html"
		);
	}

}

// GET /render/text
// test http_response_render_text ()
static void text_render_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const char *text = "<!DOCTYPE html><html><head><meta charset=\"utf-8\" /><title>Cerver</title></head><body><h2>text_handler () works!</h2></body></html>";
	size_t text_len = strlen (text);

	if (http_response_render_text (http_receive, HTTP_STATUS_OK, text, text_len)) {
		cerver_log_error ("text_handler () has failed!");
	}

}

// GET /render/json
// test http_response_render_json ()
static void json_render_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const char *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	if (http_response_render_json (http_receive, HTTP_STATUS_OK, json, json_len)) {
		cerver_log_error ("json_handler () has failed!");
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

// GET /json/create/key
// test http_response_create_json_key_value ()
static void json_create_key_value_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_create_json_key_value (
		HTTP_STATUS_OK, "msg", "okay"
	);

	if (res) {
		#ifdef TEST_DEBUG
		http_response_print (res);
		#endif
		
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// GET /json/create/message
// test http_response_json_msg ()
static void json_create_message_handler (
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

// GET /json/send/message
// test http_response_json_msg_send ()
static void json_send_message_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_msg_send (
		http_receive, HTTP_STATUS_OK, "okay"
	);

}

// GET /json/create/error
// test http_response_json_error ()
static void json_create_error_handler (
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

// GET /json/send/error
// test http_response_json_msg_send ()
static void json_send_error_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_error_send (
		http_receive, HTTP_STATUS_BAD_REQUEST, "bad request"
	);

}

// GET /json/create/custom
// test http_response_json_custom ()
static void json_create_custom_handler (
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

// GET /json/send/custom
// test http_response_json_custom_send ()
static void json_send_custom_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_custom_send (
		http_receive,
		HTTP_STATUS_OK, "{\"oki\": \"doki\"}"
	);

}

// GET /json/create/reference
// test http_response_json_custom_reference ()
static void json_create_reference_handler (
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

// GET /json/send/reference
// test http_response_json_custom_reference_send ()
static void json_send_reference_handler (
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
		"web-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	if (web_cerver) {
		/*** cerver configuration ***/
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

		http_cerver_static_path_add (http_cerver, "./public");

		// GET /render
		HttpRoute *render_route = http_route_create (REQUEST_METHOD_GET, "render", main_render_handler);
		http_cerver_route_register (http_cerver, render_route);

		// GET /render/text
		HttpRoute *render_text_route = http_route_create (REQUEST_METHOD_GET, "render/text", text_render_handler);
		http_cerver_route_register (http_cerver, render_text_route);

		// GET /render/json
		HttpRoute *render_json_route = http_route_create (REQUEST_METHOD_GET, "render/json", json_render_handler);
		http_cerver_route_register (http_cerver, render_json_route);

		// GET /json/create
		HttpRoute *json_create_route = http_route_create (REQUEST_METHOD_GET, "json/create", json_create_handler);
		http_cerver_route_register (http_cerver, json_create_route);

		// GET /json/create/key
		HttpRoute *json_create_key_value_route = http_route_create (REQUEST_METHOD_GET, "json/create/key", json_create_key_value_handler);
		http_cerver_route_register (http_cerver, json_create_key_value_route);

		// GET /json/create/message
		HttpRoute *json_create_message_route = http_route_create (REQUEST_METHOD_GET, "json/create/message", json_create_message_handler);
		http_cerver_route_register (http_cerver, json_create_message_route);

		// GET /json/send/message
		HttpRoute *json_send_message_route = http_route_create (REQUEST_METHOD_GET, "json/send/message", json_send_message_handler);
		http_cerver_route_register (http_cerver, json_send_message_route);

		// GET /json/create/error
		HttpRoute *json_create_error_route = http_route_create (REQUEST_METHOD_GET, "json/create/error", json_create_error_handler);
		http_cerver_route_register (http_cerver, json_create_error_route);

		// GET /json/send/error
		HttpRoute *json_send_error_route = http_route_create (REQUEST_METHOD_GET, "json/send/error", json_send_error_handler);
		http_cerver_route_register (http_cerver, json_send_error_route);

		// GET /json/create/custom
		HttpRoute *json_create_custom_route = http_route_create (REQUEST_METHOD_GET, "json/create/custom", json_create_custom_handler);
		http_cerver_route_register (http_cerver, json_create_custom_route);

		// GET /json/send/custom
		HttpRoute *json_send_custom_route = http_route_create (REQUEST_METHOD_GET, "json/send/custom", json_send_custom_handler);
		http_cerver_route_register (http_cerver, json_send_custom_route);

		// GET /json/create/reference
		HttpRoute *json_create_reference_route = http_route_create (REQUEST_METHOD_GET, "json/create/reference", json_create_reference_handler);
		http_cerver_route_register (http_cerver, json_create_reference_route);

		// GET /json/send/reference
		HttpRoute *json_send_reference_route = http_route_create (REQUEST_METHOD_GET, "json/send/reference", json_send_reference_handler);
		http_cerver_route_register (http_cerver, json_send_reference_route);

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