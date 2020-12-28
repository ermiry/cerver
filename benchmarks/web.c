#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <pthread.h>

#include <sys/time.h>

#include <cerver/version.h>

#include <cerver/utils/log.h>

#include "curl.h"

typedef enum WebBenchType {

	WEB_BENCH_TYPE_NONE		 = 0,
	WEB_BENCH_TYPE_ALL		 = 1,
	WEB_BENCH_TYPE_SINGLE	 = 2,

} WebBenchType;

static const char *address = { "localhost.com:8080" };

typedef struct Session {

	CURL *curl;
	pthread_t thread_id;

} Session;

Session *session_new (void) {

	Session *session = (Session *) malloc (sizeof (Session));
	if (session) {
		session->curl = NULL;
		session->thread_id = 0;
	}

	return session;

}

void session_delete (void *session_ptr) {

	if (session_ptr) {
		Session *session = (Session *) session_ptr;

		curl_easy_cleanup (session->curl);

		free (session_ptr);
	}

}

Session *session_create (void) {

	Session *session = session_new ();
	if (session) {
		session->curl = curl_easy_init ();
	}

	return session;

}

static size_t web_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncat ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int web_request_all_actual (
	Session *sess
) {

	unsigned int errors = 1;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	// GET /
	errors |= curl_simple_handle_data (
		sess->curl, address,
		web_request_all_data_handler, data_buffer
	);

	// GET /test
	(void) snprintf (actual_address, 128, "%s/test", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /text
	(void) snprintf (actual_address, 128, "%s/text", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /json
	(void) snprintf (actual_address, 128, "%s/json", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /hola
	(void) snprintf (actual_address, 128, "%s/hola", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /adios
	(void) snprintf (actual_address, 128, "%s/adios", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /key
	(void) snprintf (actual_address, 128, "%s/key", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /custom
	(void) snprintf (actual_address, 128, "%s/custom", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	// GET /reference
	(void) snprintf (actual_address, 128, "%s/reference", address);
	errors |= curl_simple_handle_data (
		sess->curl, actual_address,
		web_request_all_data_handler, data_buffer
	);

	return errors;

}

// perform requests to every route
static unsigned int web_request_all (void) {

	unsigned int retval = 1;

	Session *sess = session_create ();
	if (sess) {
		if (!web_request_all_actual (sess)) {
			cerver_log_success (
				"web_request_all () - All requests succeeded!"
			);

			retval = 0;
		}
	}

	session_delete (sess);

	return retval;

}

static size_t web_bench_single_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

// perform as many requests as possible to a single route
// for x seconds
static unsigned int web_bench_single_for_seconds (
	unsigned int seconds
) {

	Session *sess = session_create ();

	struct timeval start = { 0 };
	struct timeval end = { 0 };

	size_t count = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	(void) gettimeofday (&start, NULL);
	do {
		// GET /test
		(void) snprintf (actual_address, 128, "%s/test", address);
		(void) curl_simple_handle_data (
			sess->curl, actual_address,
			web_bench_single_data_handler, data_buffer
		);

		count += 1;

		(void) gettimeofday (&end, NULL);
	} while (
		((double) (end.tv_sec - start.tv_sec) +
		(end.tv_usec - start.tv_usec) * 1e-6f)
		< (double) seconds
	);

	(void) printf ("\n\nweb_bench_single_for_seconds () results: \n");
	(void) printf ("Performed %ld requests in %d seconds\n\n", count, seconds);

	session_delete (sess);

	return 0;

}

static void print_help (const char *name) {

	(void) printf ("\n");
	(void) printf ("Usage: %s [OPTIONS]\n", name);
	(void) printf ("-h                 \tPrint this help\n");
	(void) printf ("--all              \tPerform all requests\n");
	(void) printf ("--single           \tPerform a single request for x seconds\n");
	(void) printf ("\t--seconds        \tThe duration of the benchmark\n");
	(void) printf ("\n");
	(void) printf ("\n");

}

int main (int argc, const char **argv) {

	WebBenchType web_bench_type = WEB_BENCH_TYPE_NONE;

	unsigned int seconds = 1;

	cerver_log_init ();

	cerver_version_print_full ();

	if (argc > 1) {
		const char *curr_arg = NULL;
		for (int i = 1; i < argc; i++) {
			curr_arg = argv[i];

			if (!strcmp (curr_arg, "-h")) print_help (argv[0]);

			else if (!strcmp (curr_arg, "--all")) {
				web_bench_type = WEB_BENCH_TYPE_ALL;
			}

			else if (!strcmp (curr_arg, "--single")) {
				web_bench_type = WEB_BENCH_TYPE_SINGLE;

				int next = (i + 1);
				if (next < argc) {
					if (!strcmp (argv[next], "--seconds")) {
						next += 1;
						if (next < argc) {
							seconds = (unsigned int) atoi (argv[next]);
						}

						i++;
					}

					else {
						cerver_log_error (
							"Unknown single flag: %s", argv[next]
						);
					}
				}

				i++;
			}
		}

		switch (web_bench_type) {
			case WEB_BENCH_TYPE_NONE: break;

			case WEB_BENCH_TYPE_ALL:
				(void) web_request_all ();
				break;

			case WEB_BENCH_TYPE_SINGLE:
				(void) web_bench_single_for_seconds (seconds);
				break;

			default: break;
		}
	}

	else print_help (argv[0]);

	cerver_log_end ();

	return 0;

}