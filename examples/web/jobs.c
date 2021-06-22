#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <time.h>
#include <signal.h>

#include <cerver/cerver.h>
#include <cerver/handler.h>
#include <cerver/version.h>

#include <cerver/threads/jobs.h>

#include <cerver/http/http.h>
#include <cerver/http/route.h>
#include <cerver/http/request.h>
#include <cerver/http/response.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

static Cerver *web_cerver = NULL;
static JobQueue *job_queue = NULL;

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

	job_queue_stop (job_queue);
	(void) sleep (1);
	job_queue_delete (job_queue);

	cerver_end ();

	exit (0);

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
void main_handler (
	const struct _HttpReceive *http_receive,
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

// GET /jobs
void jobs_handler (
	const struct _HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_multi_parts_print (request);

	const char *value = http_request_multi_parts_get_value (request, "value");

	job_handler_wait (
		job_queue, (void *) value, NULL
	);

	HttpResponse *res = http_response_json_msg (
		HTTP_STATUS_OK, "Jobs route works!"
	);

	if (res) {
		http_response_send (res, http_receive);
		http_response_delete (res);
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

	cerver_log_debug ("Jobs Web Cerver Example");
	printf ("\n");

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
		cerver_set_receive_buffer_size (web_cerver, 4096);
		cerver_set_thpool_n_threads (web_cerver, 4);
		cerver_set_handler_type (web_cerver, CERVER_HANDLER_TYPE_THREADS);

		cerver_set_reusable_address_flags (web_cerver, true);

		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) web_cerver->cerver_data;

		// GET /
		HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "/", main_handler);
		http_cerver_route_register (http_cerver, main_route);

		// GET /jobs
		HttpRoute *jobs_route = http_route_create (REQUEST_METHOD_GET, "jobs", jobs_handler);
		http_route_set_modifier (jobs_route, HTTP_ROUTE_MODIFIER_MULTI_PART);
		http_cerver_route_register (http_cerver, jobs_route);

		/*** job queue ***/
		job_queue = job_queue_create (JOB_QUEUE_TYPE_HANDLERS);
		job_queue_set_handler (job_queue, custom_handler_method);
		job_queue_start (job_queue);

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