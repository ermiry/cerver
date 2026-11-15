#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/string.h>

#include <cerver/version.h>

#include <cerver/core/cerver.h>
#include <cerver/core/end.h>
#include <cerver/core/start.h>
#include <cerver/handler/handler.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/middleware.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>

#include "../address.h"

#define ROUTER_PORT						8080
#define ROUTER_THREADS					4
#define ROUTER_RECEIVE_BUFFER_SIZE		4096
#define ROUTER_MAX_BODY_SIZE			(1 << 4)
#define ROUTER_JSON_SIZE				256

static const char *router_authorization = { "router-token" };
static const char *router_group_data = { "items" };

static Cerver *router_cerver = NULL;

static void end (int dummy) {

	(void) dummy;

	if (router_cerver) (void) cerver_shutdown (router_cerver);

}

static void router_finish (void) {

	http_cerver_all_stats_print ((HttpCerver *) router_cerver->cerver_data);
	(void) cerver_teardown (router_cerver);

}

// sends {"key": "value"} with the value of a param
static void router_send_param (
	const HttpReceive *http_receive, const char *key, const String *value
) {

	char json[ROUTER_JSON_SIZE] = { 0 };
	const int length = snprintf (
		json, ROUTER_JSON_SIZE, "{\"%s\": \"%s\"}", key, value ? value->str : ""
	);

	(void) http_response_render_json (http_receive, HTTP_STATUS_OK, json, (size_t) length);

}

// GET /api/items/:id<objectid>
static void router_item_by_id_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	router_send_param (http_receive, "id", http_request_get_param (request, "id"));

}

// GET /api/items/:name
static void router_item_by_name_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	router_send_param (http_receive, "name", http_request_get_param (request, "name"));

}

// GET /api/items/me, PATCH /api/items/:name, GET /legacy/debug/status
static void router_ok_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	(void) http_response_send (oki_doki, http_receive);

}

// GET /legacy/:id/status
static void router_legacy_status_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	router_send_param (http_receive, "status", http_request_get_param_at_idx (request, 0));

}

// GET /api/private, sends the custom data of its group
static void router_private_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) request;

	const char *data = (const char *) http_receive_get_route_custom_data (http_receive);

	char json[ROUTER_JSON_SIZE] = { 0 };
	const int length = snprintf (json, ROUTER_JSON_SIZE, "{\"data\": \"%s\"}", data ? data : "");

	(void) http_response_render_json (http_receive, HTTP_STATUS_OK, json, (size_t) length);

}

static unsigned int router_authentication (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;

	const String *authorization = http_request_get_header (request, HTTP_HEADER_AUTHORIZATION);

	return (authorization && !strcmp (authorization->str, router_authorization)) ? 0 : 1;

}

// the request id header must be present in the requests to the items group
static HttpMiddlewareResult router_request_id_middleware (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	HttpMiddlewareResult result = HTTP_MIDDLEWARE_RESULT_CONTINUE;

	if (!http_request_get_header (request, HTTP_HEADER_REQUEST_ID)) {
		(void) http_response_send (bad_request_error, http_receive);
		result = HTTP_MIDDLEWARE_RESULT_STOP;
	}

	return result;

}

static void router_routes (HttpCerver *http_cerver) {

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");

	HttpGroup *items = http_group_create (api, "items");
	(void) http_group_add_before (items, router_request_id_middleware);
	(void) http_group_get (items, ":id<objectid>", router_item_by_id_handler);
	(void) http_group_get (items, ":name", router_item_by_name_handler);
	(void) http_group_get (items, "me", router_ok_handler);
	(void) http_group_patch (items, ":name", router_ok_handler);

	HttpEndpoint *limited = http_group_post (api, "limited", router_ok_handler);
	http_endpoint_set_max_body_size (limited, ROUTER_MAX_BODY_SIZE);

	HttpGroup *private_group = http_group_create (api, "private");
	http_group_set_auth (private_group, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_group_set_authentication_handler (private_group, router_authentication);
	http_group_set_custom_data (private_group, (void *) router_group_data, NULL);
	(void) http_group_get (private_group, "", router_private_handler);

	// the first routes API, the static route was shadowed by the param before the tree
	HttpRoute *legacy = http_route_create (REQUEST_METHOD_GET, "legacy", router_ok_handler);
	http_route_child_add (legacy, http_route_create (REQUEST_METHOD_GET, ":id/status", router_legacy_status_handler));
	http_route_child_add (legacy, http_route_create (REQUEST_METHOD_GET, "debug/status", router_ok_handler));
	http_cerver_route_register (http_cerver, legacy);

}

int main (int argc, char **argv) {

	(void) argc;
	(void) argv;

	(void) signal (SIGINT, end);
	(void) signal (SIGTERM, end);
	(void) signal (SIGPIPE, SIG_IGN);

	cerver_init ();

	cerver_version_print_full ();

	router_cerver = cerver_create_web ("router-cerver", ROUTER_PORT, CERVER_DEFAULT_CONNECTION_QUEUE);
	if (router_cerver) {
		(void) cerver_set_bind_address (router_cerver, test_address ());
		cerver_set_receive_buffer_size (router_cerver, ROUTER_RECEIVE_BUFFER_SIZE);
		cerver_set_thpool_n_threads (router_cerver, ROUTER_THREADS);
		cerver_set_handler_type (router_cerver, CERVER_HANDLER_TYPE_THREADS);
		cerver_set_reusable_address_flags (router_cerver, true);

		HttpCerver *http_cerver = (HttpCerver *) router_cerver->cerver_data;
		http_cerver_set_method_not_allowed (http_cerver, true);
		http_cerver_set_not_found_handler (http_cerver);

		router_routes (http_cerver);

		if (!cerver_start (router_cerver)) {
			router_finish ();
		}

		else {
			cerver_log_error ("Failed to start %s!", router_cerver->info->name);
			(void) cerver_teardown (router_cerver);
		}
	}

	else {
		cerver_log_error ("Failed to create cerver!");
	}

	cerver_end ();

	return 0;

}
