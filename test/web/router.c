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

#include <cerver/http/core/context.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/core/settings.h>
#include <cerver/http/core/stats.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/response.h>
#include <cerver/http/routes/middleware.h>
#include <cerver/http/routes/tree.h>

#include <cerver/utils/log.h>

#include "../address.h"
#include "../handler.h"

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

	cerver_http_all_stats_print (cerver_http_get (router_cerver));
	(void) cerver_teardown (router_cerver);

}

// sends {"key": "value"} with the value of a param
static void router_send_param (
	CerverHttpContext *context, const char *key, const CerverHttpView value
) {

	char json[ROUTER_JSON_SIZE] = { 0 };
	const int length = snprintf (
		json, ROUTER_JSON_SIZE, "{\"%s\": \"%s\"}", key, cerver_http_view_is_missing (value) ? "" : value.data
	);

	(void) cerver_http_response_send_json (context, json, (size_t) length);

}

// GET /api/items/:id<objectid>
static void router_item_by_id_handler (CerverHttpContext *context) {

	router_send_param (context, "id", cerver_http_request_param (context, "id"));

}

// GET /api/items/:name
static void router_item_by_name_handler (CerverHttpContext *context) {

	router_send_param (context, "name", cerver_http_request_param (context, "name"));

}

// GET /api/items/me, PATCH /api/items/:name, GET /legacy/debug/status
static void router_ok_handler (CerverHttpContext *context) {

	(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_OK);

}

// GET /legacy/:id/status
static void router_legacy_status_handler (CerverHttpContext *context) {

	router_send_param (context, "status", cerver_http_request_param_at (context, 0));

}

// GET /api/private, sends the custom data of its group
static void router_private_handler (CerverHttpContext *context) {

	const char *data = (const char *) cerver_http_context_endpoint_custom_data (context);

	char json[ROUTER_JSON_SIZE] = { 0 };
	const int length = snprintf (json, ROUTER_JSON_SIZE, "{\"data\": \"%s\"}", data ? data : "");

	(void) cerver_http_response_send_json (context, json, (size_t) length);

}

static unsigned int router_authentication (CerverHttpContext *context) {

	const CerverHttpView authorization = cerver_http_request_header (context, HTTP_HEADER_AUTHORIZATION);

	return cerver_http_view_equals (authorization, router_authorization) ? 0 : 1;

}

// the request id header must be present in the requests to the items group
static CerverHttpMiddlewareResult router_request_id_middleware (CerverHttpContext *context) {

	CerverHttpMiddlewareResult result = HTTP_MIDDLEWARE_RESULT_CONTINUE;

	if (cerver_http_view_is_missing (cerver_http_request_header (context, HTTP_HEADER_REQUEST_ID))) {
		(void) cerver_http_response_send_common (context, CERVER_HTTP_COMMON_RESPONSE_BAD_REQUEST);
		result = HTTP_MIDDLEWARE_RESULT_STOP;
	}

	return result;

}

static void router_routes (CerverHttp *http_cerver) {

	CerverHttpGroup *api = cerver_http_group_create (http_cerver, "api");

	CerverHttpGroup *items = cerver_http_group_subgroup_add (api, "items");
	(void) cerver_http_group_add_before (items, router_request_id_middleware);
	(void) cerver_http_group_get (items, ":id<objectid>", router_item_by_id_handler);
	(void) cerver_http_group_get (items, ":name", router_item_by_name_handler);
	(void) cerver_http_group_get (items, "me", router_ok_handler);
	(void) cerver_http_group_patch (items, ":name", router_ok_handler);

	CerverHttpEndpoint *limited = cerver_http_group_post (api, "limited", router_ok_handler);
	cerver_http_endpoint_set_max_body_size (limited, ROUTER_MAX_BODY_SIZE);

	CerverHttpGroup *private_group = cerver_http_group_subgroup_add (api, "private");
	cerver_http_group_set_auth (private_group, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_group_set_authentication_handler (private_group, router_authentication);
	cerver_http_group_set_custom_data (private_group, (void *) router_group_data, NULL);
	(void) cerver_http_group_get (private_group, "", router_private_handler);

	// the routes that were of the first routes API, the static route was shadowed by the param before the tree
	CerverHttpGroup *legacy = cerver_http_group_create (http_cerver, "/legacy");
	(void) cerver_http_group_root_add (legacy, REQUEST_METHOD_GET, router_ok_handler);
	(void) cerver_http_group_get (legacy, ":id/status", router_legacy_status_handler);
	(void) cerver_http_group_get (legacy, "debug/status", router_ok_handler);

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
		cerver_set_events_backend (router_cerver, test_events_backend ());
		cerver_set_io_threads (router_cerver, test_io_threads ());
		cerver_set_reusable_address_flags (router_cerver, true);

		CerverHttp *http_cerver = cerver_http_get (router_cerver);
		cerver_http_set_method_not_allowed (http_cerver, true);
		cerver_http_set_not_found_handler (http_cerver);

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
