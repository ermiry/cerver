#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ctype.h>
#include <time.h>

#include <openssl/sha.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/handler.h"
#include "cerver/packets.h"
#include "cerver/files.h"

#include "cerver/http/http.h"
#include "cerver/http/http_parser.h"
#include "cerver/http/multipart.h"
#include "cerver/http/method.h"
#include "cerver/http/route.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/alg.h"

#include "cerver/utils/utils.h"
#include "cerver/utils/log.h"
#include "cerver/utils/base64.h"

static void http_static_path_delete (void *http_static_path_ptr);

static int http_static_path_comparator (const void *a, const void *b);

static void http_receive_handle_default_route (CerverReceive *cr, HttpRequest *request);

static void http_receive_handle (
	HttpReceive *http_receive, 
	ssize_t rc, char *packet_buffer
);

static void http_web_sockets_receive_handle (
	HttpReceive *http_receive, 
	ssize_t rc, char *packet_buffer
);

#pragma region utils

static const char hex[] = "0123456789abcdef";

// converts a hex character to its integer value
static const char from_hex (const char ch) { return isdigit(ch) ? ch - '0' : tolower(ch) - 'a' + 10; }

// converts an integer value to its hex character
static const char to_hex (const char code) { return hex[code & 15]; }

#pragma endregion

#pragma region content

const char *http_content_type_string (ContentType content_type) {

	switch (content_type) {
		#define XX(num, name, string, description) case CONTENT_TYPE_##name: return #string;
		CONTENT_TYPE_MAP(XX)
		#undef XX

		default: return NULL;
	}

}

const char *http_content_type_description (ContentType content_type) {

	switch (content_type) {
		#define XX(num, name, string, description) case CONTENT_TYPE_##name: return #description;
		CONTENT_TYPE_MAP(XX)
		#undef XX

		default: return NULL;
	}

}

const char *http_content_type_by_extension (const char *ext) {

	#define XX(num, name, string, description) if (!strcmp (#string, ext)) return #description;
	CONTENT_TYPE_MAP(XX)
	#undef XX

	return NULL;

}

#pragma endregion

#pragma region kvp

KeyValuePair *key_value_pair_new (void) {

	KeyValuePair *kvp = (KeyValuePair *) malloc (sizeof (KeyValuePair));
	if (kvp) kvp->key = kvp->value = NULL;
	return kvp;

}

void key_value_pair_delete (void *kvp_ptr) {

	if (kvp_ptr) {
		KeyValuePair *kvp = (KeyValuePair *) kvp_ptr;

		if (kvp->key) str_delete (kvp->key);
		if (kvp->value) str_delete (kvp->value);

		free (kvp);
	}

}

KeyValuePair *key_value_pair_create (const char *key, const char *value) {

	KeyValuePair *kvp = key_value_pair_new ();
	if (kvp) {
		kvp->key = key ? str_new (key) : NULL;
		kvp->value = value ? str_new (value) : NULL;
	}

	return kvp;

}

static KeyValuePair *key_value_pair_create_pieces (
	const char *key_first, const char *key_after, 
	const char *value_first, const char *value_after
) {

	KeyValuePair *kvp = NULL;

	if (key_first && key_after && value_first && value_after) {
		kvp = key_value_pair_new ();
		if (kvp) {
			kvp->key = str_new (NULL);
			kvp->key->len = key_after - key_first;
			kvp->key->str = (char *) calloc (kvp->key->len + 1, sizeof (char));
			memcpy (kvp->key->str, key_first, kvp->key->len);

			kvp->value = str_new (NULL);
			kvp->value->len = value_after - value_first;
			kvp->value->str = (char *) calloc (kvp->value->len + 1, sizeof (char));
			memcpy (kvp->value->str, value_first, kvp->value->len);
		}
	}

	return kvp;

}

const String *key_value_pairs_get_value (DoubleList *pairs, const char *key) {

	const String *value = NULL;

	if (pairs && key) {
		KeyValuePair *kvp = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kvp = (KeyValuePair *) le->data;
			if (!strcmp (kvp->key->str, key)) {
				value = kvp->value;
				break;
			}
		}
	}

	return value;

}

void key_value_pairs_print (DoubleList *pairs) {

	if (pairs) {
		unsigned int idx = 1;
		KeyValuePair *kv = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kv = (KeyValuePair *) le->data;

			printf ("[%d] - %s = %s\n", idx, kv->key->str, kv->value->str);

			idx++;
		}
	}

}

#pragma endregion

#pragma region http

HttpCerver *http_cerver_new (void) {

	HttpCerver *http_cerver = (HttpCerver *) malloc (sizeof (HttpCerver));
	if (http_cerver) {
		http_cerver->cerver = NULL;

		http_cerver->static_paths = NULL;

		http_cerver->routes = NULL;

		http_cerver->default_handler = NULL;

		http_cerver->uploads_path = NULL;
		http_cerver->uploads_dirname_generator = NULL;

		http_cerver->jwt_alg = JWT_ALG_NONE;

		http_cerver->jwt_opt_key_name = NULL;
		http_cerver->jwt_private_key = NULL;

		http_cerver->jwt_opt_pub_key_name = NULL;
		http_cerver->jwt_public_key = NULL;

		http_cerver->n_cath_all_requests = 0;
		http_cerver->n_failed_auth_requests = 0;
	}

	return http_cerver;

}

void http_cerver_delete (void *http_cerver_ptr) {

	if (http_cerver_ptr) {
		HttpCerver *http_cerver = (HttpCerver *) http_cerver_ptr;

		dlist_delete (http_cerver->static_paths);

		dlist_delete (http_cerver->routes);

		str_delete (http_cerver->uploads_path);

		str_delete (http_cerver->jwt_opt_key_name);
		str_delete (http_cerver->jwt_private_key);

		str_delete (http_cerver->jwt_opt_pub_key_name);
		str_delete (http_cerver->jwt_public_key);

		free (http_cerver_ptr);
	}

}

