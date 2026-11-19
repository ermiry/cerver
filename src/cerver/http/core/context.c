#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include <pthread.h>

#include "cerver/core/cerver.h"
#include "cerver/handler/handler.h"

#include "cerver/http/clients/client.h"
#include "cerver/http/core/connection.h"
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

#pragma region deferred

// only the handler of a request of an event loop defers its response, while it runs and in its own thread: a
// middleware, who completes the request and any other thread would defer a request that nobody completes
static bool http_context_can_defer (const CerverHttpContext *context) {

	return context && context->connection && context->handling
		&& pthread_equal (context->handler_thread, pthread_self ());

}

u8 cerver_http_context_defer (CerverHttpContext *context) {

	return http_context_can_defer (context) ? http_connection_defer (context->connection) : 1;

}

// a request that waits for the verdict of its authentication is not completed: it has not been handled
u8 cerver_http_context_complete (CerverHttpContext *context) {

	return (context && context->connection && !context->authentication_deferred) ?
		http_connection_complete (context->connection) : 1;

}

// only the authentication handler of a request of an event loop defers its verdict, while it runs and in its
// own thread
static bool http_context_can_defer_authentication (const CerverHttpContext *context) {

	return context && context->connection && context->authenticating
		&& pthread_equal (context->handler_thread, pthread_self ());

}

u8 cerver_http_context_authentication_defer (CerverHttpContext *context) {

	return http_context_can_defer_authentication (context) ?
		http_connection_defer_authentication (context->connection) : 1;

}

u8 cerver_http_context_authentication_complete (CerverHttpContext *context, const bool authenticated) {

	return (context && context->connection) ?
		http_connection_authenticate (context->connection, authenticated) : 1;

}

// the response of the request that an authentication handler sent to decide: its method gives the verdict
static void http_context_authentication_response (const CerverHttpClientResponse *response, void *context_ptr) {

	CerverHttpContext *context = (CerverHttpContext *) context_ptr;

	const bool authenticated = context->authentication_response (context, response);

	(void) cerver_http_context_authentication_complete (context, authenticated);

}

u8 cerver_http_context_authentication_request (
	CerverHttpContext *context,
	CerverHttpClientRequest *request, CerverHttpAuthenticationResponse on_response
) {

	u8 retval = 1;

	if (request) {
		if (on_response && !cerver_http_context_authentication_defer (context)) {
			context->authentication_response = on_response;

			retval = cerver_http_client_request_send (request, http_context_authentication_response, context);

			// the request was not sent, and it was deleted: nobody else gives the verdict
			if (retval) (void) cerver_http_context_authentication_complete (context, false);
		}

		else {
			cerver_http_client_request_delete (request);
		}
	}

	return retval;

}

#pragma endregion
