#include <stdlib.h>
#include <string.h>

#include <limits.h>

#include "cerver/types/string.h"

#include "cerver/collections/dlist.h"

#include "cerver/handler.h"

#include "cerver/threads/thread.h"

#include "cerver/http/route.h"
#include "cerver/http/request.h"

#include "cerver/utils/utils.h"

const char *http_route_modifier_to_string (HttpRouteModifier modifier) {

	switch (modifier) {
		#define XX(num, name, string, description) case HTTP_ROUTE_MODIFIER_##name: return #string;
		HTTP_ROUTE_MODIFIER_MAP(XX)
		#undef XX
	}

	return http_route_modifier_to_string (HTTP_ROUTE_MODIFIER_NONE);

}

const char *http_route_modifier_description (HttpRouteModifier modifier) {

	switch (modifier) {
		#define XX(num, name, string, description) case HTTP_ROUTE_MODIFIER_##name: return #description;
		HTTP_ROUTE_MODIFIER_MAP(XX)
		#undef XX
	}

	return http_route_modifier_description (HTTP_ROUTE_MODIFIER_NONE);

}

const char *http_route_auth_type_to_string (HttpRouteAuthType type) {

	switch (type) {
		#define XX(num, name, string, description) case HTTP_ROUTE_AUTH_TYPE_##name: return #string;
		HTTP_ROUTE_AUTH_TYPE_MAP(XX)
		#undef XX
	}

	return http_route_auth_type_to_string (HTTP_ROUTE_AUTH_TYPE_NONE);

}

const char *http_route_auth_type_description (HttpRouteAuthType type) {

	switch (type) {
		#define XX(num, name, string, description) case HTTP_ROUTE_AUTH_TYPE_##name: return #description;
		HTTP_ROUTE_AUTH_TYPE_MAP(XX)
		#undef XX
	}

	return http_route_auth_type_description (HTTP_ROUTE_AUTH_TYPE_NONE);

}

#pragma region tokens

static HttpRoutesTokens *http_routes_tokens_new (void) {

	HttpRoutesTokens *http_routes_tokens = (HttpRoutesTokens *) malloc (sizeof (HttpRoutesTokens));
	if (http_routes_tokens) {
		http_routes_tokens->id = 0;
		http_routes_tokens->n_routes = 0;
		http_routes_tokens->routes = NULL;
		http_routes_tokens->tokens = NULL;
	}

	return http_routes_tokens;

}

static void http_routes_tokens_delete (void *http_routes_tokens_ptr) {

	if (http_routes_tokens_ptr) {
		HttpRoutesTokens *http_routes_tokens = (HttpRoutesTokens *) http_routes_tokens_ptr;

		if (http_routes_tokens->routes) free (http_routes_tokens->routes);

		if (http_routes_tokens->tokens) {
			for (unsigned int i = 0; i < http_routes_tokens->n_routes; i++) {
				if (http_routes_tokens->tokens[i]) {
					for (unsigned int j = 0; j < http_routes_tokens->id; j++) {
						if (http_routes_tokens->tokens[i][j])
							free (http_routes_tokens->tokens[i][j]);
					}

					free (http_routes_tokens->tokens[i]);
				}
			}

			free (http_routes_tokens->tokens);
		}

		free (http_routes_tokens_ptr);
	}

}

#pragma endregion

#pragma region stats

HttpRouteStats *http_route_stats_new (void) {

	HttpRouteStats *route_stats = (HttpRouteStats *) malloc (sizeof (HttpRouteStats));
	if (route_stats) {
		(void) memset (route_stats, 0, sizeof (HttpRouteStats));

		route_stats->first = true;

		route_stats->min_process_time = __DBL_MAX__;

		route_stats->min_request_size = LONG_MAX;

		route_stats->min_response_size = LONG_MAX;

		route_stats->mutex = pthread_mutex_new ();
	}

	return route_stats;

}

void http_route_stats_delete (void *route_stats_ptr) {

	if (route_stats_ptr) {
		HttpRouteStats *route_stats = (HttpRouteStats *) route_stats_ptr;

		pthread_mutex_delete (route_stats->mutex);
		
		free (route_stats_ptr);
	}

}