HttpCerver *http_cerver_create (Cerver *cerver) {

	HttpCerver *http_cerver = http_cerver_new ();
	if (http_cerver) {
		http_cerver->cerver = cerver;

		http_cerver->static_paths = dlist_init (http_static_path_delete, http_static_path_comparator);

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
		http_cerver->jwt_private_key = str_new (NULL);
		http_cerver->jwt_private_key->str = private_key;
		http_cerver->jwt_private_key->len = private_keylen;

		// printf ("\n%s\n", http_cerver->jwt_private_key->str);

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
		http_cerver->jwt_public_key = str_new (NULL);
		http_cerver->jwt_public_key->str = public_key;
		http_cerver->jwt_public_key->len = public_keylen;

		// printf ("\n%s\n", http_cerver->jwt_public_key->str);

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

#pragma region public

static HttpStaticPath *http_static_path_new (void) {

	HttpStaticPath *http_static_path = (HttpStaticPath *) malloc (sizeof (HttpStaticPath));
	if (http_static_path) {
		http_static_path->path = NULL;
		http_static_path->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;
	}

	return http_static_path;

}

static void http_static_path_delete (void *http_static_path_ptr) {

	if (http_static_path_ptr) {
		HttpStaticPath *http_static_path = (HttpStaticPath *) http_static_path_ptr;

		str_delete (http_static_path->path);

		free (http_static_path);
	}

}

static int http_static_path_comparator (const void *a, const void *b) {

	return strcmp (((HttpStaticPath *) a)->path->str, ((HttpStaticPath *) b)->path->str);

}

static HttpStaticPath *http_static_path_create (const char *path) {

	HttpStaticPath *http_static_path = http_static_path_new ();
	if (http_static_path) {
		http_static_path->path = str_new (path);
	}

	return http_static_path;

}

// sets authentication requirenments for a whole static path
void http_static_path_set_auth (HttpStaticPath *static_path, HttpRouteAuthType auth_type) {

	if (static_path) static_path->auth_type = auth_type;

}

// add a new static path where static files can be served upon request
// it is recomened to set absoulute paths
// returns the http static path structure that was added to the cerver
HttpStaticPath *http_cerver_static_path_add (HttpCerver *http_cerver, const char *static_path) {

	HttpStaticPath *http_static_path = NULL;

	if (http_cerver && static_path) {
		http_static_path = http_static_path_create (static_path);

		(void) dlist_insert_after (
			http_cerver->static_paths,
			dlist_end (http_cerver->static_paths),
			http_static_path
		);
	}

	return http_static_path;

}

// removes a path from the served public paths
// returns 0 on success, 1 on error
u8 http_receive_public_path_remove (HttpCerver *http_cerver, const char *static_path) {

	u8 retval = 1;

	if (http_cerver && static_path) {
		String *query = str_new (static_path);

		void *data = dlist_remove (
			http_cerver->static_paths,
			query,
			NULL
		);

		if (data) {
			http_static_path_delete (data);
			retval = 0;
		}

		str_delete (query);
	}

	return retval;

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

#pragma region uploads

// sets the default uploads path where any multipart file request will be saved
// this method will replace the previous value with the new one
void http_cerver_set_uploads_path (HttpCerver *http_cerver, const char *uploads_path) {

	if (http_cerver) {
		if (http_cerver->uploads_path) str_delete (http_cerver->uploads_path);
		http_cerver->uploads_path = uploads_path ? str_new (uploads_path) : NULL;
	}

}

// sets a method to be called on every new request & that will be used to generate a new directory
// inside the uploads path to save all the files from each request
void http_cerver_set_uploads_dirname_generator (HttpCerver *http_cerver, String *(*dirname_generator)(CerverReceive *)) {

	if (http_cerver) {
		http_cerver->uploads_dirname_generator = dirname_generator;
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

	if (http_cerver) http_cerver->jwt_opt_key_name = filename ? str_new (filename) : NULL;

}

// sets the filename from where the jwt public key will be loaded
void http_cerver_auth_set_jwt_pub_key_filename (HttpCerver *http_cerver, const char *filename) {

	if (http_cerver) http_cerver->jwt_opt_pub_key_name = filename ? str_new (filename) : NULL;

}

// generates and signs a jwt token that is ready to be sent
// returns a newly allocated string that should be deleted after use
char *http_cerver_auth_generate_jwt (HttpCerver *http_cerver, DoubleList *values) {

	char *token = NULL;

	jwt_t *jwt = NULL;
	if (!jwt_new (&jwt)) {
		time_t iat = time (NULL);

		KeyValuePair *kvp = NULL;
		for (ListElement *le = dlist_start (values); le; le = le->next) {
			kvp = (KeyValuePair *) le->data;
			jwt_add_grant (jwt, kvp->key->str, kvp->value->str);
		}

		jwt_add_grant_int (jwt, "iat", iat);

		if (!jwt_set_alg (
			jwt, 
			http_cerver->jwt_alg, 
			(const unsigned char *) http_cerver->jwt_private_key->str, 
			http_cerver->jwt_private_key->len
		)) {
			token = jwt_encode_str (jwt);
		}

		jwt_free (jwt);
	}

	return token;

}

#pragma endregion

#pragma region stats

static size_t http_cerver_stats_get_children_routes (HttpCerver *http_cerver, size_t *handlers) {

	size_t count = 0;

	unsigned int i = 0;
	HttpRoute *route = NULL;
	for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
		route = (HttpRoute *) le->data;
		
		// main route handlers
		for (i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (route->handlers[i]) *handlers += 1;
		}

		count += route->children->size;

		HttpRoute *child = NULL;
		for (ListElement *child_le = dlist_start (route->children); child_le; child_le = child_le->next) {
			child = (HttpRoute *) child_le->data;

			for (i = 0; i < HTTP_HANDLERS_COUNT; i++) {
				if (child->handlers[i]) *handlers += 1;
			}
		}
	}

	return count;

}

// print number of routes & handlers
void http_cerver_routes_stats_print (HttpCerver *http_cerver) {

	if (http_cerver) {
		printf ("Main routes: %ld\n", http_cerver->routes->size);

		size_t total_handlers = 0;
		size_t children_routes = http_cerver_stats_get_children_routes (http_cerver, &total_handlers);

		printf ("Children routes: %ld\n", children_routes);
		printf ("Total handlers: %ld\n", total_handlers);
	}

}

// print route's stats
void http_cerver_route_stats_print (HttpRoute *route) {

	if (route) {
		unsigned int i = 0;

		printf ("\t%s:\n", route->route->str);
		
		for (i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (route->handlers[i]) 
				printf ("\t\t%s\t%ld\n", http_request_method_str ((RequestMethod) i), route->n_requests[i]);
		}

		if (route->children->size) {
			printf ("\t%ld children: \n", route->children->size);

			HttpRoute *child = NULL;
			for (ListElement *le = dlist_start (route->children); le; le = le->next) {
				child = (HttpRoute *) le->data;

				printf ("\t\t%s:\n", child->actual->str);

				for (i = 0; i < HTTP_HANDLERS_COUNT; i++) {
					if (child->handlers[i]) 
						printf ("\t\t\t%s\t%ld\n", http_request_method_str ((RequestMethod) i), child->n_requests[i]);
				}
			}
		}

		else {
			printf ("\tNo children\n");
		}
	}

}

// print all http cerver stats, general & by route
void http_cerver_all_stats_print (HttpCerver *http_cerver) {

	if (http_cerver) {
		http_cerver_routes_stats_print (http_cerver);

		// print stats by route
		for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
			http_cerver_route_stats_print ((HttpRoute *) le->data);
		}
	}

}

#pragma endregion

#pragma region url

// returns a newly allocated url-encoded version of str that should be deleted after use
char *http_url_encode (const char *str) {

	char *pstr = (char *) str, *buf = (char *) malloc (strlen (str) * 3 + 1), *pbuf = buf;

	if (buf) {
		while (*pstr) {
			if (isalnum (*pstr) || *pstr == '-' || *pstr == '_' || *pstr == '.' || *pstr == '~') 
				*pbuf++ = *pstr;

			else if (*pstr == ' ') *pbuf++ = '+';

			else *pbuf++ = '%', *pbuf++ = to_hex (*pstr >> 4), *pbuf++ = to_hex (*pstr & 15);

			pstr++;
		}

		*pbuf = '\0';
	}

	return buf;

}

// returns a newly allocated url-decoded version of str that should be deleted after use
char *http_url_decode (const char *str) {

	char *pstr = (char *) str, *buf = (char *) malloc (strlen (str) + 1), *pbuf = buf;

	if (buf) {
		while (*pstr) {
			if (*pstr == '%') {
				if (pstr[1] && pstr[2]) {
					*pbuf++ = from_hex (pstr[1]) << 4 | from_hex (pstr[2]);
					pstr += 2;
				}
			}
			
			else if (*pstr == '+') *pbuf++ = ' ';
			
			else *pbuf++ = *pstr;

			pstr++;
		}

		*pbuf = '\0';
	}

	return buf;

}

#pragma endregion

#pragma region parser

static String *http_strip_path_from_query (const char *url, size_t url_len) {

	String *query = NULL;

	unsigned int idx = 0;
	bool found = false;
	while (idx < url_len) {
		if (url[idx] == '?') {
			found = true;
			break;
		}

		idx++;
	}

	if (found) {
		query = str_new (NULL);
		query->len = url_len - idx;
		query->str = (char *) calloc (query->len, sizeof (char));
		char *from = (char *) url;
		from += (idx + 1);
		c_string_n_copy (query->str, from, query->len);
	}

	return query;

}

DoubleList *http_parse_query_into_pairs (const char *first, const char *last) {

	DoubleList *pairs = NULL;

	if (first && last) {
		pairs = dlist_init (key_value_pair_delete, NULL);

		const char *walk = first;
		const char *key_first = first;
		const char *key_after = NULL;
		const char *value_first = NULL;
		const char *value_after = NULL;

		for (; walk < last; walk++) {
			switch (*walk) {
				case '&': {
					if (value_first != NULL) value_after = walk;
					else key_after = walk;

					dlist_insert_after (
						pairs, 
						dlist_end (pairs), 
						key_value_pair_create_pieces (
							key_first, key_after, 
							value_first, value_after
						)
					);
				
					if (walk + 1 < last) key_first = walk + 1;
					else key_first = NULL;
					
					key_after = NULL;
					value_first = NULL;
					value_after = NULL;
				} break;

				case '=': {
					if (key_after == NULL) {
						key_after = walk;
						if (walk + 1 <= last) {
							value_first = walk + 1;
							value_after = walk + 1;
						}
					}
				} break;

				default: break;
			}
		}

		if (value_first != NULL) value_after = walk;
		else key_after = walk;

		dlist_insert_after (
			pairs, 
			dlist_end (pairs),
			key_value_pair_create_pieces (
				key_first, key_after, 
				value_first, value_after
			)
		);
	}

	return pairs;

}

const String *http_query_pairs_get_value (DoubleList *pairs, const char *key) {

	return key_value_pairs_get_value (pairs, key);

}

void http_query_pairs_print (DoubleList *pairs) {

	key_value_pairs_print (pairs);

}

static int http_receive_handle_url (http_parser *parser, const char *at, size_t length) {

	// printf ("url: %.*s\n", (int) length, at);

	HttpRequest *request = ((HttpReceive *) parser->data)->request;

	request->query = http_strip_path_from_query (at, length);
	// if (request->query) printf ("query: %s\n", request->query->str);
	
	request->url = str_new (NULL);
	request->url->len = request->query ? length - request->query->len : length;
	request->url->str = c_string_create ("%.*s", (int) request->url->len, at);

	// printf ("path: %s\n", request->url->str);

	return 0;

}

static inline RequestHeader http_receive_handle_header_field_handle (const char *header) {

	if (!strcasecmp ("Accept", header)) return REQUEST_HEADER_ACCEPT;
	if (!strcasecmp ("Accept-Charset", header)) return REQUEST_HEADER_ACCEPT_CHARSET;
	if (!strcasecmp ("Accept-Encoding", header)) return REQUEST_HEADER_ACCEPT_ENCODING;
	if (!strcasecmp ("Accept-Language", header)) return REQUEST_HEADER_ACCEPT_LANGUAGE;

	if (!strcasecmp ("Access-Control-Request-Headers", header)) return REQUEST_HEADER_ACCESS_CONTROL_REQUEST_HEADERS;

	if (!strcasecmp ("Authorization", header)) return REQUEST_HEADER_AUTHORIZATION;

	if (!strcasecmp ("Cache-Control", header)) return REQUEST_HEADER_CACHE_CONTROL;

	if (!strcasecmp ("Connection", header)) return REQUEST_HEADER_CONNECTION;

	if (!strcasecmp ("Content-Length", header)) return REQUEST_HEADER_CONTENT_LENGTH;
	if (!strcasecmp ("Content-Type", header)) return REQUEST_HEADER_CONTENT_TYPE;

	if (!strcasecmp ("Cookie", header)) return REQUEST_HEADER_COOKIE;

	if (!strcasecmp ("Date", header)) return REQUEST_HEADER_DATE;

	if (!strcasecmp ("Expect", header)) return REQUEST_HEADER_EXPECT;

	if (!strcasecmp ("Host", header)) return REQUEST_HEADER_HOST;

	if (!strcasecmp ("Origin", header)) return REQUEST_HEADER_ORIGIN;

	if (!strcasecmp ("Proxy-Authorization", header)) return REQUEST_HEADER_PROXY_AUTHORIZATION;

	if (!strcasecmp ("Upgrade", header)) return REQUEST_HEADER_UPGRADE;

	if (!strcasecmp ("User-Agent", header)) return REQUEST_HEADER_USER_AGENT;

	if (!strcasecmp ("Sec-WebSocket-Key", header)) return REQUEST_HEADER_WEB_SOCKET_KEY;
	if (!strcasecmp ("Sec-WebSocket-Version", header)) return REQUEST_HEADER_WEB_SOCKET_VERSION;

	return REQUEST_HEADER_INVALID;		// no known header

}

static int http_receive_handle_header_field (http_parser *parser, const char *at, size_t length) {

	char header[32] = { 0 };
	snprintf (header, 32, "%.*s", (int) length, at);
	// printf ("\nHeader field: /%.*s/\n", (int) length, at);

	(((HttpReceive *) parser->data)->request)->next_header = http_receive_handle_header_field_handle (header);

	return 0;

}

static int http_receive_handle_header_value (http_parser *parser, const char *at, size_t length) {

	// printf ("\nHeader value: %.*s\n", (int) length, at);

	HttpRequest *request = ((HttpReceive *) parser->data)->request;
	if (request->next_header != REQUEST_HEADER_INVALID) {
		request->headers[request->next_header] = str_new (NULL);
		request->headers[request->next_header]->str = c_string_create ("%.*s", (int) length, at);
		request->headers[request->next_header]->len = length;
	}

	// request->next_header = REQUEST_HEADER_INVALID;

	return 0;

}

static int http_receive_handle_body (http_parser *parser, const char *at, size_t length) {

	// printf ("Body: %.*s", (int) length, at);
	// printf ("%.*s", (int) length, at);

	HttpRequest *request = ((HttpReceive *) parser->data)->request;
	request->body = str_new (NULL);
	request->body->str = c_string_create ("%.*s", (int) length, at);
	request->body->len = length;

	return 0;

}

#pragma endregion

#pragma region mpart

static inline bool is_multipart (const char *content, const char *type) {

	return !strncmp (content, type, strlen (type)) ? true : false;

}

static DoubleList *http_mpart_attributes_parse (char *str) {

	DoubleList *attributes = dlist_init (key_value_pair_delete, NULL);

	char *pair, *name, *value;
	char *header_str = strdup (str);
	char *original = header_str;

	while (isspace (*header_str)) header_str++;

	while ((pair = strsep (&header_str, ";")) && pair != NULL) {
		name = strsep (&pair, "=");
		value = strsep (&pair, "=");

		dlist_insert_after (
			attributes, 
			dlist_end (attributes), 
			key_value_pair_create (
				c_string_trim (name),
				c_string_trim (c_string_strip_quotes (value))
			)
		);
	}

	free (original);

	return attributes;

}

static char *http_mpart_get_boundary (const char *content_type) {

	char *retval = NULL;

	char *end = (char *) content_type;
	end += strlen ("multipart/form-data;");

	DoubleList *attributes = http_mpart_attributes_parse (end);
	if (attributes) {
		// key_value_pairs_print (attributes);

		const String *original_boundary = http_query_pairs_get_value (attributes, "boundary");
		if (original_boundary) retval = c_string_create ("--%s", original_boundary->str);

		dlist_delete (attributes);
	}

	return retval;

}

static int http_receive_handle_mpart_part_data_begin (multipart_parser *parser) {

	// create a new multipart structure to handle new data
	HttpRequest *request = ((HttpReceive *) parser->data)->request;

	request->current_part = http_multi_part_new ();
	dlist_insert_after (
		request->multi_parts, 
		dlist_end (request->multi_parts), 
		request->current_part
	);

	return 0;

}

// Content-Disposition: form-data; name="mirary"; filename="M.jpg"
// Content-Type: image/jpeg
static inline MultiPartHeader http_receive_handle_mpart_header_field_handle (const char *header) {

	if (!strcasecmp ("Content-Disposition", header)) return MULTI_PART_HEADER_CONTENT_DISPOSITION;
	if (!strcasecmp ("Content-Length", header)) return MULTI_PART_HEADER_CONTENT_LENGTH;
	if (!strcasecmp ("Content-Type", header)) return MULTI_PART_HEADER_CONTENT_TYPE;

	return MULTI_PART_HEADER_INVALID;		// no known header

}

static int http_receive_handle_mpart_header_field (multipart_parser *parser, const char *at, size_t length) {

	char header[32] = { 0 };
	snprintf (header, 32, "%.*s", (int) length, at);
	// printf ("\nHeader field: /%.*s/\n", (int) length, at);

	(((HttpReceive *) parser->data)->request)->current_part->next_header = http_receive_handle_mpart_header_field_handle (header);

	return 0;

}

static int http_receive_handle_mpart_header_value (multipart_parser *parser, const char *at, size_t length) {

	// printf ("\nHeader value: %.*s\n", (int) length, at);

	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;
	if (multi_part->next_header != MULTI_PART_HEADER_INVALID) {
		multi_part->headers[multi_part->next_header] = str_new (NULL);
		multi_part->headers[multi_part->next_header]->str = c_string_create ("%.*s", (int) length, at);
		multi_part->headers[multi_part->next_header]->len = length;
	}

	// multi_part->next_header = MULTI_PART_HEADER_INVALID;

	return 0;

}

static int http_receive_handle_mpart_headers_completed (multipart_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = (http_receive)->request->current_part;

	http_multi_part_headers_print (multi_part);

	if (c_string_starts_with (multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION]->str, "form-data;")) {
		char *end = (char *) multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION]->str;
		end += strlen ("form-data;");

		multi_part->params = http_mpart_attributes_parse (end);
		// key_value_pairs_print (multi_part->params);

		multi_part->name = key_value_pairs_get_value (multi_part->params, "name");
		multi_part->filename = key_value_pairs_get_value (multi_part->params, "filename");

		if (multi_part->filename) {
			http_receive->request->n_files += 1;

			if (http_receive->http_cerver->uploads_path) {
				char *filename = c_string_create (
					"%s/%s", 
					http_receive->http_cerver->uploads_path->str, multi_part->filename->str
				);

				multi_part->fd = open (filename, O_CREAT | O_WRONLY, 0777);
				switch (multi_part->fd) {
					case -1: {
						char *status = c_string_create ("Failed to open %s filename to save multipart file!", filename);
						if (status) {
							cerver_log_error (status);
							free (status);
						}

						perror ("Error");

						free (filename);
					} break;

					default: {
						char *status = c_string_create ("Opened %s to save multipart file!", filename);
						if (status) {
							cerver_log_debug (status);
							free (status);
						}

						multi_part->saved_filename = str_new (NULL);
						multi_part->saved_filename->str = filename;
						multi_part->saved_filename->len = strlen (filename);
					} break;
				}
			}

			else {
				cerver_log_error ("Can't save multipart file - no uploads path!");
			}
		}

		else {
			http_receive->request->n_values += 1;
		}
	}

	return 0;

}

