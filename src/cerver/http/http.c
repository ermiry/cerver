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

static void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

static void http_receive_handle (
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
			(void) memcpy (kvp->key->str, key_first, kvp->key->len);

			kvp->value = str_new (NULL);
			kvp->value->len = value_after - value_first;
			kvp->value->str = (char *) calloc (kvp->value->len + 1, sizeof (char));
			(void) memcpy (kvp->value->str, value_first, kvp->value->len);
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

			(void) printf ("[%d] - %s = %s\n", idx, kv->key->str, kv->value->str);

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

		cerver_log_success (
			"Loaded cerver's %s http jwt PRIVATE key (size %ld)!",
			http_cerver->cerver->info->name->str,
			http_cerver->jwt_private_key->len
		);

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to load cerver's %s http jwt PRIVATE key!",
			http_cerver->cerver->info->name->str
		);
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

		cerver_log_success (
			"Loaded cerver's %s http jwt PUBLIC key (size %ld)!",
			http_cerver->cerver->info->name->str,
			http_cerver->jwt_public_key->len
		);

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to load cerver's %s http jwt PUBLIC key!",
			http_cerver->cerver->info->name->str
		);
	}

	return retval;

}

static unsigned int http_cerver_init_load_jwt_keys (HttpCerver *http_cerver) {

	unsigned int errors = 0 ;

	if (http_cerver->jwt_opt_key_name) {
		cerver_log_msg ("Loading jwt PRIVATE key...");
		errors |= http_cerver_init_load_jwt_private_key (http_cerver);
	}

	if (http_cerver->jwt_opt_pub_key_name) {
		cerver_log_msg ("Loading jwt PUBLIC key...");
		errors |= http_cerver_init_load_jwt_public_key (http_cerver);
	}

	return errors;

}

void http_cerver_init (HttpCerver *http_cerver) {

	if (http_cerver) {
		cerver_log_msg ("Loading HTTP routes...");

		// init top level routes
		HttpRoute *route = NULL;
		for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
			route = (HttpRoute *) le->data;
			
			http_route_init (route);
			http_route_print (route);
		}

		// load jwt keys
		(void) http_cerver_init_load_jwt_keys (http_cerver);

		cerver_log_success ("Done loading HTTP routes!");
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
HttpStaticPath *http_cerver_static_path_add (
	HttpCerver *http_cerver, const char *static_path
) {

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
u8 http_receive_public_path_remove (
	HttpCerver *http_cerver, const char *static_path
) {

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

void http_cerver_set_catch_all_route (
	HttpCerver *http_cerver, 
	void (*catch_all_route)(
		const struct _HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver && catch_all_route) {
		http_cerver->default_handler = catch_all_route;
	}

}

#pragma endregion

#pragma region uploads

// sets the default uploads path where any multipart file request will be saved
// this method will replace the previous value with the new one
void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
) {

	if (http_cerver) {
		if (http_cerver->uploads_path) str_delete (http_cerver->uploads_path);
		http_cerver->uploads_path = uploads_path ? str_new (uploads_path) : NULL;
	}

}

// sets a method that should generate a c string to be used
// to save each incoming file of any multipart request
// the new filename should be placed in generated_filename
// with a max size of HTTP_MULTI_PART_GENERATED_FILENAME_LEN
void http_cerver_set_uploads_filename_generator (
	HttpCerver *http_cerver,
	void (*uploads_filename_generator)(
		const CerverReceive *,
		const char *original_filename,
		char *generated_filename
	)
) {

	if (http_cerver) {
		http_cerver->uploads_filename_generator = uploads_filename_generator;
	}

}

// sets a method to be called on every new request & that will be used to generate a new directory
// inside the uploads path to save all the files from each request
void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver,
	String *(*dirname_generator)(const CerverReceive *)
) {

	if (http_cerver) {
		http_cerver->uploads_dirname_generator = dirname_generator;
	}

}

#pragma endregion

#pragma region auth

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256
void http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, jwt_alg_t jwt_alg
) {

	if (http_cerver) http_cerver->jwt_alg = jwt_alg;

}

// sets the filename from where the jwt private key will be loaded
void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) http_cerver->jwt_opt_key_name = filename ? str_new (filename) : NULL;

}

// sets the filename from where the jwt public key will be loaded
void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) http_cerver->jwt_opt_pub_key_name = filename ? str_new (filename) : NULL;

}

