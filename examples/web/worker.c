#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/version.h>

#include <cerver/threads/worker.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "../data.h"

static Cerver *web_cerver = NULL;
static Worker *worker = NULL;

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

	// end worker
	worker_end (worker);

	cerver_end ();

	exit (0);

}

#pragma endregion

#pragma region worker

static void worker_handler_method (void *data_ptr) {

	Data *data = (Data *) data_ptr;

	(void) printf ("Doing work with data...\n");

	// do actual work
	data_print (data);
	(void) sleep (1);

	(void) printf ("Done working with data!\n\n");

}

#pragma endregion

#pragma region routes

// GET /
void main_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	(void) http_response_json_key_value_send (
		http_receive, HTTP_STATUS_OK,
		"oki", "doki"
	);

}

// GET /work
void worker_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	const char *name = http_request_multi_parts_get_value (request, "name");
	const char *value = http_request_multi_parts_get_value (request, "value");

	if (name && value) {
		if (!worker_push_job (worker, NULL, data_create (name, value))) {
			(void) http_response_json_key_value_send (
				http_receive, HTTP_STATUS_OK,
				"oki", "doki"
			);
		}

		else {
			(void) http_response_json_error_send (
				http_receive,
				HTTP_STATUS_INTERNAL_SERVER_ERROR,
				"Failed to push to worker!"
			);
		}
	}

	else {
		(void) http_response_json_error_send (
			http_receive,
			HTTP_STATUS_BAD_REQUEST,
			"Missing values!"
		);
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

	printf ("\n");
	cerver_version_print_full ();
	printf ("\n");

	cerver_log_debug ("Worker Web Cerver Example");
	printf ("\n");

	web_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"worker-cerver",
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

		// GET /
		HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "/", main_handler);
		http_cerver_route_register (http_cerver, main_route);

		// POST /work
		HttpRoute *worker_route = http_route_create (REQUEST_METHOD_POST, "work", worker_handler);
		http_route_set_modifier (worker_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, worker_route);

		/*** worker ***/
		worker = worker_create ();
		worker_set_name (worker, "test");
		worker_set_work (worker, worker_handler_method);
		worker_set_delete_data (worker, data_delete);
		(void) worker_start (worker);

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
