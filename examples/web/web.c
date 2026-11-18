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
#include <cerver/http/core/pool.h>
#include <cerver/http/core/public.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

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

// GET /
void main_handler (CerverHttpContext *context) {

	if (cerver_http_response_send_file (
		context, "./examples/web/public/index.html"
	)) {
		cerver_log_error (
			"Failed to send ./examples/web/public/index.html"
		);
	}

}

// GET /test
void test_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Test route works!"
	);

}

// GET /text
void text_handler (CerverHttpContext *context) {

	char const *text = "<!DOCTYPE html><html><head><meta charset=\"utf-8\" /><title>Cerver</title></head><body><h2>text_handler () works!</h2></body></html>";
	size_t text_len = strlen (text);

	if (cerver_http_response_send_content (context, HTTP_CONTENT_TYPE_HTML, text, text_len)) {
		cerver_log_error ("text_handler () has failed!");
	}

}

// GET /json
void json_handler (CerverHttpContext *context) {

	char const *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	if (cerver_http_response_send_json (context, json, json_len)) {
		cerver_log_error ("json_handler () has failed!");
	}

}

// GET /hola
void hola_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Hola route works!"
	);

}

// GET /adios
void adios_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Adios route works!"
	);

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

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (web_cerver);

		// the static paths and the handler of / send files, so they run in the threads of the pool
		(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

		cerver_http_static_path_add (http_cerver, "./examples/web/public");

		CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");

		// GET /
		(void) cerver_http_group_root_add (main_group, REQUEST_METHOD_GET, main_handler);

		// GET /test
		(void) cerver_http_group_get (main_group, "test", test_handler);

		// GET /text
		(void) cerver_http_group_get (main_group, "text", text_handler);

		// GET /json
		(void) cerver_http_group_get (main_group, "json", json_handler);

		// GET /hola
		(void) cerver_http_group_get (main_group, "hola", hola_handler);

		// GET /adios
		(void) cerver_http_group_get (main_group, "adios", adios_handler);

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
