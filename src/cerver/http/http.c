#include <stdlib.h>

#include <time.h>

#include "cerver/types/types.h"

#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/files.h"

#include "cerver/http/http.h"
#include "cerver/http/http_parser.h"
#include "cerver/http/method.h"
#include "cerver/http/route.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/alg.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"

static void http_receive_handle_default_route (CerverReceive *cr, HttpRequest *request);

#pragma region kv

static KeyValuePair *key_value_pair_new (void) {

	KeyValuePair *kvp = (KeyValuePair *) malloc (sizeof (KeyValuePair));
	if (kvp) kvp->key = kvp->value = NULL;
	return kvp;

}

static void key_value_pair_delete (void *ptr) {

	if (ptr) {
		KeyValuePair *kvp = (KeyValuePair *) ptr;
		if (kvp->key) free (kvp->key);
		if (kvp->value) free (kvp->value);

		free (kvp);
	}

}

static KeyValuePair *key_value_pair_create (const char *keyFirst, const char *keyAfter, 
	const char *valueFirst, const char *valueAfter) {

	KeyValuePair *kvp = NULL;

	if (keyFirst && keyAfter && valueFirst && valueAfter) {
		const int keyLen = (int) (keyAfter - keyFirst);
		const int valueLen = (int) (valueAfter - valueFirst);

		kvp = key_value_pair_new ();
		if (kvp) {
			kvp->key = (char *) calloc (keyLen + 1, sizeof (char));
			memcpy (kvp->key, keyFirst, keyLen * sizeof (char));
			kvp->value = (char *) calloc (valueLen + 1, sizeof (char));
			memcpy (kvp->value, valueFirst, valueLen * sizeof (char));
		}
	}

	return kvp;

}

#pragma endregion

#pragma region http

HttpCerver *http_cerver_new (void) {

	HttpCerver *http_cerver = (HttpCerver *) malloc (sizeof (HttpCerver));
	if (http_cerver) {
		http_cerver->cerver = NULL;

		http_cerver->routes = NULL;

		http_cerver->default_handler = NULL;

		http_cerver->jwt_alg = JWT_ALG_NONE;

		http_cerver->jwt_opt_key_name = NULL;
		http_cerver->jwt_private_key = NULL;

		http_cerver->jwt_opt_pub_key_name = NULL;
		http_cerver->jwt_public_key = NULL;
	}

	return http_cerver;

}

void http_cerver_delete (void *http_cerver_ptr) {

	if (http_cerver_ptr) {
		HttpCerver *http_cerver = (HttpCerver *) http_cerver_ptr;

		dlist_delete (http_cerver->routes);

		estring_delete (http_cerver->jwt_opt_key_name);
		estring_delete (http_cerver->jwt_private_key);

		estring_delete (http_cerver->jwt_opt_pub_key_name);
		estring_delete (http_cerver->jwt_public_key);

		free (http_cerver_ptr);
	}

}

HttpCerver *http_cerver_create (Cerver *cerver) {

	HttpCerver *http_cerver = http_cerver_new ();
	if (http_cerver) {
		http_cerver->cerver = cerver;

		http_cerver->routes = dlist_init (http_route_delete, NULL);

		http_cerver->default_handler = http_receive_handle_default_route;

		http_cerver->jwt_alg = JWT_DEFAULT_ALG;
	}

	return http_cerver;

}

static unsigned int http_cerver_init_load_jwt_private_key (HttpCerver *http_cerver) {

	unsigned int retval = 1;

	size_t private_keylen = 0;
	char *private_key = file_read (http_cerver->jwt_opt_key_name->str, &private_keylen);
	if (private_key) {
		http_cerver->jwt_private_key = estring_new (NULL);
		http_cerver->jwt_private_key->str = private_key;
		http_cerver->jwt_private_key->len = private_keylen;

		printf ("\n%s\n", http_cerver->jwt_public_key->str);

		char *status = c_string_create (
			"Loaded cerver's %s http jwt PRIVATE key (size %ld)!",
			http_cerver->cerver->info->name->str,
			http_cerver->jwt_private_key->len
		);

		if (status) {
			cerver_log_success (status);
			free (status);
		}

		retval = 0;
	}

	else {
		char *status = c_string_create (
			"Failed to load cerver's %s http jwt PRIVATE key!",
			http_cerver->cerver->info->name->str
		);

		if (status) {
			cerver_log_error (status);
			free (status);
		}
	}

	return retval;

}

