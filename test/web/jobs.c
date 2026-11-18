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
#include <cerver/http/core/routes.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/tree.h>

#include <cerver/threads/jobs.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "../address.h"
#include "../handler.h"

static Cerver *web_cerver = NULL;
static JobQueue *job_queue = NULL;

#pragma region end

// correctly closes any on-going server and process when quitting the appplication
void end (int dummy) {

	(void) dummy;

	if (web_cerver) (void) cerver_shutdown (web_cerver);

}

static void jobs_finish (void) {

	cerver_stats_print (web_cerver, false, false);
	cerver_log_msg ("\nHTTP Cerver stats:\n");
	cerver_http_all_stats_print (cerver_http_get (web_cerver));
	cerver_log_line_break ();
	(void) cerver_teardown (web_cerver);

	(void) job_queue_stop (job_queue);
	job_queue_delete (job_queue);

}

#pragma endregion

#pragma region handler

static void custom_handler_method (void *data_ptr) {

	cerver_log_debug ("custom_handler_method ()!");
	cerver_log_msg ("Value: %s", ((const char *) data_ptr));

	(void) sleep (1);

}

#pragma endregion

#pragma region routes

// GET /
void main_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Test route works!"
	);

}

// GET /jobs
void jobs_handler (CerverHttpContext *context) {

	const CerverHttpView value = cerver_http_request_part_value (context, "value");

	job_handler_wait (
		job_queue, (void *) value.data, NULL
	);

	(void) cerver_http_response_send_json_message (
		context, HTTP_STATUS_OK, "Jobs route works!"
	);

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
		"jobs-cerver",
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

		CerverHttpGroup *main_group = cerver_http_group_create (http_cerver, "/");

		// GET /
		(void) cerver_http_group_root_add (main_group, REQUEST_METHOD_GET, main_handler);

		// GET /jobs
		CerverHttpEndpoint *jobs = cerver_http_group_get (main_group, "jobs", jobs_handler);
		cerver_http_endpoint_set_modifier (jobs, HTTP_ROUTE_MODIFIER_MULTI_PART);

		// its handler waits for its job: it runs in a thread
		cerver_http_endpoint_set_pool (jobs, HTTP_POOL_DEFAULT_NAME);

		/*** job queue ***/
		job_queue = job_queue_create (JOB_QUEUE_TYPE_HANDLERS);
		job_queue_set_handler (job_queue, custom_handler_method);
		job_queue_start (job_queue);

		if (!cerver_start (web_cerver)) {
			jobs_finish ();
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
