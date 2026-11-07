#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <limits.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/threads/thread.h"

#include "cerver/http/auth.h"
#include "cerver/http/http.h"
#include "cerver/http/route.h"
#include "cerver/http/request.h"

#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

const char *http_route_modifier_to_string (
	const HttpRouteModifier modifier
) {

	switch (modifier) {
		#define XX(num, name, string, description) case HTTP_ROUTE_MODIFIER_##name: return #string;
		HTTP_ROUTE_MODIFIER_MAP(XX)
		#undef XX
	}

	return http_route_modifier_to_string (HTTP_ROUTE_MODIFIER_NONE);

}

const char *http_route_modifier_description (
	const HttpRouteModifier modifier
) {

	switch (modifier) {
		#define XX(num, name, string, description) case HTTP_ROUTE_MODIFIER_##name: return #description;
		HTTP_ROUTE_MODIFIER_MAP(XX)
		#undef XX
	}

	return http_route_modifier_description (HTTP_ROUTE_MODIFIER_NONE);

}

const char *http_route_auth_type_to_string (
	const HttpRouteAuthType type
) {

	switch (type) {
		#define XX(num, name, string, description) case HTTP_ROUTE_AUTH_TYPE_##name: return #string;
		HTTP_ROUTE_AUTH_TYPE_MAP(XX)
		#undef XX
	}

	return http_route_auth_type_to_string (HTTP_ROUTE_AUTH_TYPE_NONE);

}

const char *http_route_auth_type_description (
	const HttpRouteAuthType type
) {

	switch (type) {
		#define XX(num, name, string, description) case HTTP_ROUTE_AUTH_TYPE_##name: return #description;
		HTTP_ROUTE_AUTH_TYPE_MAP(XX)
		#undef XX
	}

	return http_route_auth_type_description (HTTP_ROUTE_AUTH_TYPE_NONE);

}

static const char *http_route_methods_separator = { ", " };

#pragma region stats

// the generic atomic builtins are used for doubles, the _n ones only take integers
static double http_route_stats_double_load (const double *value) {

	double current = 0;
	__atomic_load (value, &current, __ATOMIC_RELAXED);

	return current;

}

// the doubles are updated with compare and swap, there is no atomic add for them
static void http_route_stats_double_add (double *value, const double amount) {

	double current = http_route_stats_double_load (value);
	double updated = current + amount;
	while (!__atomic_compare_exchange (
		value, &current, &updated, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	)) {
		updated = current + amount;
	}

}

