#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/version.h>
#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/files.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

Cerver *web_cerver = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

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

// GET /test
void test_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "Test route works!"
	);
	if (res) {
		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif
		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// POST /upload
void upload_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_multi_parts_print (request);

	const char *filename = http_request_multi_parts_get_filename (
		request, "file"
	);

	if (filename) (void) printf ("filename: %s\n", filename);

	const char *saved_filename = http_request_multi_parts_get_saved_filename (
		request, "file"
	);

	if (saved_filename) (void) printf ("saved filename: %s\n", saved_filename);

	DoubleList *all_filenames = http_request_multi_parts_get_all_filenames (request);
	if (all_filenames) {
		(void) printf ("\nAll filenames: \n");
		int count = 0;
		char *filename = NULL;
		for (ListElement *le = dlist_start (all_filenames); le; le = le->next) {
			filename = (char *) le->data;

			(void) printf ("[%d]: %s\n", count, filename);
			count += 1;
		}

		http_request_multi_parts_all_filenames_delete (all_filenames);
	}

	DoubleList *all_saved_filenames = http_request_multi_parts_get_all_saved_filenames (request);
	if (all_saved_filenames) {
		(void) printf ("\nAll saved filenames: \n");
		int count = 0;
		char *filename = NULL;
		for (ListElement *le = dlist_start (all_saved_filenames); le; le = le->next) {
			filename = (char *) le->data;

			(void) printf ("[%d]: %s\n", count, filename);
			count += 1;
		}

		http_request_multi_parts_all_filenames_delete (all_saved_filenames);
	}

	const static char *json = { "{ \"msg\": \"Upload route works!\" }" };
	const size_t json_len = strlen (json);

	HttpResponse *res = http_response_create (HTTP_STATUS_OK, json, json_len);
	if (res) {
		(void) http_response_add_content_type_header (res, HTTP_CONTENT_TYPE_JSON);
		(void) http_response_add_content_length_header (res, json_len);
		// http_response_add_header (res, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

		(void) http_response_compile (res);

		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif

		(void) http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// POST /multiple
void multiple_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	DoubleList *all_filenames = http_request_multi_parts_get_all_filenames (request);
	if (all_filenames) {
		(void) printf ("\nAll filenames: \n");
		int count = 0;
		char *filename = NULL;
		for (ListElement *le = dlist_start (all_filenames); le; le = le->next) {
			filename = (char *) le->data;

			(void) printf ("[%d]: %s\n", count, filename);
			count += 1;
		}

		http_request_multi_parts_all_filenames_delete (all_filenames);
	}

	DoubleList *all_saved_filenames = http_request_multi_parts_get_all_saved_filenames (request);
	if (all_saved_filenames) {
		(void) printf ("\nAll saved filenames: \n");
		int count = 0;
		char *filename = NULL;
		for (ListElement *le = dlist_start (all_saved_filenames); le; le = le->next) {
			filename = (char *) le->data;

			(void) printf ("[%d]: %s\n", count, filename);
			count += 1;
		}

		http_request_multi_parts_all_filenames_delete (all_saved_filenames);
	}

	const static char *json = { "{ \"msg\": \"Multiple route works!\" }" };
	const size_t json_len = strlen (json);

	HttpResponse *res = http_response_create (HTTP_STATUS_OK, json, json_len);
	if (res) {
		(void) http_response_add_content_type_header (res, HTTP_CONTENT_TYPE_JSON);
		(void) http_response_add_content_length_header (res, json_len);
		// http_response_add_header (res, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

		(void) http_response_compile (res);

		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif

		(void) http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// POST /iter/good
void iter_good_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (http_request_multi_parts_iter_start (request)) {
		const MultiPart *mpart = http_request_multi_parts_iter_get_next (request);
		while (mpart) {
			if (http_multi_part_is_file (mpart)) {
				(void) printf (
					"FILE: %s - [%d] %s -> [%d] %s\n",
					http_multi_part_get_name (mpart)->str,
					http_multi_part_get_filename_len (mpart),
					http_multi_part_get_filename (mpart),
					http_multi_part_get_saved_filename_len (mpart),
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

			mpart = http_request_multi_parts_iter_get_next (request);
		}

		const static char *json = { "{ \"msg\": \"Iter good route works!\" }" };
		const size_t json_len = strlen (json);

		HttpResponse *res = http_response_create (HTTP_STATUS_OK, json, json_len);
		if (res) {
			(void) http_response_add_content_type_header (res, HTTP_CONTENT_TYPE_JSON);
			(void) http_response_add_content_length_header (res, json_len);
			// http_response_add_header (res, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

			(void) http_response_compile (res);

			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif

			(void) http_response_send (res, http_receive);
			http_response_delete (res);
		}
	}

}

// POST /iter/empty
void iter_empty_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	if (!http_request_multi_parts_iter_start (request)) {
		const static char *json = { "{ \"msg\": \"Iter empty route works!\" }" };
		const size_t json_len = strlen (json);

		HttpResponse *res = http_response_create (HTTP_STATUS_OK, json, json_len);
		if (res) {
			(void) http_response_add_content_type_header (res, HTTP_CONTENT_TYPE_JSON);
			(void) http_response_add_content_length_header (res, json_len);
			// http_response_add_header (res, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

			(void) http_response_compile (res);

			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif

			(void) http_response_send (res, http_receive);
			http_response_delete (res);
		}
	}

}

// POST /discard
void discard_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_multi_parts_print (request);

	const char *key = http_request_multi_parts_get_value (request, "key");
	if (!strcmp (key, "value")) {
		cerver_log_success ("Success request, keeping multi part files...");

		HttpResponse *res = http_response_json_msg (
			HTTP_STATUS_OK, "Success request!"
		);
		if (res) {
			#ifdef EXAMPLES_DEBUG
			http_response_print (res);
			#endif
			http_response_send (res, http_receive);
			http_response_delete (res);
		}
	}

	else {
		cerver_log_error ("key != value");
		cerver_log_debug ("Discarding multi part files...");
		http_request_multi_part_discard_files (request);
		http_response_json_error_send (http_receive, HTTP_STATUS_BAD_REQUEST, "Bad request!");
	}

}

#pragma endregion

#pragma region start

void custom_uploads_filename_generator (
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
		cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

		(void) files_create_dir ("uploads", 0777);
		http_cerver_set_uploads_path (http_cerver, "uploads");

		http_cerver_set_uploads_filename_generator (
			http_cerver, custom_uploads_filename_generator
		);

		// GET /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// POST /upload
		HttpRoute *upload_route = http_route_create (REQUEST_METHOD_POST, "upload", upload_handler);
		http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, upload_route);

		// POST /multiple
		HttpRoute *multiple_route = http_route_create (REQUEST_METHOD_POST, "multiple", multiple_handler);
		http_route_set_modifier (multiple_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, multiple_route);

		// POST /iter/good
		HttpRoute *iter_good_route = http_route_create (REQUEST_METHOD_POST, "iter/good", iter_good_handler);
		http_route_set_modifier (iter_good_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, iter_good_route);

		// POST /iter/empty
		HttpRoute *iter_empty_route = http_route_create (REQUEST_METHOD_POST, "iter/empty", iter_empty_handler);
		http_route_set_modifier (iter_empty_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, iter_empty_route);

		// POST /discard
		HttpRoute *discard_route = http_route_create (REQUEST_METHOD_POST, "discard", discard_handler);
		http_route_set_modifier (discard_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, discard_route);

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
