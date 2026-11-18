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

Cerver *videos_service = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (videos_service) (void) cerver_shutdown (videos_service);

}

// the signals only stop the cerver, it is released when cerver_start () returns
static void finish (void) {

	cerver_stats_print (videos_service, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (videos_service));
	cerver_log_line_break ();
	(void) cerver_teardown (videos_service);

}

#pragma endregion

#pragma region routes

// GET /
void main_handler (CerverHttpContext *context) {

	if (cerver_http_response_send_file (
		context, "./examples/web/public/video.html"
	)) {
		cerver_log_error (
			"Failed to send ./examples/web/public/video.html"
		);
	}

}

// GET /video
void video_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_video (context, "./data/videos/redis.webm");

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

		cerver_set_reusable_address_flags (videos_service, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (videos_service);

		// every handler and the static paths send files, so they run in the threads of the pool
		(void) cerver_http_set_handlers_pool (http_cerver, HTTP_POOL_DEFAULT_NAME);

		cerver_http_static_path_add (http_cerver, "./examples/web/public");

		CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");

		// GET /
		(void) cerver_http_group_root_add (main_group, REQUEST_METHOD_GET, main_handler);

		// GET /video
		(void) cerver_http_group_get (main_group, "video", video_handler);

		if (!cerver_start (videos_service)) {
			finish ();
		}

		else {
			cerver_log_error (
				"Failed to start %s!",
				videos_service->info->name
			);

			(void) cerver_teardown (videos_service);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}

#pragma endregion
