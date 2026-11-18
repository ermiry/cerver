#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/threads/worker.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "../address.h"
#include "../handler.h"
#include <app/data.h>

static Cerver *web_cerver = NULL;
static Worker *worker = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

static void worker_finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

	worker_end (worker);

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
void main_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_key_value (
		context, HTTP_STATUS_OK,
		"oki", "doki"
	);

}

// POST /work
void worker_handler (CerverHttpContext *context) {

	const CerverHttpView name = cerver_http_request_part_value (context, "name");
	const CerverHttpView value = cerver_http_request_part_value (context, "value");

	if (!cerver_http_view_is_missing (name) && !cerver_http_view_is_missing (value)) {
		if (!worker_push_job (worker, data_create (name.data, value.data))) {
			(void) cerver_http_response_send_json_key_value (
				context, HTTP_STATUS_OK,
				"oki", "doki"
			);
		}

		else {
			(void) cerver_http_response_send_json_error (
				context,
				HTTP_STATUS_INTERNAL_SERVER_ERROR,
				"Failed to push to worker!"
			);
		}
	}

	else {
		(void) cerver_http_response_send_json_error (
			context,
			HTTP_STATUS_BAD_REQUEST,
			"Missing values!"
		);
	}

}

// GET /work/start
void worker_start_handler (CerverHttpContext *context) {

	if (!worker_resume (worker)) {
		(void) cerver_http_response_send_json_key_value (
			context, HTTP_STATUS_OK,
			"oki", "doki"
		);
	}

	else {
		(void) cerver_http_response_send_json_error (
			context,
			HTTP_STATUS_INTERNAL_SERVER_ERROR,
			"Worker is running!"
		);
	}

}

// GET /work/stop
void worker_stop_handler (CerverHttpContext *context) {

	if (!worker_stop (worker)) {
		(void) cerver_http_response_send_json_key_value (
			context, HTTP_STATUS_OK,
			"oki", "doki"
		);
	}

	else {
		(void) cerver_http_response_send_json_error (
			context,
			HTTP_STATUS_INTERNAL_SERVER_ERROR,
			"Worker is NOT running!"
		);
	}

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

	cerver_version_print_full ();

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
		(void) cerver_set_bind_address (web_cerver, test_address ());
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_events_backend (web_cerver, test_events_backend ());
		cerver_set_io_threads (web_cerver, test_io_threads ());

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		CerverHttp *http_cerver = cerver_http_get (web_cerver);

		cerver_http_enable_admin_routes (http_cerver, true);

		// the admin routes have their own group in the root: GET /cerver/stats/workers
		CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");

		// GET /
		(void) cerver_http_group_root_add (main_group, REQUEST_METHOD_GET, main_handler);

		// POST /work
		CerverHttpEndpoint *work = cerver_http_group_post (main_group, "work", worker_handler);
		cerver_http_endpoint_set_modifier (work, HTTP_ROUTE_MODIFIER_MULTI_PART);

		// GET /work/start
		(void) cerver_http_group_get (main_group, "work/start", worker_start_handler);

		// GET /work/stop, its handler waits for the job of the worker: it runs in a thread
		CerverHttpEndpoint *work_stop = cerver_http_group_get (main_group, "work/stop", worker_stop_handler);
		cerver_http_endpoint_set_pool (work_stop, HTTP_POOL_DEFAULT_NAME);

		/*** worker ***/
		worker = worker_create ();
		worker_set_name (worker, "test");
		worker_set_work (worker, worker_handler_method);
		worker_set_delete_data (worker, data_delete);
		(void) worker_start (worker);

		// register worker to be handled by admin routes
		cerver_http_register_admin_worker (http_cerver, worker);

		if (!cerver_start (web_cerver)) {
			worker_finish ();
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