static int http_receive_handle_mpart_data (multipart_parser *parser, const char *at, size_t length) {

	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;

	// printf ("fd %d - filename %s - bytes %ld\n", multi_part->fd, multi_part->filename->str, length);

	if (multi_part->fd != -1) {
		multi_part->n_reads += 1;

		ssize_t wrote = write (multi_part->fd, at, length);
		switch (wrote) {
			case -1: {
				cerver_log_error ("http_receive_handle_mpart_data () - Error writting to file!");
				perror ("Error");
			} break;

			default: 
				multi_part->total_wrote += wrote;
				break;
		}
	}

	else {
		// printf ("|%.*s|\n", (int) length, at);
		multi_part->value = str_create ("%.*s", (int) length, at);
	}

	return 0;

}

static int http_receive_handle_mpart_data_end (multipart_parser *parser) {

	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;

	if (multi_part->fd != -1) {
		close (multi_part->fd);
	}

	return 0;

}

static int http_receive_handle_mpart_body (http_parser *parser, const char *at, size_t length) {

	(void) multipart_parser_execute (((HttpReceive *) parser->data)->mpart_parser, at, length);

	return 0;

}

#pragma endregion

#pragma region handler

static int http_receive_handle_headers_completed (http_parser *parser);

static int http_receive_handle_message_completed (http_parser *parser);