// adds one request to counter
// updates process times & request & response sizes
void http_route_stats_update (
	HttpRouteStats *route_stats,
	double process_time,
	size_t request_size, size_t response_size
) {

	if (route_stats) {
		(void) pthread_mutex_lock (route_stats->mutex);

		if (route_stats->first) route_stats->first = false;

		route_stats->n_requests += 1;

		// process
		if (process_time < route_stats->min_process_time)
			route_stats->min_process_time = process_time;

		if (process_time > route_stats->max_process_time)
			route_stats->max_process_time = process_time;

		route_stats->sum_process_times += process_time;
		route_stats->mean_process_time = (route_stats->sum_process_times / route_stats->n_requests);

		// request size
		if (request_size < route_stats->min_request_size)
			route_stats->min_request_size = request_size;

		if (request_size > route_stats->max_request_size)
			route_stats->max_request_size = request_size;

		route_stats->sum_request_sizes += request_size;
		route_stats->mean_request_size = (route_stats->sum_request_sizes / route_stats->n_requests);

		// response size
		if (response_size < route_stats->min_response_size)
			route_stats->min_response_size = response_size;

		if (response_size > route_stats->max_response_size)
			route_stats->max_response_size = response_size;

		route_stats->sum_response_sizes += response_size;
		route_stats->mean_response_size = (route_stats->sum_response_sizes / route_stats->n_requests);

		(void) pthread_mutex_unlock (route_stats->mutex);
	}

}

HttpRouteFileStats *http_route_file_stats_new (void) {

	HttpRouteFileStats *route_file_stats = (HttpRouteFileStats *) malloc (sizeof (HttpRouteFileStats));
	if (route_file_stats) {
		(void) memset (route_file_stats, 0, sizeof (HttpRouteFileStats));

		route_file_stats->min_n_files = LONG_MAX;

		route_file_stats->min_file_size = LONG_MAX;

		route_file_stats->mutex = NULL;
	}

	return route_file_stats;

}

void http_route_file_stats_delete (void *route_file_stats_ptr) {

	if (route_file_stats_ptr) {
		HttpRouteFileStats *route_file_stats = (HttpRouteFileStats *) route_file_stats_ptr;

		pthread_mutex_delete (route_file_stats->mutex);
		
		free (route_file_stats_ptr);
	}

}

HttpRouteFileStats *http_route_file_stats_create (void) {

	HttpRouteFileStats *route_file_stats = http_route_file_stats_new ();
	if (route_file_stats) {
		route_file_stats->mutex = pthread_mutex_new ();
	}

	return route_file_stats;

}

// updates route's file stats with http receive file stats
void http_route_file_stats_update (
	HttpRouteFileStats *file_stats,
	HttpRouteFileStats *new_file_stats
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
		file_stats->mean_n_files = ((double) file_stats->sum_n_files / (double) file_stats->n_requests);

		// size
		if (new_file_stats->min_file_size < file_stats->min_file_size)
			file_stats->min_file_size = new_file_stats->min_file_size;

		if (new_file_stats->max_file_size > file_stats->max_file_size)
			file_stats->max_file_size = new_file_stats->max_file_size;

		file_stats->sum_file_size += new_file_stats->sum_file_size;
		file_stats->mean_file_size = ((double) file_stats->sum_n_files / (double) file_stats->n_uploaded_files);

		(void) pthread_mutex_unlock (file_stats->mutex);
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

		route->n_tokens = 0;
		route->tokens = NULL;

		route->children = NULL;

		route->routes_tokens = NULL;

		route->modifier = HTTP_ROUTE_MODIFIER_NONE;

		route->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;

		route->decode_data = NULL;
		route->delete_decoded_data = NULL;

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			route->handlers[i] = NULL;
			route->stats[i] = NULL;
		}

		route->file_stats = NULL;
	}

	return route;

}

void http_route_delete (void *route_ptr) {

	if (route_ptr) {
		HttpRoute *route = (HttpRoute *) route_ptr;

		str_delete (route->base);
		str_delete (route->actual);
		str_delete (route->route);

		// 03/08/2020 - deleted in http_routes_tokens_delete ()
		// free (route->tokens);

		dlist_delete (route->children);

		if (route->routes_tokens) {
			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				if (route->routes_tokens[i]) http_routes_tokens_delete (route->routes_tokens[i]);
			}

			free (route->routes_tokens);
		}

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			http_route_stats_delete (route->stats[i]);
		}

		http_route_file_stats_delete (route->file_stats);

		free (route_ptr);
	}

}

int http_route_comparator_by_n_tokens (const void *a, const void *b) {

	return ((HttpRoute *) a)->n_tokens - ((HttpRoute *) b)->n_tokens;

}

// creates a new route that can be registered to be sued by an http cerver
HttpRoute *http_route_create ( 
	RequestMethod method, 
	const char *actual_route, 
	HttpHandler handler
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

			route->handlers[method] = handler;

			for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
				route->stats[i] = http_route_stats_new ();
			}

			route->file_stats = http_route_file_stats_create ();
		}
	}

	return route;

}

// sets the route's handler for the selected http method
void http_route_set_handler (
	HttpRoute *route, RequestMethod method, HttpHandler handler
) {

	if (route) {
		route->handlers[method] = handler;
	}

}