static unsigned int http_cerver_init_load_jwt_public_key (HttpCerver *http_cerver) {

	unsigned int retval = 1;

	size_t public_keylen = 0;
	char *public_key = file_read (http_cerver->jwt_opt_pub_key_name->str, &public_keylen);
	if (public_key) {
		http_cerver->jwt_public_key = estring_new (NULL);
		http_cerver->jwt_public_key->str = public_key;
		http_cerver->jwt_public_key->len = public_keylen;

		printf ("\n%s\n", http_cerver->jwt_public_key->str);

		char *status = c_string_create (
			"Loaded cerver's %s http jwt PUBLIC key (size %ld)!",
			http_cerver->cerver->info->name->str,
			http_cerver->jwt_public_key->len
		);

		if (status) {
			cerver_log_success (status);
			free (status);
		}

		retval = 0;
	}

	else {
		char *status = c_string_create (
			"Failed to load cerver's %s http jwt PUBLIC key!",
			http_cerver->cerver->info->name->str
		);

		if (status) {
			cerver_log_error (status);
			free (status);
		}
	}

	return retval;

}

static unsigned int http_cerver_init_load_jwt_keys (HttpCerver *http_cerver) {

	unsigned int errors = 0 ;

	if (http_cerver->jwt_opt_key_name) {
		errors |= http_cerver_init_load_jwt_private_key (http_cerver);
	}

	if (http_cerver->jwt_opt_pub_key_name) {
		errors |= http_cerver_init_load_jwt_public_key (http_cerver);
	}

	return errors;

}

void http_cerver_init (HttpCerver *http_cerver) {

	if (http_cerver) {
		// init top level routes
		for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
			http_route_init ((HttpRoute *) le->data);
		}

		// load jwt keys
		(void) http_cerver_init_load_jwt_keys (http_cerver);
	}

}

#pragma endregion

#pragma region routes

void http_cerver_route_register (HttpCerver *http_cerver, HttpRoute *route) {

	if (http_cerver && route) {
		dlist_insert_after (http_cerver->routes, dlist_end (http_cerver->routes), route);
	}

}

void http_cerver_set_catch_all_route (HttpCerver *http_cerver, 
	void (*catch_all_route)(CerverReceive *cr, HttpRequest *request)) {

	if (http_cerver && catch_all_route) {
		http_cerver->default_handler = catch_all_route;
	}

}

#pragma endregion

#pragma region auth

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256
void http_cerver_auth_set_jwt_algorithm (HttpCerver *http_cerver, jwt_alg_t jwt_alg) {

	if (http_cerver) http_cerver->jwt_alg = jwt_alg;

}

// sets the filename from where the jwt private key will be loaded
void http_cerver_auth_set_jwt_priv_key_filename (HttpCerver *http_cerver, const char *filename) {

	if (http_cerver) http_cerver->jwt_opt_key_name = filename ? estring_new (filename) : NULL;

}

// sets the filename from where the jwt public key will be loaded
void http_cerver_auth_set_jwt_pub_key_filename (HttpCerver *http_cerver, const char *filename) {

	if (http_cerver) http_cerver->jwt_opt_pub_key_name = filename ? estring_new (filename) : NULL;

}

#pragma endregion

#pragma region parser

static int http_receive_handle_url (http_parser *parser, const char *at, size_t length) {

	// printf ("\n%s\n\n", at);
	// printf ("%.*s\n", (int) length, at);

	HttpRequest *request = (HttpRequest *) parser->data;
	request->url = estring_new (NULL);
	request->url->str = c_string_create ("%.*s", (int) length, at);
	request->url->len = length;

	return 0;

}

