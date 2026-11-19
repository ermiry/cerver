#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/time.h>

#include <cerver/version.h>

#include <cerver/http/clients/client.h>
#include <cerver/http/requests/method.h>

#include <cerver/utils/log.h>

#define WEB_BENCH_URL_SIZE				128

#define WEB_REQUEST_ALL_PATHS_COUNT		(sizeof (web_request_all_paths) / sizeof (web_request_all_paths[0]))

typedef enum WebBenchType {

	WEB_BENCH_TYPE_NONE		 = 0,
	WEB_BENCH_TYPE_ALL		 = 1,
	WEB_BENCH_TYPE_SINGLE	 = 2,

} WebBenchType;

// the client takes only the urls with their scheme, curl added it by itself
static const char *address = { "http://localhost.com:8080" };

// the routes of examples/web, and three that it does not have
static const char *web_request_all_paths[] = {
	"/", "/test", "/text", "/json", "/hola", "/adios", "/key", "/custom", "/reference"
};

static const char *web_bench_single_path = { "/test" };

// one client for every request of the program: it keeps its connections between the requests,
// like the handle of curl of each session did
static CerverHttpClient *web_client = NULL;

// only an error of the request fails it, whatever the status of its response, as with curl
static unsigned int web_request_result (const CerverHttpClientRequest *request) {

	CerverHttpClientError error = cerver_http_client_response_error (
		cerver_http_client_request_response (request)
	);

	if (error != CERVER_HTTP_CLIENT_ERROR_NONE) {
		cerver_log_error (
			"web_request () failed: %s", cerver_http_client_error_description (error)
		);
	}

	return (unsigned int) (error != CERVER_HTTP_CLIENT_ERROR_NONE);

}

static unsigned int web_request_perform (CerverHttpClientRequest *request) {

	if (cerver_http_client_request_perform (request)) {
		cerver_log_error ("web_request () failed: the request was not sent");
		return 1;
	}

	return web_request_result (request);

}

// performs a GET to a path of the address
// returns 0 on success, 1 on any error
static unsigned int web_request (const char *path) {

	unsigned int retval = 1;

	char url[WEB_BENCH_URL_SIZE] = { 0 };
	(void) snprintf (url, WEB_BENCH_URL_SIZE, "%s%s", address, path);

	CerverHttpClientRequest *request = cerver_http_client_request_create (
		web_client, REQUEST_METHOD_GET, url
	);

	if (request) {
		retval = web_request_perform (request);
	}

	else {
		cerver_log_error ("web_request () failed to create the request to %s", url);
	}

	cerver_http_client_request_delete (request);

	return retval;

}

// perform requests to every route
static unsigned int web_request_all (void) {

	unsigned int errors = 0;

	for (size_t i = 0; i < WEB_REQUEST_ALL_PATHS_COUNT; i++) {
		errors |= web_request (web_request_all_paths[i]);
	}

	if (!errors) {
		cerver_log_success (
			"web_request_all () - All requests succeeded!"
		);

		cerver_log_line_break ();
		cerver_log_line_break ();
	}

	return errors;

}

// perform as many requests as possible to a single route
// for x seconds
static unsigned int web_bench_single_for_seconds (
	unsigned int seconds
) {

	struct timeval start = CERVER_ZERO_INITIALIZER;
	struct timeval end = CERVER_ZERO_INITIALIZER;

	size_t count = 0;

	(void) gettimeofday (&start, NULL);
	do {
		// GET /test
		(void) web_request (web_bench_single_path);

		count += 1;

		(void) gettimeofday (&end, NULL);
	} while (
		((double) (end.tv_sec - start.tv_sec) +
		(double) (end.tv_usec - start.tv_usec) * 1e-6)
		< (double) seconds
	);

	(void) printf ("\n\nweb_bench_single_for_seconds () results: \n");
	(void) printf ("Performed %zu requests in %u seconds\n\n", count, seconds);

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

static void web_bench_run (int argc, const char **argv) {

	WebBenchType web_bench_type = WEB_BENCH_TYPE_NONE;

	unsigned int seconds = 1;

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

}

int main (int argc, const char **argv) {

	int retval = 0;

	cerver_log_init ();

	cerver_version_print_full ();

	web_client = cerver_http_client_create (NULL);
	if (web_client) {
		web_bench_run (argc, argv);
	}

	else {
		cerver_log_error ("Failed to create the HTTP client!");
		retval = 1;
	}

	cerver_http_client_delete (web_client);

	cerver_log_end ();

	return retval;

}
