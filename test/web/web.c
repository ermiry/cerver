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

#include "../address.h"
#include "../handler.h"

static Cerver *web_cerver = NULL;

#pragma region end

// the signals only stop the cerver, it is released when cerver_start () returns
static void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

static void web_finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

}

#pragma endregion

#pragma region routes

// GET /render
static void main_render_handler (CerverHttpContext *context) {

	if (cerver_http_response_send_file (
		context, "./public/index.html"
	)) {
		cerver_log_error (
			"Failed to send ./public/index.html"
		);
	}

}

// GET /render/text
// test cerver_http_response_send_text ()
static void text_render_handler (CerverHttpContext *context) {

	const char *text = "<!DOCTYPE html><html><head><meta charset=\"utf-8\" /><title>Cerver</title></head><body><h2>text_handler () works!</h2></body></html>";
	size_t text_len = strlen (text);

	if (cerver_http_response_send_text (context, text, text_len)) {
		cerver_log_error ("text_handler () has failed!");
	}

}

// GET /render/json
// test cerver_http_response_send_json ()
static void json_render_handler (CerverHttpContext *context) {

	const char *json = "{\"msg\": \"okay\"}";
	size_t json_len = strlen (json);

	if (cerver_http_response_send_json (context, json, json_len)) {
		cerver_log_error ("json_handler () has failed!");
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

	cerver_version_print_full ();

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
		(void) cerver_set_bind_address (web_cerver, test_address ());
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_events_backend (web_cerver, test_events_backend ());
		cerver_set_io_threads (web_cerver, test_io_threads ());

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (web_cerver);

		// the render route and the static paths read files: the handlers run in threads
		(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

		cerver_http_static_path_add (http_cerver, "./public");

		CerverHttpGroup *render = cerver_http_group_create (http_cerver, "/render");

		// GET /render
		(void) cerver_http_group_root_add (render, REQUEST_METHOD_GET, main_render_handler);

		// GET /render/text
		(void) cerver_http_group_get (render, "text", text_render_handler);

		// GET /render/json
		(void) cerver_http_group_get (render, "json", json_render_handler);

		if (!cerver_start (web_cerver)) {
			web_finish ();
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