// generates and signs a jwt token that is ready to be sent
// returns a newly allocated string that should be deleted after use
char *http_cerver_auth_generate_jwt (
	HttpCerver *http_cerver, DoubleList *values
) {

	char *token = NULL;

	jwt_t *jwt = NULL;
	if (!jwt_new (&jwt)) {
		time_t iat = time (NULL);

		KeyValuePair *kvp = NULL;
		for (ListElement *le = dlist_start (values); le; le = le->next) {
			kvp = (KeyValuePair *) le->data;
			(void) jwt_add_grant (jwt, kvp->key->str, kvp->value->str);
		}

		(void) jwt_add_grant_int (jwt, "iat", iat);

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

// returns TRUE if the jwt has been decoded and validate successfully
// returns FALSE if token is NOT valid or if an error has occurred
// option to pass the method to be used to create a decoded data
// that will be placed in the decoded data argument
bool http_cerver_auth_validate_jwt (
	HttpCerver *http_cerver, const char *bearer_token,
	void *(*decode_data)(void *), void **decoded_data
) {

	bool retval = false;

	if (http_cerver && bearer_token) {
		char *token = (char *) bearer_token;
		token += sizeof ("Bearer");

		jwt_t *jwt = NULL;
		jwt_valid_t *jwt_valid = NULL;
		if (!jwt_valid_new (&jwt_valid, http_cerver->jwt_alg)) {
			jwt_valid->hdr = 1;
			jwt_valid->now = time (NULL);

			if (!jwt_decode (
				&jwt, token,
				(unsigned char *) http_cerver->jwt_public_key->str,
				http_cerver->jwt_public_key->len
			)) {
				#ifdef HTTP_AUTH_DEBUG
				cerver_log_debug ("JWT decoded successfully!");
				#endif

				if (!jwt_validate (jwt, jwt_valid)) {
					#ifdef HTTP_AUTH_DEBUG
					cerver_log_success ("JWT is authentic!");
					#endif

					if (decode_data) {
						*decoded_data = decode_data (jwt->grants);
					}

					retval = true;
				}

				else {
					#ifdef HTTP_AUTH_DEBUG
					cerver_log_error (
						"Failed to validate JWT: %08x", 
						jwt_valid_get_status(jwt_valid)
					);
					#endif
				}

				jwt_free (jwt);
			}

			else {
				#ifdef HTTP_AUTH_DEBUG
				cerver_log_error ("Invalid JWT!");
				#endif
			}
		}

		jwt_valid_free (jwt_valid);
	}

	return retval;

}

#pragma endregion

#pragma region stats

static size_t http_cerver_stats_get_children_routes (
	const HttpCerver *http_cerver, size_t *handlers
) {

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
void http_cerver_routes_stats_print (const HttpCerver *http_cerver) {

	if (http_cerver) {
		cerver_log_msg ("Main routes: %ld", http_cerver->routes->size);

		size_t total_handlers = 0;
		size_t children_routes = http_cerver_stats_get_children_routes (
			http_cerver, &total_handlers
		);

		cerver_log_msg ("Children routes: %ld", children_routes);
		cerver_log_msg ("Total handlers: %ld", total_handlers);
	}

}

static void http_cerver_route_handler_stats_print (
	const RequestMethod method, const HttpRouteStats *stats
) {

	cerver_log_msg (
		"\t\t%s\t%ld",
		http_request_method_str (method),
		stats->n_requests
	);

	// route stats
	cerver_log_msg ("\t\tMin process time: %.4f secs", stats->first ? 0 : stats->min_process_time);
	cerver_log_msg ("\t\tMax process time: %.4f secs", stats->max_process_time);
	cerver_log_msg ("\t\tMean process time: %.4f secs", stats->mean_process_time);

	cerver_log_msg ("\t\tMin request size: %ld", stats->first ? 0 : stats->min_request_size);
	cerver_log_msg ("\t\tMax request size: %ld", stats->max_request_size);
	cerver_log_msg ("\t\tMean request size: %ld", stats->mean_request_size);

	cerver_log_msg ("\t\tMin response size: %ld", stats->first ? 0 : stats->min_response_size);
	cerver_log_msg ("\t\tMax response size: %ld", stats->max_response_size);
	cerver_log_msg ("\t\tMean response size: %ld", stats->mean_response_size);

	

}

static void http_cerver_route_file_stats_print (
	const HttpRouteFileStats *file_stats
) {

	cerver_log_line_break ();
	cerver_log_msg ("\t\tUploaded files: %ld", file_stats->n_uploaded_files);

	cerver_log_msg ("\t\tMin n files: %ld", file_stats->first ? 0 : file_stats->min_n_files);
	cerver_log_msg ("\t\tMax n files: %ld", file_stats->max_n_files);
	cerver_log_msg ("\t\tMean n files: %.2f", file_stats->mean_n_files);

	cerver_log_msg ("\t\tMin file size: %ld", file_stats->first ? 0 : file_stats->min_file_size);
	cerver_log_msg ("\t\tMax file size: %ld", file_stats->max_file_size);
	cerver_log_msg ("\t\tMean file size: %ld", file_stats->mean_file_size);

}

static void http_cerver_child_route_handler_stats_print (
	const RequestMethod method, const HttpRouteStats *stats
) {

	cerver_log_msg (
		"\t\t\t%s\t%ld",
		http_request_method_str (method),
		stats->n_requests
	);

	cerver_log_msg ("\t\t\tMin process time: %.4f secs", stats->first ? 0 : stats->min_process_time);
	cerver_log_msg ("\t\t\tMax process time: %.4f secs", stats->max_process_time);
	cerver_log_msg ("\t\t\tMean process time: %.4f secs", stats->mean_process_time);

	cerver_log_msg ("\t\t\tMin request size: %ld", stats->first ? 0 : stats->min_request_size);
	cerver_log_msg ("\t\t\tMax request size: %ld", stats->max_request_size);
	cerver_log_msg ("\t\t\tMean request size: %ld", stats->mean_request_size);

	cerver_log_msg ("\t\t\tMin response size: %ld", stats->first ? 0 : stats->min_response_size);
	cerver_log_msg ("\t\t\tMax response size: %ld", stats->max_response_size);
	cerver_log_msg ("\t\t\tMean response size: %ld", stats->mean_response_size);

}

static void http_cerver_child_route_file_stats_print (
	const HttpRouteFileStats *file_stats
) {

	cerver_log_msg ("\t\t\tUploaded files: %ld", file_stats->n_uploaded_files);

	cerver_log_msg ("\t\t\tMin n files: %ld", file_stats->first ? 0 : file_stats->min_n_files);
	cerver_log_msg ("\t\t\tMax n files: %ld", file_stats->max_n_files);
	cerver_log_msg ("\t\t\tMean n files: %.2f", file_stats->mean_n_files);

	cerver_log_msg ("\t\t\tMin file size: %ld", file_stats->first ? 0 : file_stats->min_file_size);
	cerver_log_msg ("\t\t\tMax file size: %ld", file_stats->max_file_size);
	cerver_log_msg ("\t\t\tMean file size: %ld", file_stats->mean_file_size);

}

static void http_cerver_route_children_stats_print (
	const HttpRoute *route
) {

	cerver_log_msg ("\t\t%ld children:", route->children->size);

	RequestMethod method = REQUEST_METHOD_UNDEFINED;

	HttpRoute *child = NULL;
	for (ListElement *le = dlist_start (route->children); le; le = le->next) {
		child = (HttpRoute *) le->data;

		cerver_log_msg ("\t\t%s:", child->actual->str);

		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (child->handlers[i]) {
				method = (RequestMethod) i;
				http_cerver_child_route_handler_stats_print (
					method, child->stats[i]
				);

				if (
					(child->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
					&& (method == REQUEST_METHOD_POST)
				) {
					http_cerver_child_route_file_stats_print (
						child->file_stats
					);
				}
			}
		}
	}

}

// print route's stats
void http_cerver_route_stats_print (const HttpRoute *route) {

	if (route) {
		cerver_log_msg ("\t%s:", route->route->str);
		
		RequestMethod method = REQUEST_METHOD_UNDEFINED;
		for (unsigned int i = 0; i < HTTP_HANDLERS_COUNT; i++) {
			if (route->handlers[i]) {
				method = (RequestMethod) i;
				http_cerver_route_handler_stats_print (
					method, route->stats[i]
				);

				if (
					(route->modifier == HTTP_ROUTE_MODIFIER_MULTI_PART)
					&& (method == REQUEST_METHOD_POST)
				) {
					http_cerver_route_file_stats_print (
						route->file_stats
					);
				}
			}
		}

		if (route->children->size) {
			http_cerver_route_children_stats_print (route);
		}

		else {
			cerver_log_msg ("\t\tNo children");
		}
	}

}

// print all http cerver stats, general & by route
void http_cerver_all_stats_print (const HttpCerver *http_cerver) {

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

static String *http_strip_path_from_query (
	const char *url, size_t url_len
) {

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

DoubleList *http_parse_query_into_pairs (
	const char *first, const char *last
) {

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

static int http_receive_handle_url (
	http_parser *parser, const char *at, size_t length
) {

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

static int http_receive_handle_header_field (
	http_parser *parser, const char *at, size_t length
) {

	char header[32] = { 0 };
	(void) snprintf (header, 32, "%.*s", (int) length, at);
	// printf ("\nHeader field: /%.*s/\n", (int) length, at);

	(((HttpReceive *) parser->data)->request)->next_header = http_receive_handle_header_field_handle (header);

	return 0;

}

static int http_receive_handle_header_value (
	http_parser *parser, const char *at, size_t length
) {

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

static int http_receive_handle_body (
	http_parser *parser, const char *at, size_t length
) {

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

	char *pair = NULL, *name = NULL, *value = NULL;
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
static inline MultiPartHeader http_receive_handle_mpart_header_field_handle (
	const char *header
) {

	if (!strcasecmp ("Content-Disposition", header)) return MULTI_PART_HEADER_CONTENT_DISPOSITION;
	if (!strcasecmp ("Content-Length", header)) return MULTI_PART_HEADER_CONTENT_LENGTH;
	if (!strcasecmp ("Content-Type", header)) return MULTI_PART_HEADER_CONTENT_TYPE;

	return MULTI_PART_HEADER_INVALID;		// no known header

}

static int http_receive_handle_mpart_header_field (
	multipart_parser *parser, const char *at, size_t length
) {

	char header[32] = { 0 };
	(void) snprintf (header, 32, "%.*s", (int) length, at);
	// printf ("\nHeader field: /%.*s/\n", (int) length, at);

	(((HttpReceive *) parser->data)->request)->current_part->next_header = http_receive_handle_mpart_header_field_handle (header);

	return 0;

}

static int http_receive_handle_mpart_header_value (
	multipart_parser *parser, const char *at, size_t length
) {

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

	#ifdef HTTP_MPART_DEBUG
	http_multi_part_headers_print (multi_part);
	#endif

	if (multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION]) {
		if (c_string_starts_with (multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION]->str, "form-data;")) {
			char *end = (char *) multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION]->str;
			end += strlen ("form-data;");

			multi_part->params = http_mpart_attributes_parse (end);
			// key_value_pairs_print (multi_part->params);

			multi_part->name = key_value_pairs_get_value (multi_part->params, "name");
			// multi_part->filename = key_value_pairs_get_value (multi_part->params, "filename");
			const String *original_filename = key_value_pairs_get_value (
				multi_part->params, "filename"
			);

			if (original_filename) {
				// stats
				http_receive->file_stats->n_uploaded_files += 1;

				// sanitize file
				(void) strncpy (
					multi_part->filename, original_filename->str, HTTP_MULTI_PART_FILENAME_LEN
				);

				files_sanitize_filename (multi_part->filename);

				http_receive->request->n_files += 1;

				if (http_receive->http_cerver->uploads_path) {
					if (http_receive->request->dirname) {
						if (http_receive->http_cerver->uploads_filename_generator) {
							http_receive->http_cerver->uploads_filename_generator (
								http_receive->cr,
								multi_part->filename,
								multi_part->generated_filename
							);

							(void) snprintf (
								multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_LEN,
								"%s/%s/%s",
								http_receive->http_cerver->uploads_path->str,
								http_receive->request->dirname->str,
								multi_part->generated_filename
							);
						}

						else {
							(void) snprintf (
								multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_LEN,
								"%s/%s/%s",
								http_receive->http_cerver->uploads_path->str,
								http_receive->request->dirname->str,
								multi_part->filename
							);
						}
					}

					else {
						if (http_receive->http_cerver->uploads_filename_generator) {
							http_receive->http_cerver->uploads_filename_generator (
								http_receive->cr,
								multi_part->filename,
								multi_part->generated_filename
							);

							(void) snprintf (
								multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_LEN,
								"%s/%s",
								http_receive->http_cerver->uploads_path->str,
								multi_part->generated_filename
							);
						}

						else {
							(void) snprintf (
								multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_LEN,
								"%s/%s",
								http_receive->http_cerver->uploads_path->str,
								multi_part->filename
							);
						}
					}

					multi_part->fd = open (multi_part->saved_filename, O_CREAT | O_WRONLY, 0777);
					switch (multi_part->fd) {
						case -1: {
							cerver_log_error (
								"Failed to open %s filename to save multipart file!",
								multi_part->saved_filename
							);
							perror ("Error");
						} break;

						default: {
							#ifdef HTTP_MPART_DEBUG
							cerver_log_debug (
								"Opened %s to save multipart file!",
								multi_part->saved_filename
							);
							#endif
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
	}

	return 0;

}

static int http_receive_handle_mpart_data (
	multipart_parser *parser, const char *at, size_t length
) {

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

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;

	if (multi_part->fd != -1) {
		(void) close (multi_part->fd);
	}

	if (multi_part->total_wrote < http_receive->file_stats->min_file_size)
		http_receive->file_stats->min_file_size = multi_part->total_wrote;

	if (multi_part->total_wrote > http_receive->file_stats->max_file_size)
		http_receive->file_stats->max_file_size = multi_part->total_wrote;

	http_receive->file_stats->sum_file_size += multi_part->total_wrote;

	return 0;

}

static int http_receive_handle_mpart_body (
	http_parser *parser, const char *at, size_t length
) {

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

		http_receive->route = NULL;
		http_receive->request_method = REQUEST_METHOD_DELETE;

		http_receive->status = HTTP_STATUS_NONE;
		http_receive->sent = 0;

		http_receive->file_stats = http_route_file_stats_new ();

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

		http_receive->route = NULL;
		http_route_file_stats_delete (http_receive->file_stats);

		if (http_receive->mpart_parser) multipart_parser_free (http_receive->mpart_parser);

		free (http_receive);
	}

}

static void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	HttpResponse *res = http_response_json_msg (HTTP_STATUS_OK, "HTTP Cerver!");
	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

// catch all mismatches and handle with cath all route
static void http_receive_handle_catch_all (
	HttpCerver *http_cerver, HttpReceive *http_receive, HttpRequest *request
) {

	cerver_log_warning (
		"No matching route for %s %s",
		http_method_str ((enum http_method) request->method),
		request->url->str
	);

	// handle with default route
	http_cerver->default_handler (http_receive, request);

	http_cerver->n_cath_all_requests += 1;

}

// handles an actual route match & selects the right handler based on the request's method
static void http_receive_handle_match (
	HttpCerver *http_cerver, 
	HttpReceive *http_receive,
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
				case HTTP_ROUTE_MODIFIER_NONE:
				case HTTP_ROUTE_MODIFIER_MULTI_PART:
					found->handlers[request->method] (http_receive, request);
					break;

				case HTTP_ROUTE_MODIFIER_WEB_SOCKET:
					
					break;
			}

			// 25/11/2020 - handled by http_route_stats_update ()
			// found->stats[request->method]->n_requests += 1;
		}

		else {
			http_receive_handle_catch_all (http_cerver, http_receive, request);
		}
	}

	else {
		http_receive_handle_catch_all (http_cerver, http_receive, request);
	}

}

static inline bool http_receive_handle_select_children (
	HttpRoute *route, HttpRequest *request, HttpRoute **found
) {

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
							// printf ("%s\n", routes_tokens->tokens[main_idx][sub_idx]);
							if (routes_tokens->tokens[main_idx][sub_idx][0] != '*') {
								if (strcmp (routes_tokens->tokens[main_idx][sub_idx], tokens[sub_idx])) {
									// printf ("fail!\n");
									fail = true;
									break;
								}
							}

							// else {
							// 	printf ("*\n");
							// }
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

static void http_receive_handle_select_failed_auth (HttpReceive *http_receive) {

	HttpResponse *res = http_response_json_error (HTTP_STATUS_UNAUTHORIZED, "Failed to authenticate!");
	if (res) {
		http_response_print (res);
		http_response_send (res, http_receive);
		http_respponse_delete (res);
	}

}

static void http_receive_handle_select_auth_bearer (
	HttpCerver *http_cerver, 
	HttpReceive *http_receive,
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

		if (!jwt_decode (&jwt, token, (unsigned char *) http_cerver->jwt_public_key->str, http_cerver->jwt_public_key->len)) {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_debug ("JWT decoded successfully!");
			#endif

			if (!jwt_validate (jwt, jwt_valid)) {
				#ifdef HTTP_AUTH_DEBUG
				cerver_log_success ("JWT is authentic!");
				#endif

				if (found->decode_data) {
					request->decoded_data = found->decode_data (jwt->grants);
					request->delete_decoded_data = found->delete_decoded_data;
				}

				http_receive_handle_match (
					http_cerver,
					http_receive,
					request,
					found
				);
			}

			else {
				#ifdef HTTP_AUTH_DEBUG
				cerver_log_error (
					"Failed to validate JWT: %08x", 
					jwt_valid_get_status(jwt_valid)
				);
				#endif

				http_receive_handle_select_failed_auth (http_receive);

				http_cerver->n_failed_auth_requests += 1;
			}

			jwt_free (jwt);
		}

		else {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_error ("Invalid JWT!");
			#endif

			http_receive_handle_select_failed_auth (http_receive);
			http_cerver->n_failed_auth_requests += 1;
		}
	}

	jwt_valid_free (jwt_valid);

}

// select the route that will handle the request
static void http_receive_handle_select (
	HttpReceive *http_receive, HttpRequest *request
) {

	HttpCerver *http_cerver = (HttpCerver *) http_receive->cr->cerver->cerver_data;

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
		http_receive->route = found;
		http_receive->request_method = request->method;

		switch (found->auth_type) {
			// no authentication, handle the request directly
			case HTTP_ROUTE_AUTH_TYPE_NONE: {
				http_receive_handle_match (
					http_cerver,
					http_receive,
					request,
					found
				);
			} break;

			// handle authentication with bearer token
			case HTTP_ROUTE_AUTH_TYPE_BEARER: {
				if (request->headers[REQUEST_HEADER_AUTHORIZATION]) {
					http_receive_handle_select_auth_bearer (http_cerver, http_receive, found, request);
				}

				// no authentication header was provided
				else {
					http_receive_handle_select_failed_auth (http_receive);
					http_cerver->n_failed_auth_requests += 1;
				}
			} break;
		}
	}

	else {
		http_receive_handle_catch_all (http_cerver, http_receive, request);
	}

}

static int http_receive_handle_headers_completed (http_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	#ifdef HTTP_HEADERS_DEBUG
	http_request_headers_print (http_receive->request);
	#endif

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

				// TODO: handler errors
				if (http_receive->http_cerver->uploads_dirname_generator) {
					if (http_receive->http_cerver->uploads_path) {
						http_receive->request->dirname = http_receive->http_cerver->uploads_dirname_generator (http_receive->cr);
						char dirname[512] = { 0 };
						(void) snprintf (
							dirname, 512,
							"%s/%s",
							http_receive->http_cerver->uploads_path->str, http_receive->request->dirname->str
						);

						(void) files_create_dir (dirname, 0777);
					}
				}

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
		(void) memset (&filestatus, 0, sizeof (struct stat));
		if (!stat (filename, &filestatus)) {
			// serve the file
			int file = open (filename, O_RDONLY);
			if (file) {
				http_response_send_file (
					http_receive, file, filename, &filestatus
				);

				close (file);
			}

			found = true;
			break;
		}
	}

	if (!found) {
		// TODO: what to do here? - maybe something similar to catch all route
		cerver_log_warning (
			"Unable to find file %s",
			http_receive->request->url->str
		);

		http_receive_handle_default_route (http_receive, http_receive->request);
	}

}

static int http_receive_handle_message_completed (http_parser *parser) {

	// printf ("\non message complete!\n");

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->request->method = (RequestMethod) http_receive->parser->method;

	#ifdef HTTP_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HTTP,
		"%s %s",
		http_method_str ((enum http_method) http_receive->request->method),
		http_receive->request->url->str
	);
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
					http_receive_handle_select (http_receive, http_receive->request);
				}
			}

			else {
				http_receive_handle_select (http_receive, http_receive->request);
			}
		} break;

		default:
			http_receive_handle_select (http_receive, http_receive->request);
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
		cerver_log_error (
			"http_parser_execute () failed - n parsed %ld / received %ld",
			n_parsed, rc
		);

		// send back error message
		HttpResponse *res = http_response_json_error ((http_status) 500, "Internal error!");
		if (res) {
			// http_response_print (res);
			http_response_send (res, http_receive);
			http_respponse_delete (res);
		}

		connection_end (http_receive->cr->connection);
	}

	// printf ("http_receive_handle () - end!\n");

}

#pragma endregion