HttpReceive *http_receive_new (void) {

	HttpReceive *http_receive = (HttpReceive *) malloc (sizeof (HttpReceive));
	if (http_receive) {
		http_receive->cr = NULL;

		http_receive->keep_alive = false;

		http_receive->handler = http_receive_handle;

		http_receive->http_cerver = NULL;

		http_receive->parser = (http_parser *) malloc (sizeof (http_parser));
		http_parser_init (http_receive->parser, HTTP_REQUEST);

		http_receive->settings.on_message_begin = NULL;
		http_receive->settings.on_url = http_receive_handle_url;
		http_receive->settings.on_status = NULL;
		http_receive->settings.on_header_field = http_receive_handle_header_field;
		http_receive->settings.on_header_value = http_receive_handle_header_value;
		http_receive->settings.on_headers_complete = http_receive_handle_headers_completed;
		http_receive->settings.on_body = http_receive_handle_body;
		http_receive->settings.on_message_complete = http_receive_handle_message_completed;
		http_receive->settings.on_chunk_header = NULL;
		http_receive->settings.on_chunk_complete = NULL;

		http_receive->mpart_parser = NULL;

		http_receive->request = http_request_new ();

		// websockets
		http_receive->fin_rsv_opcode = 0;
		http_receive->fragmented_message_len = 0;
		http_receive->fragmented_message = NULL;
		http_receive->ws_on_open = NULL;
		http_receive->ws_on_close = NULL;
		http_receive->ws_on_ping = NULL;
		http_receive->ws_on_pong = NULL;
		http_receive->ws_on_message = NULL;
		http_receive->ws_on_error = NULL;

		// http_receive->parser->data = http_receive->request;
		http_receive->parser->data = http_receive;
	}

	return http_receive;

}

