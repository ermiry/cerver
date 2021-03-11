#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/http/http.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/route.h"

#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include "cerver/utils/log.h"

// GET [top level]/cerver/stats
static void http_cerver_admin_handler (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	json_t *json = json_object ();
	if (json) {
		(void) json_object_set_new (
			json, "n_incompleted_requests", json_integer ((json_int_t) http_cerver->n_incompleted_requests)
		);

		(void) json_object_set_new (
			json, "n_unhandled_requests", json_integer ((json_int_t) http_cerver->n_unhandled_requests)
		);

		(void) json_object_set_new (
			json, "n_catch_all_requests", json_integer ((json_int_t) http_cerver->n_catch_all_requests)
		);

		(void) json_object_set_new (
			json, "n_failed_auth_requests", json_integer ((json_int_t) http_cerver->n_failed_auth_requests)
		);

		char *json_string = json_dumps (json, 0);
		if (json_string) {
			(void) http_response_render_json (
				http_receive, json_string, strlen (json_string)
			);
		}

		json_decref (json);
	}

}

u8 http_cerver_admin_init (HttpRoute *top_level_route) {

	u8 retval = 1;

	if (top_level_route) {
		// GET [top level]/cerver/stats
		HttpRoute *admin_route = http_route_create (REQUEST_METHOD_GET, "cerver/stats", http_cerver_admin_handler);
		http_route_child_add (top_level_route, admin_route);

		retval = 0;
	}

	return retval;

}