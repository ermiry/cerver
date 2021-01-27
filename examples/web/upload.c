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
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (
		(http_status) 200, "Test route works!"
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
	const struct _HttpReceive *http_receive,
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

	char content_len[32] = { 0 };
	(void) snprintf (content_len, 32, "%ld", strlen (json));

	HttpResponse *res = http_response_create (HTTP_STATUS_OK, json, strlen (json));
	if (res) {
		http_response_add_header (res, HTTP_HEADER_CONTENT_TYPE, "application/json");
		http_response_add_header (res, HTTP_HEADER_CONTENT_LENGTH, content_len);
		// http_response_add_header (res, HTTP_HEADER_ACCESS_CONTROL_ALLOW_ORIGIN, "*");

		http_response_compile (res);

		#ifdef EXAMPLES_DEBUG
		http_response_print (res);
		#endif

		http_response_send (res, http_receive);
		http_response_delete (res);
	}

}

// POST /discard
void discard_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_multi_parts_print (request);

	const String *key = http_request_multi_parts_get_value (request, "key");
	if (!strcmp (key->str, "value")) {
		cerver_log_success ("Success request, keeping multi part files...");

		HttpResponse *res = http_response_json_msg (
			(http_status) 200, "Success request!"
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
		http_response_json_error_send (http_receive, 400, "Bad request!");
	}

}

#pragma endregion

#pragma region start

void custom_uploads_filename_generator (
	const CerverReceive *cr,
	const char *original_filename,
	char *generated_filename
) {

	(void) snprintf (
		generated_filename, HTTP_MULTI_PART_GENERATED_FILENAME_LEN,
		"%d-%ld-%s",
		cr->connection->socket->sock_fd,
		time (NULL),
		original_filename
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

		// char filename[256] = { "\n p2'\nr-o@g_ram \t84iz./te{st_20191\n11814}233\n3030>.png\n" };
		// (void) printf ("\nTesting filename sanitize method: \n");
		// files_sanitize_filename (filename);
		// (void) printf ("Filename: <%s>\n\n", filename);

		// GET /test
		HttpRoute *test_route = http_route_create (REQUEST_METHOD_GET, "test", test_handler);
		http_cerver_route_register (http_cerver, test_route);

		// POST /upload
		HttpRoute *upload_route = http_route_create (REQUEST_METHOD_POST, "upload", upload_handler);
		http_route_set_modifier (upload_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, upload_route);

		// POST /discard
		HttpRoute *discard_route = http_route_create (REQUEST_METHOD_POST, "discard", discard_handler);
		http_cerver_route_register (http_cerver, discard_route);

		if (cerver_start (web_cerver)) {
			cerver_log_error (
				"Failed to start %s!",
				web_cerver->info->name->str
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