void http_receive_delete (HttpReceive *http_receive) {

	if (http_receive) {
		http_receive->cr = NULL;

		http_receive->http_cerver = NULL;

		free (http_receive->parser);

		http_request_delete (http_receive->request);

		if (http_receive->mpart_parser) multipart_parser_free (http_receive->mpart_parser);

		free (http_receive);
	}

}

static void http_receive_handle_default_route (CerverReceive *cr, HttpRequest *request) {

	HttpResponse *res = http_response_json_msg (HTTP_STATUS_OK, "HTTP Cerver!");
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
		http_method_str ((enum http_method) request->method),
		request->url->str
	);

	if (status) {
		cerver_log_warning (status);
		free (status);
	}

	// handle with default route
	http_cerver->default_handler (cr, request);

	http_cerver->n_cath_all_requests += 1;

}

// route is expecting a web socket connection, check headers and perform handshake
static void http_receive_handle_match_web_socket (
	HttpReceive *http_receive, CerverReceive *cr, 
	HttpRequest *request, 
	HttpRoute *route
) {

	// check if client sent the correct headers
	const String *connection = request->headers[REQUEST_HEADER_CONNECTION];
	const String *upgrade = request->headers[REQUEST_HEADER_UPGRADE];

	if (!strcasecmp ("Upgrade", connection->str) && !strcasecmp ("websocket", upgrade->str)) {
		const String *web_socket_key = request->headers[REQUEST_HEADER_WEB_SOCKET_KEY];
		// const String *web_socket_version = request->headers[REQUEST_HEADER_WEB_SOCKET_VERSION];

		if (web_socket_key) {
			char buffer[128] = { 0 };
			snprintf (buffer, 128, "%s%s", web_socket_key->str, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");

			unsigned char hash[SHA_DIGEST_LENGTH];
			SHA1 ((const unsigned char *) buffer, strlen (buffer), hash);

			memset (buffer, 0, 128);
			base64_encode (buffer, (const char *) hash, SHA_DIGEST_LENGTH);

			HttpResponse *res = http_response_create ((http_status) 101, NULL, 0);
			if (res) {
				http_response_add_header (res, RESPONSE_HEADER_CONNECTION, "Upgrade");
				http_response_add_header (res, RESPONSE_HEADER_UPGRADE, "websocket");
				http_response_add_header (res, RESPONSE_HEADER_WEB_SOCKET_ACCEPT, buffer);

				http_response_compile (res);
				http_response_print (res);
				http_response_send (res, cr->cerver, cr->connection);
				http_respponse_delete (res);
			}

			// keep connection alive
			http_receive->keep_alive = true;

			http_receive->handler = http_web_sockets_receive_handle;

			http_receive->ws_on_open = route->ws_on_open;
			http_receive->ws_on_close = route->ws_on_close;
			http_receive->ws_on_ping = route->ws_on_ping;
			http_receive->ws_on_pong = route->ws_on_pong;
			http_receive->ws_on_message = route->ws_on_message;
			http_receive->ws_on_error = route->ws_on_error;

			// set the sockets timeout to prevent threads from getting stuck if no more data to read
			if (sock_set_timeout (cr->connection->socket->sock_fd, DEFAULT_WEB_SOCKET_RECV_TIMEOUT)) {
				char *status = c_string_create (
					"http_receive_handle_match_web_socket () - Failed to set socket's %d timeout", 
					cr->connection->socket->sock_fd
				);

				if (status) {
					cerver_log_error (status);
					free (status);
				}

				// FIXME: end connection with error
			}
		}

		else http_response_create_and_send (400, NULL, 0, cr->cerver, cr->connection);
	}

	else http_response_create_and_send (400, NULL, 0, cr->cerver, cr->connection);

}

// handles an actual route match & selects the right handler based on the request's method
static void http_receive_handle_match (
	HttpCerver *http_cerver, 
	HttpReceive *http_receive, CerverReceive *cr, 
	HttpRequest *request, 
	HttpRoute *found
) {

	if (request->method < HTTP_HANDLERS_COUNT) {
		if (found->handlers[request->method]) {
			// parse query values
			if (request->query) {
				char *real_query = http_url_decode (request->query->str);

				if (real_query) {
					request->query_params = http_parse_query_into_pairs (
						real_query, 
						real_query + request->query->len
					);

					// printf ("Query: %s\n", request->query->str);
					// printf ("Real Query: %s\n", real_query);
					// http_query_pairs_print (request->query_params);

					free (real_query);
				}
			}

			// handle body based on header
			if (request->body) {
				if (request->headers[REQUEST_HEADER_CONTENT_TYPE]) {
					if (!strncmp ("application/x-www-form-urlencoded", request->headers[REQUEST_HEADER_CONTENT_TYPE]->str, 33)) {
						char *real_body = http_url_decode (request->body->str);

						if (real_body) {
							request->body_values = http_parse_query_into_pairs (
								real_body,
								real_body + request->body->len
							);

							// printf ("Body: %s\n", request->body->str);
							// printf ("Real Body: %s\n", real_body);
							// http_query_pairs_print (request->body_values);

							free (real_body);
						}
					}
				}
			}

			switch (found->modifier) {
				case HTTP_ROUTE_MODIFIER_NONE: found->handlers[request->method] (cr, request); break;

				case HTTP_ROUTE_MODIFIER_WEB_SOCKET:
					http_receive_handle_match_web_socket (http_receive, cr, request, found);
					break;
			}

			found->n_requests[request->method] += 1;
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

	// printf ("request->url->str: %s\n", request->url->str);
	char *start_sub = strstr (request->url->str, route->route->str);
	if (start_sub) {
		// match and still some path left
		char *end_sub = request->url->str + route->route->len;

		// printf ("first end_sub: %s\n", end_sub);

		tokens = c_string_split (end_sub, '/', &n_tokens);

		// printf ("n tokens: %ld\n", n_tokens);
		// printf ("second end_sub: %s\n", end_sub);

		if (tokens) {
			HttpRoutesTokens *routes_tokens = route->routes_tokens[n_tokens - 1];
			if (routes_tokens->n_routes) {
				// match all url tokens with existing route tokens
				bool fail = false;
				for (unsigned int main_idx = 0; main_idx < routes_tokens->n_routes; main_idx++) {
					// printf ("testing route %s\n", routes_tokens->routes[main_idx]->route->str);

					if (routes_tokens->tokens[main_idx]) {
						fail = false;
						// printf ("routes_tokens->id: %d\n", routes_tokens->id);
						for (unsigned int sub_idx = 0; sub_idx < routes_tokens->id; sub_idx++) {
							printf ("%s\n", routes_tokens->tokens[main_idx][sub_idx]);
							if (routes_tokens->tokens[main_idx][sub_idx][0] != '*') {
								if (strcmp (routes_tokens->tokens[main_idx][sub_idx], tokens[sub_idx])) {
									// printf ("fail!\n");
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
									request->params[request->n_params] = str_new (tokens[sub_idx]);
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
				// printf ("\nno routes!\n");
			}

			for (size_t i = 0; i < n_tokens; i++) if (tokens[i]) free (tokens[i]);
			free (tokens);
		}
	}

	return retval;

}

static void http_receive_handle_select_failed_auth (CerverReceive *cr) {

	HttpResponse *res = http_response_json_error (HTTP_STATUS_UNAUTHORIZED, "Failed to authenticate!");
	if (res) {
		http_response_print (res);
		http_response_send (res, cr->cerver, cr->connection);
		http_respponse_delete (res);
	}

}

static void http_receive_handle_select_auth_bearer (
	HttpCerver *http_cerver, 
	HttpReceive *http_receive, CerverReceive *cr, 
	HttpRoute *found, HttpRequest *request
) {

	// get the bearer token
	// printf ("\nComplete Token -> %s\n", request->headers[REQUEST_HEADER_AUTHORIZATION]->str);

	char *token = request->headers[REQUEST_HEADER_AUTHORIZATION]->str + sizeof ("Bearer");

	// printf ("\nToken -> %s\n", token);

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
					http_receive, cr,
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

				http_cerver->n_failed_auth_requests += 1;
			}

			jwt_free(jwt);
		}

		else {
			cerver_log_error ("Invalid JWT!");
			http_receive_handle_select_failed_auth (cr);
			http_cerver->n_failed_auth_requests += 1;
		}
	}

	jwt_valid_free (jwt_valid);

}

// select the route that will handle the request
static void http_receive_handle_select (HttpReceive *http_receive, CerverReceive *cr, HttpRequest *request) {

	HttpCerver *http_cerver = (HttpCerver *) cr->cerver->cerver_data;

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
					http_receive, cr,
					request,
					found
				);
			} break;

			// handle authentication with bearer token
			case HTTP_ROUTE_AUTH_TYPE_BEARER: {
				if (request->headers[REQUEST_HEADER_AUTHORIZATION]) {
					http_receive_handle_select_auth_bearer (http_cerver, http_receive, cr, found, request);
				}

				// no authentication header was provided
				else {
					http_receive_handle_select_failed_auth (cr);
					http_cerver->n_failed_auth_requests += 1;
				}
			} break;
		}
	}

	else {
		http_receive_handle_catch_all (http_cerver, cr, request);
	}

}

static int http_receive_handle_headers_completed (http_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	// http_request_headers_print (http_receive->request);

	// check if we are going to get any file(s)
	if (http_receive->request->headers[REQUEST_HEADER_CONTENT_TYPE]) {
		if (is_multipart (http_receive->request->headers[REQUEST_HEADER_CONTENT_TYPE]->str, "multipart/form-data")) {
			// printf ("\nis multipart!\n");
			char *boundary = http_mpart_get_boundary (http_receive->request->headers[REQUEST_HEADER_CONTENT_TYPE]->str);
			if (boundary) {
				// printf ("\n%s\n", boundary);
				http_receive->settings.on_body = http_receive_handle_mpart_body;

				http_receive->mpart_settings.on_header_field = http_receive_handle_mpart_header_field;
				http_receive->mpart_settings.on_header_value = http_receive_handle_mpart_header_value;
				http_receive->mpart_settings.on_part_data = http_receive_handle_mpart_data;

				http_receive->mpart_settings.on_part_data_begin = http_receive_handle_mpart_part_data_begin;
				http_receive->mpart_settings.on_headers_complete = http_receive_handle_mpart_headers_completed;
				http_receive->mpart_settings.on_part_data_end = http_receive_handle_mpart_data_end;
				http_receive->mpart_settings.on_body_end = NULL;

				http_receive->mpart_parser = multipart_parser_init (boundary, &http_receive->mpart_settings);
				http_receive->mpart_parser->data = http_receive;

				http_receive->request->multi_parts = dlist_init (http_multi_part_delete, NULL);

				free (boundary);
			}

			else {
				cerver_log_error ("Failed to get multipart boundary!");
			}
		}
	}

	return 0;

}

// TODO: handle authentication
static void http_receive_handle_serve_file (HttpReceive *http_receive) {

	bool found = false;

	HttpStaticPath *static_path = NULL;
	struct stat filestatus = { 0 };
	char filename[256] = { 0 };
	for (ListElement *le = dlist_start (http_receive->http_cerver->static_paths); le; le = le->next) {
		static_path = (HttpStaticPath *) le->data;

		(void) c_string_concat_safe (
			static_path->path->str, http_receive->request->url->str, 
			filename, 256
		);

		// check if file exists
		memset (&filestatus, 0, sizeof (struct stat));
		if (!stat (filename, &filestatus)) {
			// serve the file
			int file = open (filename, O_RDONLY);
			if (file) {
				http_response_send_file (http_receive->cr, file, filename, &filestatus);

				close (file);
			}

			found = true;
			break;
		}
	}

	if (!found) {
		// TODO: what to do here? - maybe something similar to catch all route
		char *status = c_string_create (
			"Unable to find file %s",
			http_receive->request->url->str
		);

		if (status) {
			cerver_log_warning (status);
			free (status);
		}

		http_receive_handle_default_route (http_receive->cr, http_receive->request);
	}

}

static int http_receive_handle_message_completed (http_parser *parser) {

	// printf ("\non message complete!\n");

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->request->method = (RequestMethod) http_receive->parser->method;

	#ifdef HTTP_DEBUG
	char *status = c_string_create (
		"%s %s",
		http_method_str ((enum http_method) http_receive->request->method),
		http_receive->request->url->str
	);

	if (status) {
		cerver_log_msg (stdout, LOG_TYPE_DEBUG, LOG_TYPE_HTTP, status);
		free (status);
	}
	#endif

	// select method handler
	switch (http_receive->request->method) {
		case REQUEST_METHOD_GET: {
			// check for file extension
			if (strrchr (http_receive->request->url->str, '.')) {
				// check if we can serve file from static paths
				if (http_receive->http_cerver->static_paths->size) {
					http_receive_handle_serve_file (http_receive);
				}

				// unable to serve a file, try for matching route
				else {
					http_receive_handle_select (http_receive, http_receive->cr, http_receive->request);
				}
			}

			else {
				http_receive_handle_select (http_receive, http_receive->cr, http_receive->request);
			}
		} break;

		default:
			http_receive_handle_select (http_receive, http_receive->cr, http_receive->request);
			break;
	}

	if (!http_receive->keep_alive) {
		connection_end (http_receive->cr->connection);
	}

	return 0;

}

static void http_receive_handle (
	HttpReceive *http_receive, 
	ssize_t rc, char *packet_buffer
) {

	ssize_t n_parsed = http_parser_execute (http_receive->parser, &http_receive->settings, packet_buffer, rc);
	if (n_parsed != rc) {
		char *status = c_string_create (
			"http_parser_execute () failed - n parsed %ld / received %ld",
			n_parsed, rc
		);

		if (status) {
			cerver_log_error (status);
			free (status);
		}

		// send back error message
		HttpResponse *res = http_response_json_error ((http_status) 500, "Internal error!");
		if (res) {
			// http_response_print (res);
			http_response_send (res, http_receive->cr->cerver, http_receive->cr->connection);
			http_respponse_delete (res);
		}

		connection_end (http_receive->cr->connection);
	}

	// printf ("http_receive_handle () - end!\n");

}

#pragma endregion

#pragma region websockets

// FIXME: handle msg_len >= 126 packets
// sends a ws message to the selected connection
// returns 0 on success, 1 on error
u8 http_web_sockets_send (
	Cerver *cerver, Connection *connection,
	const char *msg, const size_t msg_len
) {

	u8 retval = 1;

	unsigned char fin_rsv_opcode = 129;

	unsigned char res[128] = { 0 };

	res[0] = fin_rsv_opcode;
	res[1] = (unsigned char) msg_len;

	if (msg_len >= 126) {
		// TODO:
	}

	else {
		for (unsigned int i = 0; i < msg_len; i++) {
			res[i + 2] = msg[i];
		}
	}

	printf ("fin_rsv_opcode: %d\n", res[0]);
	printf ("res len: %d\n", res[1]);
	printf ("response: %s\n", res + 2);

	ssize_t sent = send (connection->socket->sock_fd, res, msg_len + 2, 0);
	if (sent > 0) retval = 0;

	printf ("sent: %ld\n", sent);
	printf ("\n");

	return retval;

}

// handle message based on control code
static void http_web_sockets_handler (
	HttpReceive *http_receive,
	unsigned char fin_rsv_opcode,
	char *message, size_t message_size
) {

	// if connection close
	if ((fin_rsv_opcode & 0x0f) == 8) {

	}

	// if ping
	else if ((fin_rsv_opcode & 0x0f) == 9) {
		printf ("\nPING!\n\n");
	}

	// if pong
	else if ((fin_rsv_opcode & 0x0f) == 10) {
		printf ("\nPONG\n\n");
	}

	// if fragmented message and not final fragment
	else if ((fin_rsv_opcode & 0x80) == 0) {

	}

	else {
		// printf ("message: %s\n", message);

		if (http_receive->ws_on_message) {
			http_receive->ws_on_message (
				http_receive->cr->cerver, http_receive->cr->connection,
				message, message_size
			);
		}

		// safe to delete message
		free (message);
	}

}

static void http_web_sockets_read_message_content (
	HttpReceive *http_receive,
	ssize_t buffer_size, char *buffer,
	unsigned char fin_rsv_opcode, size_t message_size
) {

	// read mask
	unsigned char mask[4] = { (unsigned char) buffer[0], (unsigned char) buffer[1], (unsigned char) buffer[2], (unsigned char) buffer[3] };

	char *message = NULL;

	// if fragmented message
	if ((fin_rsv_opcode & 0x80) == 0 || (fin_rsv_opcode & 0x0f) == 0) {
		if (!http_receive->fragmented_message) {
			http_receive->fin_rsv_opcode = fin_rsv_opcode;
			http_receive->fragmented_message = (char *) calloc (message_size, sizeof (char));
			http_receive->fragmented_message_len = message_size;
		}

		else {
			http_receive->fragmented_message_len += message_size;
		}

		message = http_receive->fragmented_message;
	}

	else {
		message = (char *) calloc (message_size, sizeof (char));
	}

	char *end = (buffer + 4);
	for (size_t c = 0; c < message_size; c++) {
		message[c] = *end ^ mask[c % 4];
		end += 1;
	}

	http_web_sockets_handler (
		http_receive,
		fin_rsv_opcode,
		message, message_size
	);

}

static void http_web_sockets_receive_handle (
	HttpReceive *http_receive, 
	ssize_t rc, char *packet_buffer
) {

	printf ("http_web_sockets_receive_handle ()\n");

	unsigned char first_bytes[2] = { (unsigned char) packet_buffer[0], (unsigned char) packet_buffer[1] };

	unsigned char fin_rsv_opcode = first_bytes[0];
	printf ("%d\n", first_bytes[0]);

	// close connection if unmasked message from client (protocol error)
	if (first_bytes[1] < 128) {
		cerver_log_error ("http_web_sockets_receive_handle () - message from client not masked!");
		// TODO: end connection

		return;
	}

	size_t length = (first_bytes[1] & 127);

	if (length == 126) {
		printf ("length == 126\n");

		// 2 next bytes is the size of content
		unsigned char length_bytes[2] = { (unsigned char) packet_buffer[2], (unsigned char) packet_buffer[3] };

		size_t length = 0;
		size_t num_bytes = 2;
		for (size_t c = 0; c < num_bytes; c++) {
			length += length_bytes[c] << (8 * (num_bytes - 1 - c));
		}

		printf ("length: %ld\n", length);

		http_web_sockets_read_message_content (
			http_receive, 
			rc - 4, packet_buffer + 4,
			fin_rsv_opcode, length
		);
	}

	else if (length == 127) {
		printf ("length == 127\n");

		// 8 next bytes is the size of content
		unsigned char length_bytes[8] = { 
			(unsigned char) packet_buffer[2], (unsigned char) packet_buffer[3],
			(unsigned char) packet_buffer[4], (unsigned char) packet_buffer[5],
			(unsigned char) packet_buffer[6], (unsigned char) packet_buffer[7],
			(unsigned char) packet_buffer[8], (unsigned char) packet_buffer[9],
		};

		size_t length = 0;
		size_t num_bytes = 8;
		for (size_t c = 0; c < num_bytes; c++) {
			length += length_bytes[c] << (8 * (num_bytes - 1 - c));
		}

		http_web_sockets_read_message_content (
			http_receive, 
			rc - 10, packet_buffer + 10,
			fin_rsv_opcode, length
		);
	}

	else {
		printf ("length: %ld\n", length);

		http_web_sockets_read_message_content (
			http_receive,
			rc - 2, packet_buffer + 2,
			fin_rsv_opcode, length
		);
	}

}

#pragma endregion