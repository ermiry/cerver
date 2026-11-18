#include <signal.h>
#include <stdbool.h>
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

static void multi_finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

}

#pragma endregion

#pragma region routes

static const char *multi_view_text (const CerverHttpView view) {

	return cerver_http_view_is_missing (view) ? "" : view.data;

}

// a part is not empty when it has a name, or a file with its names, or a value
static bool multi_part_is_not_empty (const CerverHttpPart *part) {

	bool result = (cerver_http_part_name (part).length > 0);

	if (cerver_http_part_is_file (part)) {
		result |= (cerver_http_part_filename (part).length > 0);
		result |= (cerver_http_part_saved_filename (part).length > 0);
	}

	else {
		result |= (cerver_http_part_value (part).length > 0);
	}

	return result;

}

static void multi_part_print (const CerverHttpPart *part) {

	if (cerver_http_part_is_file (part)) {
		const CerverHttpView filename = cerver_http_part_filename (part);
		const CerverHttpView saved_filename = cerver_http_part_saved_filename (part);

		(void) printf (
			"FILE: %s - [%zu] %s -> [%zu] %s\n",
			multi_view_text (cerver_http_part_name (part)),
			filename.length, multi_view_text (filename),
			saved_filename.length, multi_view_text (saved_filename)
		);
	}

	else {
		const CerverHttpView value = cerver_http_part_value (part);

		(void) printf (
			"VALUE: %s - [%zu] %s\n",
			multi_view_text (cerver_http_part_name (part)),
			value.length, multi_view_text (value)
		);
	}

}

// POST /test
static void test_handler (CerverHttpContext *context) {

	// a request without parts is a bad request
	const bool result = (cerver_http_request_part_next (context, NULL) != NULL);
	bool not_empty = true;

	const CerverHttpPart *cerver_part = cerver_http_request_part (context, "cerver");
	if (cerver_part) {
		cerver_log_success ("Got cerver!");
	}

	else {
		cerver_log_error ("Failed to get cerver!");
	}

	for (
		const CerverHttpPart *part = cerver_http_request_part_next (context, NULL);
		part;
		part = cerver_http_request_part_next (context, part)
	) {
		if (multi_part_is_not_empty (part)) {
			multi_part_print (part);
		}

		else {
			cerver_log_error ("part is empty!");
			not_empty = false;
		}
	}

	if (result && not_empty) {
		(void) cerver_http_response_send_json_boolean (
			context, HTTP_STATUS_OK, "result", true
		);
	}

	else {
		(void) cerver_http_response_send_json_boolean (
			context, HTTP_STATUS_BAD_REQUEST, "result", false
		);
	}

	cerver_http_request_discard_files (context);

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
		multi_view_text (cerver_http_part_filename (part))
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

		// POST /test
		CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");

		CerverHttpEndpoint *test = cerver_http_group_post (main_group, "test", test_handler);
		cerver_http_endpoint_set_modifier (test, HTTP_ROUTE_MODIFIER_MULTI_PART);

		// its handler deletes the files of the request: it runs in a thread
		cerver_http_endpoint_set_pool (test, HTTP_POOL_DEFAULT_NAME);

		if (!cerver_start (web_cerver)) {
			multi_finish ();
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
