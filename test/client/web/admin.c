#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/http/json/json.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define ADDRESS_SIZE			128
#define STATS_BUFFER_SIZE		(1 << 16)

static const char *address = { "127.0.0.1:8080" };

static const char *stats_counters[] = {
	"n_incompleted_requests", "n_unhandled_requests",
	"n_catch_all_requests", "n_failed_auth_requests"
};

#define STATS_COUNTERS			(sizeof (stats_counters) / sizeof (stats_counters[0]))

typedef struct StatsBuffer {

	size_t len;
	char data[STATS_BUFFER_SIZE];

} StatsBuffer;

// keeps the whole stats response, it is bigger than a single write
static size_t admin_stats_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	StatsBuffer *buffer = (StatsBuffer *) storage;

	const size_t length = size * nmemb;
	const size_t available = STATS_BUFFER_SIZE - 1 - buffer->len;
	const size_t copied = (length < available) ? length : available;

	(void) memcpy (buffer->data + buffer->len, contents, copied);
	buffer->len += copied;
	buffer->data[buffer->len] = '\0';

	return length;

}

static unsigned int admin_stats_check (const char *data, const size_t len) {

	unsigned int errors = 0;

	json_error_t error;
	json_t *json = json_loadb (data, len, 0, &error);
	if (!json) {
		cerver_log_error ("/cerver/stats is not a JSON: %s", error.text);
		return 1;
	}

	errors |= !json_is_integer (json_object_get (json, "main_routes"));

	// the routes of the first API are listed in routes, the endpoints of the groups in endpoints
	const json_t *routes = json_object_get (json, "routes");
	errors |= !json_is_array (routes) || !json_array_size (routes);
	errors |= !json_is_array (json_object_get (json, "endpoints"));

	for (size_t i = 0; i < STATS_COUNTERS; i++) {
		errors |= !json_is_integer (json_object_get (json, stats_counters[i]));
	}

	if (errors) cerver_log_error ("/cerver/stats does not have the expected values: %s", data);

	json_decref (json);

	return errors;

}

// GET /cerver/stats returns the routes, the endpoints and the counters
static unsigned int admin_request_stats (CURL *curl) {

	char actual_address[ADDRESS_SIZE] = { 0 };
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/cerver/stats", address);

	StatsBuffer *buffer = (StatsBuffer *) calloc (1, sizeof (StatsBuffer));
	if (!buffer) return 1;

	unsigned int errors = curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		admin_stats_data_handler, (char *) buffer
	);

	if (!errors) errors = admin_stats_check (buffer->data, buffer->len);

	free (buffer);

	return errors;

}

static size_t admin_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int admin_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[ADDRESS_SIZE] = { 0 };

	// GET /
	errors |= curl_simple_handle_data (
		curl, address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	// GET /cerver/info
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/cerver/info", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	// GET /cerver/stats
	errors |= admin_request_stats (curl);

	// GET /cerver/stats/filesystems
	(void) snprintf (actual_address, ADDRESS_SIZE, "%s/cerver/stats/filesystems", address);
	errors |= curl_simple_handle_data (
		curl, actual_address,
		HTTP_STATUS_OK,
		admin_request_all_data_handler, data_buffer
	);

	return errors;

}

// perform requests to every route
static unsigned int admin_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!admin_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"admin_request_all () - All requests succeeded!"
			);

			cerver_log_line_break ();
			cerver_log_line_break ();

			retval = 0;
		}
	}

	curl_easy_cleanup (curl);

	return retval;

}

int main (int argc, char **argv) {

	cerver_log_init ();

	const unsigned int errors = admin_request_all ();

	cerver_log_end ();

	return (int) errors;

}