static void http_route_stats_double_min (double *value, const double candidate) {

	double desired = candidate;
	double current = http_route_stats_double_load (value);
	while ((desired < current) && !__atomic_compare_exchange (
		value, &current, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static void http_route_stats_double_max (double *value, const double candidate) {

	double desired = candidate;
	double current = http_route_stats_double_load (value);
	while ((desired > current) && !__atomic_compare_exchange (
		value, &current, &desired, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static void http_route_stats_size_min (size_t *value, const size_t candidate) {

	size_t current = __atomic_load_n (value, __ATOMIC_RELAXED);
	while ((candidate < current) && !__atomic_compare_exchange_n (
		value, &current, candidate, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static void http_route_stats_size_max (size_t *value, const size_t candidate) {

	size_t current = __atomic_load_n (value, __ATOMIC_RELAXED);
	while ((candidate > current) && !__atomic_compare_exchange_n (
		value, &current, candidate, true, __ATOMIC_RELAXED, __ATOMIC_RELAXED
	));

}

static const double http_route_stats_buckets[HTTP_ROUTE_STATS_DURATION_BUCKETS] = {
	0.005, 0.01, 0.025, 0.05, 0.1, 0.25, 0.5, 1, 2.5, 5, 10
};

const double *http_route_stats_duration_buckets (void) {

	return http_route_stats_buckets;

}

// counts the process time in the first bucket that it fits in
static void http_route_stats_duration_update (
	HttpRouteStats *route_stats, const double process_time
) {

	for (unsigned int i = 0; i < HTTP_ROUTE_STATS_DURATION_BUCKETS; i++) {
		if (process_time <= http_route_stats_buckets[i]) {
			(void) __atomic_add_fetch (&route_stats->duration_buckets[i], 1, __ATOMIC_RELAXED);
			break;
		}
	}

}

HttpRouteStats *http_route_stats_new (void) {

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

void http_route_stats_delete (void *route_stats_ptr) {

	if (route_stats_ptr) {
		free (route_stats_ptr);
	}

}

// adds one request to counter
// updates process times & request & response sizes
void http_route_stats_update (
	HttpRouteStats *route_stats,
	const double process_time,
	const size_t request_size, const size_t response_size
) {

	if (route_stats) {
		__atomic_store_n (&route_stats->first, false, __ATOMIC_RELAXED);

		(void) __atomic_add_fetch (&route_stats->n_requests, 1, __ATOMIC_RELAXED);

		http_route_stats_double_min (&route_stats->min_process_time, process_time);
		http_route_stats_double_max (&route_stats->max_process_time, process_time);
		http_route_stats_double_add (&route_stats->sum_process_times, process_time);
		http_route_stats_duration_update (route_stats, process_time);

		http_route_stats_size_min (&route_stats->min_request_size, request_size);
		http_route_stats_size_max (&route_stats->max_request_size, request_size);
		(void) __atomic_add_fetch (&route_stats->sum_request_sizes, request_size, __ATOMIC_RELAXED);

		http_route_stats_size_min (&route_stats->min_response_size, response_size);
		http_route_stats_size_max (&route_stats->max_response_size, response_size);
		(void) __atomic_add_fetch (&route_stats->sum_response_sizes, response_size, __ATOMIC_RELAXED);
	}

}

// copies the current values of the stats, the means are computed from the sums
void http_route_stats_snapshot (
	const HttpRouteStats *route_stats, HttpRouteStats *snapshot
) {

	if (route_stats && snapshot) {
		snapshot->first = __atomic_load_n (&route_stats->first, __ATOMIC_RELAXED);
		snapshot->n_requests = __atomic_load_n (&route_stats->n_requests, __ATOMIC_RELAXED);

		snapshot->min_process_time = http_route_stats_double_load (&route_stats->min_process_time);
		snapshot->max_process_time = http_route_stats_double_load (&route_stats->max_process_time);
		snapshot->sum_process_times = http_route_stats_double_load (&route_stats->sum_process_times);

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

HttpRouteFileStats *http_route_file_stats_new (void) {

	HttpRouteFileStats *route_file_stats = (HttpRouteFileStats *) malloc (sizeof (HttpRouteFileStats));
	if (route_file_stats) {
		(void) memset (route_file_stats, 0, sizeof (HttpRouteFileStats));

		route_file_stats->first = true;

		route_file_stats->min_n_files = SIZE_MAX;

		route_file_stats->min_file_size = SIZE_MAX;

		route_file_stats->mutex = NULL;
	}

	return route_file_stats;

}

void http_route_file_stats_delete (void *route_file_stats_ptr) {

	if (route_file_stats_ptr) {
		HttpRouteFileStats *route_file_stats = (HttpRouteFileStats *) route_file_stats_ptr;

		thread_mutex_delete (route_file_stats->mutex);

		free (route_file_stats_ptr);
	}

}

HttpRouteFileStats *http_route_file_stats_create (void) {

	HttpRouteFileStats *route_file_stats = http_route_file_stats_new ();
	if (route_file_stats) {
		route_file_stats->mutex = thread_mutex_new ();
	}

	return route_file_stats;

}

// updates route's file stats with http receive file stats
void http_route_file_stats_update (
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
void http_route_file_stats_snapshot (
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

#pragma region route

HttpRoute *http_route_new (void) {

	HttpRoute *route = (HttpRoute *) malloc (sizeof (HttpRoute));
	if (route) {
		route->base = NULL;
		route->actual = NULL;
		route->route = NULL;

		route->children = NULL;

		route->modifier = HTTP_ROUTE_MODIFIER_NONE;

		route->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;

		route->decode_data = NULL;
		route->delete_decoded_data = NULL;

		route->authentication_handler = NULL;

		route->custom_data = NULL;
		route->delete_custom_data = NULL;

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			route->handlers[i] = NULL;
			route->stats[i] = NULL;
		}

		route->file_stats = NULL;

		route->authentication_with_body = false;
	}

	return route;

}

void http_route_delete (void *route_ptr) {

	if (route_ptr) {
		HttpRoute *route = (HttpRoute *) route_ptr;

		str_delete (route->base);
		str_delete (route->actual);
		str_delete (route->route);

		dlist_delete (route->children);

		if (route->custom_data) {
			if (route->delete_custom_data) {
				route->delete_custom_data (route->custom_data);
			}
		}

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			http_route_stats_delete (route->stats[i]);
		}

		http_route_file_stats_delete (route->file_stats);

		free (route_ptr);
	}

}

// a handler for a method outside REQUEST_METHOD_MAP is never registered
static void http_route_set_method_handler (
	HttpRoute *route, const RequestMethod method, const HttpHandler handler
) {

	if (http_request_method_is_valid (method)) {
		route->handlers[method] = handler;

		if (!route->stats[method]) {
			route->stats[method] = http_route_stats_new ();
		}
	}

	else {
		cerver_log_error (
			"http_route_set_handler () - route %s can not handle the invalid method %d",
			route->route ? route->route->str : "", (int) method
		);
	}

}

// creates a new route that can be registered to be used by an http cerver
HttpRoute *http_route_create (
	const RequestMethod method,
	const char *actual_route,
	const HttpHandler handler
) {

	HttpRoute *route = NULL;

	if (actual_route) {
		route = http_route_new ();
		if (route) {
			route->actual = str_new (actual_route);

			// by default, all routes are top level when they are created
			route->base = str_new ("/");

			if (!strcmp ("/", actual_route)) route->route = str_new ("/");
			else route->route = str_create ("/%s", actual_route);

			route->children = dlist_init (http_route_delete, NULL);

			http_route_set_method_handler (route, method, handler);

			route->file_stats = http_route_file_stats_create ();
		}
	}

	return route;

}

// sets the route's handler for the selected http method
void http_route_set_handler (
	HttpRoute *route, const RequestMethod method, const HttpHandler handler
) {

	if (route) {
		http_route_set_method_handler (route, method, handler);
	}

}

void http_route_child_add (HttpRoute *parent, HttpRoute *child) {

	if (parent && child) {
		(void) dlist_insert_at_end (parent->children, child);

		// refactor child paths
		str_delete (child->base);
		child->base = str_new (parent->route->str);

		str_delete (child->route);

		if (!strcmp ("/", child->base->str)) {
			child->route = str_create ("/%s", child->actual->str);
		}

		else {
			child->route = str_create ("%s/%s", child->base->str, child->actual->str);
		}
	}

}

// sets a modifier for the selected route
void http_route_set_modifier (
	HttpRoute *route, const HttpRouteModifier modifier
) {

	if (route) route->modifier = modifier;

}

// enables authentication for the selected route
void http_route_set_auth (
	HttpRoute *route, const HttpRouteAuthType auth_type
) {

	if (route) route->auth_type = auth_type;

}

// sets the method to be used to decode incoming data from JWT
// also sets a method to delete it after use
// if no delete method is set, data won't be freed
void http_route_set_decode_data (
	HttpRoute *route,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
) {

	if (route) {
		route->decode_data = decode_data;
		route->delete_decoded_data = delete_decoded_data;
	}

}

// sets a method to decode data from a jwt into a json string
void http_route_set_decode_data_into_json (
	HttpRoute *route
) {

	if (route) {
		route->decode_data = http_decode_data_into_json;
		route->delete_decoded_data = free;
	}

}

// sets a method to be used to handle auth in a private route
// that has been configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// method must return 0 on success and 1 on error
void http_route_set_authentication_handler (
	HttpRoute *route,
	unsigned int (*authentication_handler)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (route) {
		route->authentication_handler = authentication_handler;
	}

}

void http_route_set_authentication_with_body (
	HttpRoute *route, const bool authentication_with_body
) {

	if (route) {
		route->authentication_with_body = authentication_with_body;
	}

}

const void *http_route_get_custom_data (
	const HttpRoute *route
) {

	return route ? route->custom_data : NULL;

}

void http_route_set_custom_data (
	HttpRoute *route, void *custom_data
) {

	if (route) route->custom_data = custom_data;

}

void http_route_set_delete_custom_data (
	HttpRoute *route, void (*delete_custom_data)(void *)
) {

	if (route) route->delete_custom_data = delete_custom_data;

}

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
				http_request_method_str ((RequestMethod) method)
			);

			if ((written < 0) || ((size_t) written >= (buffer_size - length))) break;

			length += (size_t) written;
		}
	}

	return length;

}

static void http_route_print_child (const HttpRoute *child) {

	cerver_log_msg ("\t\tRoute: %s", child->route->str);

	char route_methods_str[HTTP_ROUTE_METHODS_STRING_SIZE] = { 0 };
	(void) http_route_methods_string (child->handlers, route_methods_str, HTTP_ROUTE_METHODS_STRING_SIZE);

	cerver_log_msg ("\t\tMethods: %s", route_methods_str);

	cerver_log_msg ("\t\tModifier: %s", http_route_modifier_to_string (child->modifier));
	cerver_log_msg ("\t\tAuth: %s", http_route_auth_type_to_string (child->auth_type));

}

void http_route_print (const HttpRoute *route) {

	if (route) {
		cerver_log_msg ("Route: %s", route->route->str);

		char route_methods_str[HTTP_ROUTE_METHODS_STRING_SIZE] = { 0 };
		(void) http_route_methods_string (route->handlers, route_methods_str, HTTP_ROUTE_METHODS_STRING_SIZE);

		cerver_log_msg ("\tMethods: %s", route_methods_str);

		cerver_log_msg ("\tModifier: %s", http_route_modifier_to_string (route->modifier));
		cerver_log_msg ("\tAuth: %s", http_route_auth_type_to_string (route->auth_type));

		if (route->children->size) {
			cerver_log_msg ("\tChildren (%ld):", route->children->size);
			for (ListElement *le = dlist_start (route->children); le; le = le->next) {
				http_route_print_child ((HttpRoute *) le->data);
			}
		}

		else {
			cerver_log_msg ("\tNo children");
		}
	}

}

#pragma endregion