static inline int http_receive_handle_header_field_handle (const char *header) {

	if (!strcmp ("Accept", header)) return REQUEST_HEADER_ACCEPT;
	if (!strcmp ("Accept-Charset", header)) return REQUEST_HEADER_ACCEPT_CHARSET;
	if (!strcmp ("Accept-Encoding", header)) return REQUEST_HEADER_ACCEPT_ENCODING;
	if (!strcmp ("Accept-Language", header)) return REQUEST_HEADER_ACCEPT_LANGUAGE;

	if (!strcmp ("Access-Control-Request-Headers", header)) return REQUEST_HEADER_ACCESS_CONTROL_REQUEST_HEADERS;

	if (!strcmp ("Authorization", header)) return REQUEST_HEADER_AUTHORIZATION;

	if (!strcmp ("Cache-Control", header)) return REQUEST_HEADER_CACHE_CONTROL;

	if (!strcmp ("Connection", header)) return REQUEST_HEADER_CONNECTION;

	if (!strcmp ("Content-Length", header)) return REQUEST_HEADER_CONTENT_LENGTH;
	if (!strcmp ("Content-Type", header)) return REQUEST_HEADER_CONTENT_TYPE;

	if (!strcmp ("Cookie", header)) return REQUEST_HEADER_COOKIE;

	if (!strcmp ("Date", header)) return REQUEST_HEADER_DATE;

	if (!strcmp ("Expect", header)) return REQUEST_HEADER_EXPECT;

	if (!strcmp ("Host", header)) return REQUEST_HEADER_HOST;

	if (!strcmp ("Proxy-Authorization", header)) return REQUEST_HEADER_PROXY_AUTHORIZATION;

	if (!strcmp ("User-Agent", header)) return REQUEST_HEADER_USER_AGENT;

	return REQUEST_HEADER_INVALID;		// no known header

}

static int http_receive_handle_header_field (http_parser *parser, const char *at, size_t length) {

	char header[32] = { 0 };
	snprintf (header, 32, "%.*s", (int) length, at);
	// printf ("\nheader field: /%s/\n", header);

	((HttpRequest *) parser->data)->next_header = http_receive_handle_header_field_handle (header);

	return 0;

}

static int http_receive_handle_header_value (http_parser *parser, const char *at, size_t length) {

	// printf ("\nheader value: %s\n", at);

	HttpRequest *request = (HttpRequest *) parser->data;
	if (request->next_header != REQUEST_HEADER_INVALID) {
		request->headers[request->next_header] = estring_new (NULL);
		request->headers[request->next_header]->str = c_string_create ("%.*s", (int) length, at);
		request->headers[request->next_header]->len = length;
	}

	// request->next_header = REQUEST_HEADER_INVALID;

	return 0;

}

static int http_receive_handle_body (http_parser *parser, const char *at, size_t length) {

	// printf ("Body: %.*s", (int) length, at);

	HttpRequest *request = (HttpRequest *) parser->data;
	request->body = estring_new (NULL);
	request->body->str = c_string_create ("%.*s", (int) length, at);
	request->body->len = length;

	return 0;

}

DoubleList *http_parse_query_into_pairs (const char *first, const char *last) {

	DoubleList *pairs = NULL;

	if (first && last) {
		pairs = dlist_init (key_value_pair_delete, NULL);

		const char *walk = first;
		const char *keyFirst = first;
		const char *keyAfter = NULL;
		const char *valueFirst = NULL;
		const char *valueAfter = NULL;

		for (; walk < last; walk++) {
			switch (*walk) {
				case '&': {
					if (valueFirst != NULL) valueAfter = walk;
					else keyAfter = walk;

					KeyValuePair *kvp = key_value_pair_create (keyFirst, keyAfter, valueFirst, valueAfter);
					if (kvp) dlist_insert_after (pairs, dlist_end (pairs), kvp);
				
					if (walk + 1 < last) keyFirst = walk + 1;
					else keyFirst = NULL;
					
					keyAfter = NULL;
					valueFirst = NULL;
					valueAfter = NULL;
				} break;

				case '=': {
					if (keyAfter == NULL) {
						keyAfter = walk;
						if (walk + 1 <= last) {
							valueFirst = walk + 1;
							valueAfter = walk + 1;
						}
					}
				} break;

				default: break;
			}
		}

		if (valueFirst != NULL) valueAfter = walk;
		else keyAfter = walk;

		KeyValuePair *kvp = key_value_pair_create (keyFirst, keyAfter, valueFirst, valueAfter);
		if (kvp) dlist_insert_after (pairs, dlist_end (pairs), kvp);
	}

	return pairs;

}

const char *http_query_pairs_get_value (DoubleList *pairs, const char *key) {

	const char *value = NULL;

	if (pairs) {
		KeyValuePair *kvp = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kvp = (KeyValuePair *) le->data;
			if (!strcmp (kvp->key, key)) {
				value = kvp->value;
				break;
			}
		}
	}

	return value;

}

