#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/files.h>
#include <cerver/handler.h>
#include <cerver/version.h>

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

// POST /test
static void test_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	bool result = false;
	bool not_empty = true;

	// cerver_log_warning ("Count: %ld", request->multi_parts->size);

	if (http_request_multi_parts_iter_start (request)) {
		const MultiPart *mpart = http_request_multi_parts_iter_get_next (request);
		while (mpart) {
			if (http_multi_part_is_not_empty (mpart)) {
				if (http_multi_part_is_file (mpart)) {
					(void) printf (
						"FILE: %s - [%d] %s -> [%d] %s\n",
						http_multi_part_get_name (mpart)->str,
						http_multi_part_get_filename_len (mpart),
						http_multi_part_get_filename (mpart),
						http_multi_part_get_generated_filename_len (mpart),
						http_multi_part_get_saved_filename (mpart)
					);
				}

				else if (http_multi_part_is_value (mpart)) {
					(void) printf (
						"VALUE: %s - [%d] %s\n",
						http_multi_part_get_name (mpart)->str,
						http_multi_part_get_value_len (mpart),
						http_multi_part_get_value (mpart)
					);
				}
			}

			else {
				cerver_log_error ("mpart is empty!");
				not_empty = false;
			}

			mpart = http_request_multi_parts_iter_get_next (request);
		}

		result = true;
	}

	if (result && not_empty) {
		(void) http_response_json_bool_value_send (
			http_receive, HTTP_STATUS_OK, "result", true
		);
	}

	else {
		(void) http_response_json_bool_value_send (
			http_receive, HTTP_STATUS_BAD_REQUEST, "result", false
		);
	}

	http_request_multi_part_discard_files (request);

}

#pragma endregion

#pragma region start

static void custom_uploads_dirname_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_set_dirname (
		request,
		"%d-%ld",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL)
	);

}

static void custom_uploads_filename_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const MultiPart *mpart = http_request_get_current_mpart (request);

	http_multi_part_set_generated_filename (
		mpart,
		"%d-%ld-%s",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL),
		http_multi_part_get_filename (mpart)
	);

}

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

		(void) files_create_dir ("uploads", 0777);
		http_cerver_set_uploads_path (http_cerver, "uploads");

		http_cerver_set_uploads_filename_generator (
			http_cerver, custom_uploads_filename_generator
		);

		http_cerver_set_uploads_dirname_generator (
			http_cerver, custom_uploads_dirname_generator
		);

		// POST /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_POST, "test", test_handler);
		http_route_set_modifier (test_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, test_route);

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
