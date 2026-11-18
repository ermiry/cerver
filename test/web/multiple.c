#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/files/files.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/core/uploads.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "../address.h"
#include "../handler.h"

static Cerver *web_cerver = NULL;

// the public API does not give the socket of a request: the generators number the uploads
static unsigned int uploads_sequence = 0;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

static void multiple_finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

}

#pragma endregion

#pragma region routes

static const char *multiple_view_text (const CerverHttpView view) {

	return cerver_http_view_is_missing (view) ? "" : view.data;

}

// prints the names that the client gave to the files of the request
static void multiple_print_filenames (const CerverHttpContext *context) {

	if (!cerver_http_request_files_count (context)) return;

	(void) printf ("\nAll filenames: \n");
	int count = 0;
	for (
		const CerverHttpPart *part = cerver_http_request_part_next (context, NULL);
		part;
		part = cerver_http_request_part_next (context, part)
	) {
		if (cerver_http_part_is_file (part)) {
			(void) printf ("[%d]: %s\n", count, multiple_view_text (cerver_http_part_filename (part)));
			count += 1;
		}
	}

}

// prints where the files of the request were saved
static void multiple_print_saved_filenames (const CerverHttpContext *context) {

	if (!cerver_http_request_files_count (context)) return;

	(void) printf ("\nAll saved filenames: \n");
	int count = 0;
	for (
		const CerverHttpPart *part = cerver_http_request_part_next (context, NULL);
		part;
		part = cerver_http_request_part_next (context, part)
	) {
		const CerverHttpView saved_filename = cerver_http_part_saved_filename (part);
		if (!cerver_http_view_is_missing (saved_filename)) {
			(void) printf ("[%d]: %s\n", count, saved_filename.data);
			count += 1;
		}
	}

}

static void multiple_print_part (const CerverHttpPart *part) {

	if (cerver_http_part_is_file (part)) {
		const CerverHttpView filename = cerver_http_part_filename (part);
		const CerverHttpView saved_filename = cerver_http_part_saved_filename (part);

		(void) printf (
			"FILE: %s - [%zu] %s -> [%zu] %s\n",
			multiple_view_text (cerver_http_part_name (part)),
			filename.length, multiple_view_text (filename),
			saved_filename.length, multiple_view_text (saved_filename)
		);
	}

	else {
		const CerverHttpView value = cerver_http_part_value (part);

		(void) printf (
			"VALUE: %s - [%zu] %s\n",
			multiple_view_text (cerver_http_part_name (part)),
			value.length, multiple_view_text (value)
		);
	}

}

// POST /upload
static void upload_handler (CerverHttpContext *context) {

	multiple_print_filenames (context);

	multiple_print_saved_filenames (context);

	const static char *json = { "{ \"msg\": \"Upload route works!\" }" };
	const size_t json_len = strlen (json);

	(void) cerver_http_response_send_json (context, json, json_len);

}

// POST /multiple
static void multiple_handler (CerverHttpContext *context) {

	multiple_print_filenames (context);

	multiple_print_saved_filenames (context);

	const static char *json = { "{ \"msg\": \"Multiple route works!\" }" };
	const size_t json_len = strlen (json);

	(void) cerver_http_response_send_json (context, json, json_len);

}

// POST /iter/good
static void iter_good_handler (CerverHttpContext *context) {

	const CerverHttpPart *first = cerver_http_request_part_next (context, NULL);
	if (first) {
		for (
			const CerverHttpPart *part = first;
			part;
			part = cerver_http_request_part_next (context, part)
		) {
			multiple_print_part (part);
		}

		const static char *json = { "{ \"msg\": \"Iter good route works!\" }" };
		const size_t json_len = strlen (json);

		(void) cerver_http_response_send_json (context, json, json_len);
	}

}

// POST /iter/empty
static void iter_empty_handler (CerverHttpContext *context) {

	if (!cerver_http_request_part_next (context, NULL)) {
		const static char *json = { "{ \"msg\": \"Iter empty route works!\" }" };
		const size_t json_len = strlen (json);

		(void) cerver_http_response_send_json (context, json, json_len);
	}

}

// POST /discard
static void discard_handler (CerverHttpContext *context) {

	const CerverHttpView key = cerver_http_request_part_value (context, "key");
	if (cerver_http_view_equals (key, "value")) {
		cerver_log_success ("Success request, keeping multi part files...");

		(void) cerver_http_response_send_json_message (
			context, HTTP_STATUS_OK, "Success request!"
		);
	}

	else {
		cerver_log_error ("key != value");
		cerver_log_debug ("Discarding multi part files...");
		cerver_http_request_discard_files (context);
		(void) cerver_http_response_send_json_error (context, HTTP_STATUS_BAD_REQUEST, "Bad request!");
	}

}

#pragma endregion

#pragma region start

static void custom_uploads_dirname_generator (CerverHttpContext *context) {

	cerver_http_request_set_uploads_directory (
		context,
		"%u-%ld",
		__atomic_add_fetch (&uploads_sequence, 1, __ATOMIC_SEQ_CST),
		time (NULL)
	);

}

static void custom_uploads_filename_generator (CerverHttpContext *context) {

	const CerverHttpPart *part = cerver_http_request_current_part (context);

	cerver_http_part_set_generated_filename (
		part,
		"%u-%ld-%s",
		__atomic_add_fetch (&uploads_sequence, 1, __ATOMIC_SEQ_CST),
		time (NULL),
		multiple_view_text (cerver_http_part_filename (part))
	);

}

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

		(void) files_create_dir ("uploads", 0777);
		cerver_http_set_uploads_path (http_cerver, "uploads");

		cerver_http_set_uploads_filename_generator (
			http_cerver, custom_uploads_filename_generator
		);

		cerver_http_set_uploads_dirname_generator (
			http_cerver, custom_uploads_dirname_generator
		);

		// every endpoint receives a multi-part body
		CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");
		cerver_http_group_set_modifier (main_group, HTTP_ROUTE_MODIFIER_MULTI_PART);

		// POST /upload
		(void) cerver_http_group_post (main_group, "upload", upload_handler);

		// POST /multiple
		(void) cerver_http_group_post (main_group, "multiple", multiple_handler);

		// POST /iter/good
		(void) cerver_http_group_post (main_group, "iter/good", iter_good_handler);

		// POST /iter/empty
		(void) cerver_http_group_post (main_group, "iter/empty", iter_empty_handler);

		// POST /discard, its handler deletes the files of the request: it runs in a thread
		CerverHttpEndpoint *discard = cerver_http_group_post (main_group, "discard", discard_handler);
		cerver_http_endpoint_set_pool (discard, HTTP_POOL_DEFAULT_NAME);

		if (!cerver_start (web_cerver)) {
			multiple_finish ();
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