// FIXME:
static char *http_strip_path_from_query (char *str) {

	char *query = NULL;

	if (str) {
		unsigned int len = strlen (str);
		unsigned int idx = 0;
		for (; idx < len; idx++) {
			if (str[idx] == '?') break;
		} 
		
		idx++;
		int newlen = len - idx;
		query = (char *) calloc (newlen, sizeof (char));
		int j = 0;
		for (unsigned int i = idx; i < len; i++) {
			query[j] = str[i];
			j++;
		} 
	}

	return query;

}

#pragma endregion

#pragma region handler

static void http_receive_handle_default_route (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg (200, "HTTP Cerver!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

// catch all mismatches and handle with cath all route
static void http_receive_handle_catch_all (HttpCerver *http_cerver, CerverReceive *cr, HttpRequest *request) {

	char *status = c_string_create (
		"No matching route for %s %s",
		http_method_str (request->method),
		request->url->str
	);

	if (status) {
		cerver_log_warning (status);
		free (status);
	}

	// handle with default route
	http_cerver->default_handler (cr, request);

}

// handles an actual route match & selects the right handler based on the request's method
static void http_receive_handle_match (
	HttpCerver *http_cerver, CerverReceive *cr, 
	HttpRequest *request, 
	HttpRoute *found
) {

	if (request->method < HTTP_HANDLERS_COUNT) {
		if (found->handlers[request->method]) {
			found->handlers[request->method] (cr, request);
		}

		else {
			http_receive_handle_catch_all (http_cerver, cr, request);
		}
	}

	else {
		http_receive_handle_catch_all (http_cerver, cr, request);
	}

}

static inline bool http_receive_handle_select_children (HttpRoute *route, HttpRequest *request, HttpRoute **found) {

	bool retval = false;

	size_t n_tokens = 0;
	char **tokens = NULL;

	printf ("request->url->str: %s\n", request->url->str);
	char *start_sub = strstr (request->url->str, route->route->str);
	if (start_sub) {
		// match and still some path left
		char *end_sub = request->url->str + route->route->len;

		printf ("first end_sub: %s\n", end_sub);

		tokens = c_string_split (end_sub, '/', &n_tokens);

		printf ("n tokens: %ld\n", n_tokens);
		printf ("second end_sub: %s\n", end_sub);

		if (tokens) {
			HttpRoutesTokens *routes_tokens = route->routes_tokens[n_tokens - 1];
			if (routes_tokens->n_routes) {
				// match all url tokens with existing route tokens
				bool fail = false;
				for (unsigned int main_idx = 0; main_idx < routes_tokens->n_routes; main_idx++) {
					printf ("testing route %s\n", routes_tokens->routes[main_idx]->route->str);

					if (routes_tokens->tokens[main_idx]) {
						fail = false;
						printf ("routes_tokens->id: %d\n", routes_tokens->id);
						for (unsigned int sub_idx = 0; sub_idx < routes_tokens->id; sub_idx++) {
							printf ("%s\n", routes_tokens->tokens[main_idx][sub_idx]);
							if (routes_tokens->tokens[main_idx][sub_idx][0] != '*') {
								if (strcmp (routes_tokens->tokens[main_idx][sub_idx], tokens[sub_idx])) {
									printf ("fail!\n");
									fail = true;
									break;
								}
							}

							else {
								printf ("*\n");
							}
						}

						if (!fail) {
							// we have found our route!
							retval = true;

							// get route params
							for (unsigned int sub_idx = 0; sub_idx < routes_tokens->id; sub_idx++) {
								if (routes_tokens->tokens[main_idx][sub_idx][0] == '*') {
									request->params[request->n_params] = estring_new (tokens[sub_idx]);
									request->n_params++;
								}
							}

							// routes_tokens->routes[main_idx]->handler (cr, request);
							*found = routes_tokens->routes[main_idx];

							break;
						}
					}
				}
			}

			else {
				printf ("\nno routes!\n");
			}

			for (size_t i = 0; i < n_tokens; i++) if (tokens[i]) free (tokens[i]);
			free (tokens);
		}
	}

	return retval;

}

static void http_receive_handle_select_failed_auth (CerverReceive *cr) {

	HttpResponse *res = http_response_json_error (400, "Failed to authenticate!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

static void http_receive_handle_select_auth_bearer (HttpCerver *http_cerver, CerverReceive *cr, 
	HttpRoute *found, HttpRequest *request) {

	// get the bearer token
	printf ("\nComplete Token -> %s\n", request->headers[REQUEST_HEADER_AUTHORIZATION]->str);

	char *token = request->headers[REQUEST_HEADER_AUTHORIZATION]->str + sizeof ("Bearer");

	printf ("\nToken -> %s\n", token);

	jwt_t *jwt = NULL;
	jwt_valid_t *jwt_valid = NULL;
	if (!jwt_valid_new (&jwt_valid, http_cerver->jwt_alg)) {
		jwt_valid->hdr = 1;
		jwt_valid->now = time (NULL);

		int ret = jwt_decode (&jwt, token, (unsigned char *) http_cerver->jwt_public_key->str, http_cerver->jwt_public_key->len);
		if (!ret) {
			cerver_log_debug ("JWT decoded successfully!");

			if (!jwt_validate (jwt, jwt_valid)) {
				cerver_log_success ("JWT is authentic!");

				if (found->decode_data) {
					request->decoded_data = found->decode_data (jwt->grants);
					request->delete_decoded_data = found->delete_decoded_data;
				}

				http_receive_handle_match (
					http_cerver,
					cr,
					request,
					found
				);
			}

			else {
				char *status = c_string_create (
					"Failed to validate JWT: %08x", 
					jwt_valid_get_status(jwt_valid)
				);

				if (status) {
					cerver_log_error (status);
					free (status);
				}

				http_receive_handle_select_failed_auth (cr);
			}

			jwt_free(jwt);
		}

		else {
			cerver_log_error ("Invalid JWT!");

			http_receive_handle_select_failed_auth (cr);
		}
	}

	jwt_valid_free (jwt_valid);

}

// select the route that will handle the request
static void http_receive_handle_select (CerverReceive *cr, HttpRequest *request) {

	HttpCerver *http_cerver = (HttpCerver *) cr->cerver->cerver_data;

	// split the real url variables from the query
	// TODO:

	bool match = false;
	HttpRoute *found = NULL;

	// search top level routes at the start of the url
	HttpRoute *route = NULL;
	ListElement *le = dlist_start (http_cerver->routes);
	while (!match && le) {
		route = (HttpRoute *) le->data;

		if (route->route->len == request->url->len) {
			if (!strcmp (route->route->str, request->url->str)) {
				// we have a direct match
				match = true;
				found = route;
				break;
			}
		}

		else {
			if (route->children->size) {
				match = http_receive_handle_select_children (route, request, &found);
			}
		}

		le = le->next;
	}

	// we have found a route!
	if (match) {
		switch (found->auth_type) {
			// no authentication, handle the request directly
			case HTTP_ROUTE_AUTH_TYPE_NONE: {
				http_receive_handle_match (
					http_cerver,
					cr,
					request,
					found
				);
			} break;

			// handle authentication with bearer token
			case HTTP_ROUTE_AUTH_TYPE_BEARER: {
				if (request->headers[REQUEST_HEADER_AUTHORIZATION]) {
					http_receive_handle_select_auth_bearer (http_cerver, cr, found, request);
				}

				// no authentication header was provided
				else {
					http_receive_handle_select_failed_auth (cr);
				}
			} break;
		}
	}

	else {
		http_receive_handle_catch_all (http_cerver, cr, request);
	}

}

void http_receive_handle (CerverReceive *cr, ssize_t rc, char *packet_buffer) {

	http_parser *parser = malloc (sizeof (http_parser));
	http_parser_init (parser, HTTP_REQUEST);

	http_parser_settings settings = { 0 };
	settings.on_message_begin = NULL;
	settings.on_url = http_receive_handle_url;
	settings.on_status = NULL;
	settings.on_header_field = http_receive_handle_header_field;
	settings.on_header_value = http_receive_handle_header_value;
	settings.on_headers_complete = NULL;
	settings.on_body = http_receive_handle_body;
	settings.on_message_complete = NULL;
	settings.on_chunk_header = NULL;
	settings.on_chunk_complete = NULL;

	HttpRequest *request = http_request_new ();
	parser->data = request;

	size_t n_parsed = http_parser_execute (parser, &settings, packet_buffer, rc);

	printf ("\nn parsed %ld / received %ld\n", n_parsed, rc);

	// printf ("Method: %s\n", http_method_str (parser->method));

	request->method = parser->method;

	// http_request_headers_print (request);

	// select method handler
	http_receive_handle_select (cr, request);

	http_request_delete (request);

	connection_end (cr->connection);

	// packet_delete (packet);
	// estring_delete (response);

	free (parser);

	printf ("http_receive_handle () - end!\n");

}

#pragma endregion