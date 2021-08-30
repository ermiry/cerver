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

Cerver *videos_service = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	if (videos_service) {
		cerver_stats_print (videos_service, false, false);
		cerver_log_msg ("\nHTTP Cerver stats:\n");
		http_cerver_all_stats_print ((HttpCerver *) videos_service->cerver_data);
		cerver_log_line_break ();
		cerver_teardown (videos_service);
	}

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region routes

// GET /
void main_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (http_response_send_file (
		http_receive, HTTP_STATUS_OK, "./examples/web/public/video.html"
	)) {
		cerver_log_error (
			"Failed to send ./examples/web/public/video.html"
		);
	}

}

// GET /video
void video_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	// http_request_headers_print (request);

	http_response_handle_video (http_receive, "./data/videos/redis.webm");

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

	(void) printf ("\n");
	cerver_version_print_full ();
	(void) printf ("\n");

	cerver_log_debug ("Simple Web Cerver Example");
	(void) printf ("\n");

	videos_service = cerver_create (
		CERVER_TYPE_WEB,
		"videos-service",
		5000,
		PROTOCOL_TCP,
		false,
		2
	);

	if (videos_service) {
		/*** cerver configuration ***/
		cerver_set_alias (videos_service, "videos");

		cerver_set_receive_buffer_size (videos_service, 4096);
		cerver_set_thpool_n_threads (videos_service, 4);
		cerver_set_handler_type (videos_service, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (videos_service, true);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) videos_service->cerver_data;

		http_cerver_static_path_add (http_cerver, "./examples/web/public");

		// GET /
		HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "/", main_handler);
		http_cerver_route_register (http_cerver, main_route);

		// GET /video
		HttpRoute *video_route = http_route_create (REQUEST_METHOD_GET, "video", video_handler);
		http_cerver_route_register (http_cerver, video_route);

		if (cerver_start (videos_service)) {
			cerver_log_error (
				"Failed to start %s!",
				videos_service->info->name
			);

			cerver_delete (videos_service);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (videos_service);
	}

	cerver_end ();

	return 0;

}

#pragma endregion
