#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"

#include "cerver/http/core/context.h"
#include "cerver/http/core/handler.h"
#include "cerver/http/core/http.h"
#include "cerver/http/routes/tree.h"

#pragma region views

bool cerver_http_view_is_missing (const CerverHttpView view) {

	return view.data == NULL;

}

bool cerver_http_view_equals (
	const CerverHttpView view, const char *string
) {

	return view.data && string
		&& (strlen (string) == view.length)
		&& !memcmp (view.data, string, view.length);

}

#pragma endregion

#pragma region context

Cerver *cerver_http_context_cerver (const CerverHttpContext *context) {

	return (context && context->cr) ? context->cr->cerver : NULL;

}

CerverHttp *cerver_http_context_http (const CerverHttpContext *context) {

	return context ? context->http_cerver : NULL;

}

const CerverHttpEndpoint *cerver_http_context_endpoint (const CerverHttpContext *context) {

	return context ? context->endpoint : NULL;

}

const void *cerver_http_context_endpoint_custom_data (const CerverHttpContext *context) {

	return http_receive_get_route_custom_data (context);

}

const void *cerver_http_context_http_custom_data (const CerverHttpContext *context) {

	return http_receive_get_cerver_custom_data (context);

}

const char *cerver_http_context_allow (const CerverHttpContext *context) {

	return http_receive_get_allow (context);

}

bool cerver_http_context_client_is_gone (const CerverHttpContext *context) {

	return context ? http_receive_is_client_gone (context) : false;

}

CerverHttpClientState cerver_http_context_client_state (const CerverHttpContext *context) {

	return http_receive_get_client_state (context);

}

int cerver_http_context_socket (const CerverHttpContext *context) {

	return http_receive_get_sock_fd (context);

}

#pragma endregion
