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

	if (http_response_send_file (
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

#pragma endregion

#pragma region start

int main (int argc, char **argv) {

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
