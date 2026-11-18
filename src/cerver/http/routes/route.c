#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler/handler.h"

#include "cerver/http/core/http.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"

#include "cerver/threads/thread.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

const char *cerver_http_route_modifier_to_string (
	const CerverHttpRouteModifier modifier
) {

	switch (modifier) {
		#define XX(num, name, string, description) case HTTP_ROUTE_MODIFIER_##name: return #string;
		HTTP_ROUTE_MODIFIER_MAP(XX)
		#undef XX
	}

	return cerver_http_route_modifier_to_string (HTTP_ROUTE_MODIFIER_NONE);

}

const char *cerver_http_route_modifier_description (
	const CerverHttpRouteModifier modifier
) {

	switch (modifier) {
		#define XX(num, name, string, description) case HTTP_ROUTE_MODIFIER_##name: return #description;
		HTTP_ROUTE_MODIFIER_MAP(XX)
		#undef XX
	}

	return cerver_http_route_modifier_description (HTTP_ROUTE_MODIFIER_NONE);

}

const char *cerver_http_route_auth_type_to_string (
	const CerverHttpRouteAuthType type
) {

	switch (type) {
		#define XX(num, name, string, description) case HTTP_ROUTE_AUTH_TYPE_##name: return #string;
		HTTP_ROUTE_AUTH_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_http_route_auth_type_to_string (HTTP_ROUTE_AUTH_TYPE_NONE);

}

const char *cerver_http_route_auth_type_description (
	const CerverHttpRouteAuthType type
) {

	switch (type) {
		#define XX(num, name, string, description) case HTTP_ROUTE_AUTH_TYPE_##name: return #description;
		HTTP_ROUTE_AUTH_TYPE_MAP(XX)
		#undef XX
	}

	return cerver_http_route_auth_type_description (HTTP_ROUTE_AUTH_TYPE_NONE);

}

static const char *http_route_methods_separator = { ", " };

#pragma region stats

// the generic atomic builtins are used for doubles, the _n ones only take integers
static double cerver_http_route_stats_double_load (const double *value) {

	double current = 0;
	__atomic_load (value, &current, __ATOMIC_RELAXED);

	return current;

}

// the doubles are updated with compare and swap, there is no atomic add for them
static void cerver_http_route_stats_double_add (double *value, const double amount) {

	double current = cerver_http_route_stats_double_load (value);
	double updated = current + amount;
	while (!__atomic_compare_exchange (
		value, &current, &updated, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	)) {
		updated = current + amount;
	}

}

static void cerver_http_route_stats_double_min (double *value, const double candidate) {

	double desired = candidate;
	double current = cerver_http_route_stats_double_load (value);
	while ((desired < current) && !__atomic_compare_exchange (
		value, &current, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static void cerver_http_route_stats_double_max (double *value, const double candidate) {

	double desired = candidate;
	double current = cerver_http_route_stats_double_load (value);
	while ((desired > current) && !__atomic_compare_exchange (
		value, &current, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static void cerver_http_route_stats_size_min (size_t *value, const size_t candidate) {

	size_t current = __atomic_load_n (value, __ATOMIC_RELAXED);
	while ((candidate < current) && !__atomic_compare_exchange_n (
		value, &current, candidate, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static void cerver_http_route_stats_size_max (size_t *value, const size_t candidate) {

	size_t current = __atomic_load_n (value, __ATOMIC_RELAXED);
	while ((candidate > current) && !__atomic_compare_exchange_n (
		value, &current, candidate, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static const double cerver_http_route_stats_buckets[HTTP_ROUTE_STATS_DURATION_BUCKETS] = {
	0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10
};

const double *cerver_http_route_stats_duration_buckets (void) {

	return cerver_http_route_stats_buckets;

}

// counts the process time in the first bucket that it fits in
static void cerver_http_route_stats_duration_update (
	HttpRouteStats *route_stats, const double process_time
) {

	for (unsigned int i = 0; i < HTTP_ROUTE_STATS_DURATION_BUCKETS; i++) {
		if (process_time <= cerver_http_route_stats_buckets[i]) {
			(void) __atomic_add_fetch (&route_stats->duration_buckets[i], 1, __ATOMIC_RELAXED);
			break;
		}
	}

}

HttpRouteStats *cerver_http_route_stats_new (void) {

	HttpRouteStats *route_stats = (HttpRouteStats *) malloc (sizeof (HttpRouteStats));
	if (route_stats) {
		(void) memset (route_stats, 0, sizeof (HttpRouteStats));

		route_stats->first = true;

		route_stats->min_process_time = __DBL_MAX__;

		route_stats->min_request_size = SIZE_MAX;

		route_stats->min_response_size = SIZE_MAX;
	}

	return route_stats;

}

void cerver_http_route_stats_delete (void *route_stats_ptr) {

	if (route_stats_ptr) {
		free (route_stats_ptr);
	}

}

// adds one request to counter
// updates process times & request & response sizes
void cerver_http_route_stats_update (
	HttpRouteStats *route_stats,
	const double process_time,
	const size_t request_size, const size_t response_size
) {

	if (route_stats) {
		__atomic_store_n (&route_stats->first, false, __ATOMIC_RELAXED);

		(void) __atomic_add_fetch (&route_stats->n_requests, 1, __ATOMIC_RELAXED);

		cerver_http_route_stats_double_min (&route_stats->min_process_time, process_time);
		cerver_http_route_stats_double_max (&route_stats->max_process_time, process_time);
		cerver_http_route_stats_double_add (&route_stats->sum_process_times, process_time);
		cerver_http_route_stats_duration_update (route_stats, process_time);

		cerver_http_route_stats_size_min (&route_stats->min_request_size, request_size);
		cerver_http_route_stats_size_max (&route_stats->max_request_size, request_size);
		(void) __atomic_add_fetch (&route_stats->sum_request_sizes, request_size, __ATOMIC_RELAXED);

		cerver_http_route_stats_size_min (&route_stats->min_response_size, response_size);
		cerver_http_route_stats_size_max (&route_stats->max_response_size, response_size);
		(void) __atomic_add_fetch (&route_stats->sum_response_sizes, response_size, __ATOMIC_RELAXED);
	}

}

// copies the current values of the stats, the means are computed from the sums
void cerver_http_route_stats_snapshot (
	const HttpRouteStats *route_stats, HttpRouteStats *snapshot
) {

	if (route_stats && snapshot) {
		snapshot->first = __atomic_load_n (&route_stats->first, __ATOMIC_RELAXED);
		snapshot->n_requests = __atomic_load_n (&route_stats->n_requests, __ATOMIC_RELAXED);

		snapshot->min_process_time = cerver_http_route_stats_double_load (&route_stats->min_process_time);
		snapshot->max_process_time = cerver_http_route_stats_double_load (&route_stats->max_process_time);
		snapshot->sum_process_times = cerver_http_route_stats_double_load (&route_stats->sum_process_times);

		snapshot->min_request_size = __atomic_load_n (&route_stats->min_request_size, __ATOMIC_RELAXED);
		snapshot->max_request_size = __atomic_load_n (&route_stats->max_request_size, __ATOMIC_RELAXED);
		snapshot->sum_request_sizes = __atomic_load_n (&route_stats->sum_request_sizes, __ATOMIC_RELAXED);

		snapshot->min_response_size = __atomic_load_n (&route_stats->min_response_size, __ATOMIC_RELAXED);
		snapshot->max_response_size = __atomic_load_n (&route_stats->max_response_size, __ATOMIC_RELAXED);
		snapshot->sum_response_sizes = __atomic_load_n (&route_stats->sum_response_sizes, __ATOMIC_RELAXED);

		for (unsigned int i = 0; i < HTTP_ROUTE_STATS_DURATION_BUCKETS; i++) {
			snapshot->duration_buckets[i] = __atomic_load_n (&route_stats->duration_buckets[i], __ATOMIC_RELAXED);
		}

		const size_t n_requests = snapshot->n_requests;
		snapshot->mean_process_time = n_requests ? snapshot->sum_process_times / (double) n_requests : 0;
		snapshot->mean_request_size = n_requests ? snapshot->sum_request_sizes / n_requests : 0;
		snapshot->mean_response_size = n_requests ? snapshot->sum_response_sizes / n_requests : 0;

		// a route without requests shows zeros instead of the initial minimums
		if (!n_requests) {
			snapshot->min_process_time = 0;
			snapshot->min_request_size = 0;
			snapshot->min_response_size = 0;
		}
	}

}

void cerver_http_route_file_stats_init (HttpRouteFileStats *route_file_stats) {

	(void) memset (route_file_stats, 0, sizeof (HttpRouteFileStats));

	route_file_stats->first = true;

	route_file_stats->min_n_files = SIZE_MAX;

	route_file_stats->min_file_size = SIZE_MAX;

	route_file_stats->mutex = NULL;

}

HttpRouteFileStats *cerver_http_route_file_stats_new (void) {

	HttpRouteFileStats *route_file_stats = (HttpRouteFileStats *) malloc (sizeof (HttpRouteFileStats));
	if (route_file_stats) {
		cerver_http_route_file_stats_init (route_file_stats);
	}

	return route_file_stats;

}

void cerver_http_route_file_stats_delete (void *route_file_stats_ptr) {

	if (route_file_stats_ptr) {
		HttpRouteFileStats *route_file_stats = (HttpRouteFileStats *) route_file_stats_ptr;

		thread_mutex_delete (route_file_stats->mutex);

		free (route_file_stats_ptr);
	}

}

HttpRouteFileStats *cerver_http_route_file_stats_create (void) {

	HttpRouteFileStats *route_file_stats = cerver_http_route_file_stats_new ();
	if (route_file_stats) {
		route_file_stats->mutex = thread_mutex_new ();
	}

	return route_file_stats;

}

// updates route's file stats with http receive file stats
void cerver_http_route_file_stats_update (
	HttpRouteFileStats *file_stats,
	const HttpRouteFileStats *new_file_stats
) {

	if (file_stats && new_file_stats) {
		(void) pthread_mutex_lock (file_stats->mutex);

		if (file_stats->first) file_stats->first = false;

		file_stats->n_requests += 1;

		file_stats->n_uploaded_files += new_file_stats->n_uploaded_files;

		// n files
		if (new_file_stats->n_uploaded_files < file_stats->min_n_files)
			file_stats->min_n_files = new_file_stats->n_uploaded_files;

		if (new_file_stats->n_uploaded_files > file_stats->max_n_files)
			file_stats->max_n_files = new_file_stats->n_uploaded_files;

		file_stats->sum_n_files += new_file_stats->n_uploaded_files;
		file_stats->mean_n_files = (
			(double) file_stats->sum_n_files / (double) file_stats->n_requests
		);

		// size
		if (new_file_stats->min_file_size < file_stats->min_file_size)
			file_stats->min_file_size = new_file_stats->min_file_size;

		if (new_file_stats->max_file_size > file_stats->max_file_size)
			file_stats->max_file_size = new_file_stats->max_file_size;

		file_stats->sum_file_size += new_file_stats->sum_file_size;
		file_stats->mean_file_size = file_stats->n_uploaded_files ? (
			(double) file_stats->sum_file_size / (double) file_stats->n_uploaded_files
		) : 0;

		(void) pthread_mutex_unlock (file_stats->mutex);
	}

}

// copies the current values of the file stats
void cerver_http_route_file_stats_snapshot (
	const HttpRouteFileStats *file_stats, HttpRouteFileStats *snapshot
) {

	if (file_stats && snapshot) {
		(void) pthread_mutex_lock (file_stats->mutex);
		*snapshot = *file_stats;
		(void) pthread_mutex_unlock (file_stats->mutex);

		snapshot->mutex = NULL;

		if (!snapshot->n_requests) {
			snapshot->min_n_files = 0;
			snapshot->min_file_size = 0;
		}
	}

}

#pragma endregion

#pragma region methods

// writes in buffer the methods with a handler, separated with ", "
// returns the length of the string
size_t http_route_methods_string (
	HttpHandler const *handlers, char *buffer, const size_t buffer_size
) {

	size_t length = 0;

	if (buffer && buffer_size) {
		buffer[0] = '\0';

		for (unsigned int method = 0; method < HTTP_HANDLERS_COUNT; method++) {
			if (!handlers[method]) continue;

			const int written = snprintf (
				buffer + length, buffer_size - length, "%s%s",
				length ? http_route_methods_separator : "",
				cerver_http_method_to_string ((CerverHttpMethod) method)
			);

			if ((written < 0) || ((size_t) written >= (buffer_size - length))) break;

			length += (size_t) written;
		}
	}

	return length;

}

#pragma endregion