void http_route_init (HttpRoute *route) {

	if (route) {
		if (route->children) {
			route->routes_tokens = (HttpRoutesTokens **) calloc (DEFAULT_ROUTES_TOKENS_SIZE, sizeof (HttpRoutesTokens *));

			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				route->routes_tokens[i] = http_routes_tokens_new ();
				route->routes_tokens[i]->id = i + 1;
			}

			// prepare children routes
			HttpRoute *child = NULL;
			for (ListElement *le = dlist_start (route->children); le; le = le->next) {
				child = (HttpRoute *) le->data;

				child->tokens = c_string_split (child->actual->str, '/', &child->n_tokens);
				if (!child->tokens) {
					child->tokens = (char **) calloc (1, sizeof (char *));
					child->tokens[0] = strdup (child->actual->str);
					child->n_tokens = 1;
				}

				// printf ("%s -> n_tokens: %ld\n", child->actual->str, child->n_tokens);

				// check if route params have been setup
				for (unsigned int i = 0; i < child->n_tokens; i++) {
					if (c_string_count_tokens (child->tokens[i], ':')) {
						free (child->tokens[i]);

						child->tokens[i] = (char *) calloc (2, sizeof (char));
						child->tokens[i][0] = '*';
						child->tokens[i][1] = '\0';
					}
				}

				route->routes_tokens[child->n_tokens - 1]->n_routes += 1;
			}

			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				route->routes_tokens[i]->routes = (HttpRoute **) calloc (route->routes_tokens[i]->n_routes, sizeof (HttpRoute));
				route->routes_tokens[i]->tokens = (char ***) calloc (route->routes_tokens[i]->n_routes, sizeof (char **));

				for (unsigned int route_idx = 0; route_idx < route->routes_tokens[i]->n_routes; route_idx++) {
					route->routes_tokens[i]->routes[route_idx] = NULL;
					route->routes_tokens[i]->tokens[route_idx] = NULL;
				}
			}

			for (unsigned int i = 0; i < DEFAULT_ROUTES_TOKENS_SIZE; i++) {
				unsigned int n_tokens = i + 1;
				unsigned int idx = 0;
				for (ListElement *le = dlist_start (route->children); le; le = le->next) {
					child = (HttpRoute *) le->data;

					if (child->n_tokens == n_tokens) {
						// printf ("%s\n", child->actual->str);
						route->routes_tokens[n_tokens - 1]->routes[idx] = child;
						route->routes_tokens[n_tokens - 1]->tokens[idx] = child->tokens;
						idx++;
					}
				}
			}
		}
	}

}

void http_route_child_add (HttpRoute *parent, HttpRoute *child) {

	if (parent && child) {
		dlist_insert_after (parent->children, dlist_end (parent->children), child);

		// refactor child paths
		str_delete (child->base);
		child->base = str_new (parent->route->str);

		str_delete (child->route);
		child->route = str_create ("%s/%s", child->base->str, child->actual->str);
	}

}

// sets a modifier for the selected route
void http_route_set_modifier (HttpRoute *route, HttpRouteModifier modifier) {

	if (route) route->modifier = modifier;

}

// enables authentication for the selected route
void http_route_set_auth (HttpRoute *route, HttpRouteAuthType auth_type) {

	if (route) route->auth_type = auth_type;

}

// sets the method to be used to decode incoming data from jwt & a method to delete it after use
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

static void http_route_get_methods_string (
	HttpRoute *route, const char *route_methods_str
) {

	char *end = (char *) route_methods_str;
	char *method_name = NULL;
	size_t method_name_len = 0;
	for (u8 i = 0; i < HTTP_HANDLERS_COUNT; i++) {
		if (route->handlers[i]) {
			method_name = (char *) http_request_method_str ((RequestMethod) i);
			method_name_len = strlen (method_name);
			(void) memcpy (end, method_name, method_name_len);
			end += method_name_len += 1;
		}
	}

	*end = '\0';

}

static void http_route_print_child (HttpRoute *child) {

	cerver_log_msg ("\t\tRoute: %s", child->route->str);

	char route_methods_str[128] = { 0 };
	http_route_get_methods_string (child, route_methods_str);

	cerver_log_msg ("\t\tMethods: %s", route_methods_str);

	cerver_log_msg ("\t\tModifier: %s", http_route_modifier_to_string (child->modifier));
	cerver_log_msg ("\t\tAuth: %s", http_route_auth_type_to_string (child->auth_type));

}

void http_route_print (HttpRoute *route) {

	if (route) {
		cerver_log_msg ("Route: %s", route->route->str);

		char route_methods_str[128] = { 0 };
		http_route_get_methods_string (route, route_methods_str);

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