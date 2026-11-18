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

static Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
static void end (int dummy) {

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

// prints a filename of each file of the request
static void print_parts_filenames (
	const CerverHttpContext *context, const char *title,
	CerverHttpView (*filename_get)(const CerverHttpPart *)
) {

	if (cerver_http_request_files_count (context)) {
		(void) printf ("%s", title);
		int count = 0;
		for (
			const CerverHttpPart *part = cerver_http_request_part_next (context, NULL);
			part;
			part = cerver_http_request_part_next (context, part)
		) {
			if (cerver_http_part_is_file (part)) {
				(void) printf ("[%d]: %s\n", count, filename_get (part).data);
				count += 1;
			}
		}
	}

}

// POST /upload
static void upload_handler (CerverHttpContext *context) {

	print_parts_filenames (context, "\nAll filenames: \n", cerver_http_part_filename);
	print_parts_filenames (context, "\nAll saved filenames: \n", cerver_http_part_saved_filename);

	const static char *json = { "{ \"msg\": \"Upload route works!\" }" };
	const size_t json_len = strlen (json);

	// cerver_http_response_set_header (context, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

	(void) cerver_http_response_send_json (context, json, json_len);

}

// POST /multiple
static void multiple_handler (CerverHttpContext *context) {

	print_parts_filenames (context, "\nAll filenames: \n", cerver_http_part_filename);
	print_parts_filenames (context, "\nAll saved filenames: \n", cerver_http_part_saved_filename);

	const static char *json = { "{ \"msg\": \"Multiple route works!\" }" };
	const size_t json_len = strlen (json);

	// cerver_http_response_set_header (context, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

	(void) cerver_http_response_send_json (context, json, json_len);

}

static void iter_print_part (const CerverHttpPart *part) {

	const CerverHttpView name = cerver_http_part_name (part);

	if (cerver_http_part_is_file (part)) {
		const CerverHttpView filename = cerver_http_part_filename (part);
		const CerverHttpView saved_filename = cerver_http_part_saved_filename (part);

		(void) printf (
			"FILE: %s - [%zu] %s -> [%zu] %s\n",
			name.data,
			filename.length, filename.data,
			saved_filename.length, saved_filename.data
		);
	}

	else {
		const CerverHttpView value = cerver_http_part_value (part);

		(void) printf (
			"VALUE: %s - [%zu] %s\n",
			name.data,
			value.length, value.data
		);
	}

}

// POST /iter/good
static void iter_good_handler (CerverHttpContext *context) {

	const CerverHttpPart *part = cerver_http_request_part_next (context, NULL);
	if (part) {
		while (part) {
			iter_print_part (part);

			part = cerver_http_request_part_next (context, part);
		}

		const static char *json = { "{ \"msg\": \"Iter good route works!\" }" };
		const size_t json_len = strlen (json);

		// cerver_http_response_set_header (context, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

		(void) cerver_http_response_send_json (context, json, json_len);
	}

}

// POST /iter/empty
static void iter_empty_handler (CerverHttpContext *context) {

	if (!cerver_http_request_part_next (context, NULL)) {
		const static char *json = { "{ \"msg\": \"Iter empty route works!\" }" };
		const size_t json_len = strlen (json);

		// cerver_http_response_set_header (context, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

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
		"%d-%ld",
		cerver_http_context_socket (context),
		time (NULL)
	);

}

static void custom_uploads_filename_generator (CerverHttpContext *context) {

	const CerverHttpPart *part = cerver_http_request_current_part (context);

	cerver_http_part_set_generated_filename (
		part,
		"%d-%ld-%s",
		cerver_http_context_socket (context),
		time (NULL),
		cerver_http_part_filename (part).data
	);

}

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

	cerver_log_debug ("Uploads Web Cerver Example");
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

		// POST /discard
		CerverHttpEndpoint *discard = cerver_http_group_post (main_group, "discard", discard_handler);

		// the handler deletes the files of its request, so it runs in a thread of the pool
		cerver_http_endpoint_set_pool (discard, HTTP_POOL_DEFAULT_NAME);

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
