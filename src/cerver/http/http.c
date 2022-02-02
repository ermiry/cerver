#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdarg.h>

#include <ctype.h>
#include <time.h>

#include <openssl/sha.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/pool.h"

#include "cerver/files.h"
#include "cerver/handler.h"
#include "cerver/packets.h"

#include "cerver/threads/worker.h"

#include "cerver/http/admin.h"
#include "cerver/http/auth.h"
#include "cerver/http/custom.h"
#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/http_parser.h"
#include "cerver/http/method.h"
#include "cerver/http/multipart.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/route.h"
#include "cerver/http/url.h"

#include "cerver/http/json/json.h"

#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

HttpResponse *oki_doki = NULL;
HttpResponse *bad_request_error = NULL;
HttpResponse *bad_user_error = NULL;
HttpResponse *bad_auth_error = NULL;
HttpResponse *not_found_error = NULL;
HttpResponse *server_error = NULL;

static HttpResponse *catch_all = NULL;

static Pool *multi_parts_pool = NULL;

static const char *multi_part_header_value = { "multipart/form-data;" };
static const size_t multi_part_header_value_len = 20;

static const char *multi_part_form_data = { "form-data;" };
static const size_t multi_part_form_data_len = 10;

static const char *webkit_multi_part_boundary_value = { "------WebKitFormBoundary" };
static const size_t webkit_multi_part_boundary_value_len = 24;

static void http_static_path_delete (void *http_static_path_ptr);

static int http_static_path_comparator (const void *a, const void *b);

static void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

static void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

static unsigned int http_multi_parts_init_pool (void);

static void http_receive_init_mpart_parser (
	HttpReceive *http_receive,
	const char *boundary
);

static void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
);

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

KeyValuePair *key_value_pair_create (
	const char *key, const char *value
) {

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

const String *key_value_pairs_get_value (
	const DoubleList *pairs, const char *key
) {

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

void key_value_pairs_print (const DoubleList *pairs) {

	if (pairs) {
		unsigned int idx = 1;
		KeyValuePair *kv = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			kv = (KeyValuePair *) le->data;

			(void) printf (
				"[%u] - %s = %s\n",
				idx, kv->key->str, kv->value->str
			);

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

		http_cerver->main_route = NULL;

		http_cerver->default_handler = NULL;

		http_cerver->not_found_handler = false;
		http_cerver->not_found = NULL;

		http_cerver->uploads_path_len = 0;
		(void) memset (http_cerver->uploads_path, 0, HTTP_CERVER_UPLOADS_PATH_SIZE);

		http_cerver->uploads_file_mode = HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE;
		http_cerver->uploads_filename_generator = NULL;

		http_cerver->uploads_dir_mode = HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE;
		http_cerver->uploads_dirname_generator = NULL;

		http_cerver->uploads_delete_when_done = HTTP_CERVER_DEFAULT_UPLOADS_DELETE;

		http_cerver->jwt_alg = JWT_ALG_NONE;

		http_cerver->jwt_opt_key_name = NULL;
		http_cerver->jwt_private_key = NULL;

		http_cerver->jwt_opt_pub_key_name = NULL;
		http_cerver->jwt_public_key = NULL;

		http_cerver->n_origins = 0;
		for (u8 i = 0; i < HTTP_ORIGINS_SIZE; i++)
			http_origin_reset (&http_cerver->origins_whitelist[i]);

		http_cerver->custom_data = NULL;
		http_cerver->delete_custom_data = NULL;

		http_cerver->n_response_headers = 0;
		(void) memset (
			http_cerver->response_headers, 0,
			HTTP_HEADERS_SIZE * sizeof (HttpHeader)
		);

		http_cerver->n_incompleted_requests = 0;
		http_cerver->n_unhandled_requests = 0;

		http_cerver->n_catch_all_requests = 0;
		http_cerver->n_failed_auth_requests = 0;

		http_cerver->enable_admin_routes = HTTP_CERVER_DEFAULT_ENABLE_ADMIN;
		http_cerver->enable_admin_info_route = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO;
		http_cerver->enable_admin_head_handlers = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS;
		http_cerver->enable_admin_options_handlers = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS;

		http_cerver->enable_admin_routes_auth = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH;
		http_cerver->admin_auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;
		http_cerver->admin_decode_data = NULL;
		http_cerver->admin_delete_decoded_data = NULL;
		http_cerver->admin_auth_handler = NULL;

		http_cerver->enable_admin_cors_headers = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS;
		http_origin_reset (&http_cerver->admin_origin);

		http_cerver->admin_file_systems_stats = NULL;
		http_cerver->admin_workers = NULL;
		http_cerver->admin_mutex = NULL;

		http_cerver->mutex = NULL;
	}

	return http_cerver;

}

void http_cerver_delete (void *http_cerver_ptr) {

	if (http_cerver_ptr) {
		HttpCerver *http_cerver = (HttpCerver *) http_cerver_ptr;

		dlist_delete (http_cerver->static_paths);

		dlist_delete (http_cerver->routes);

		str_delete (http_cerver->jwt_opt_key_name);
		str_delete (http_cerver->jwt_private_key);

		str_delete (http_cerver->jwt_opt_pub_key_name);
		str_delete (http_cerver->jwt_public_key);

		if (http_cerver->custom_data) {
			if (http_cerver->delete_custom_data) {
				http_cerver->delete_custom_data (
					http_cerver->custom_data
				);
			}
		}

		dlist_delete (http_cerver->admin_file_systems_stats);
		dlist_delete (http_cerver->admin_workers);
		thread_mutex_delete (http_cerver->admin_mutex);

		thread_mutex_delete (http_cerver->mutex);

		free (http_cerver_ptr);
	}

}

HttpCerver *http_cerver_get (Cerver *cerver) {

	return cerver ? (HttpCerver *) cerver->cerver_data : NULL;

}

HttpCerver *http_cerver_create (Cerver *cerver) {

	HttpCerver *http_cerver = http_cerver_new ();
	if (http_cerver) {
		http_cerver->cerver = cerver;

		http_cerver->static_paths = dlist_init (
			http_static_path_delete, http_static_path_comparator
		);

		http_cerver->routes = dlist_init (http_route_delete, NULL);

		http_cerver->default_handler = http_receive_handle_default_route;

		http_cerver->not_found = http_receive_handle_not_found_route;

		http_cerver->jwt_alg = JWT_DEFAULT_ALG;

		http_cerver->admin_file_systems_stats = dlist_init (
			http_admin_file_system_stats_delete, NULL
		);

		http_cerver->admin_workers = dlist_init (NULL, NULL);

		http_cerver->admin_mutex = thread_mutex_new ();

		http_cerver->mutex = thread_mutex_new ();
	}

	return http_cerver;

}

static unsigned int http_cerver_init_responses (void) {

	unsigned int retval = 1;

	oki_doki = http_response_create_json_key_value (
		HTTP_STATUS_OK, "oki", "doki"
	);

	bad_request_error = http_response_create_json_key_value (
		HTTP_STATUS_BAD_REQUEST, "error", "Bad request!"
	);

	bad_user_error = http_response_create_json_key_value (
		HTTP_STATUS_BAD_REQUEST, "error", "Bad user!"
	);

	bad_auth_error = http_response_create_json_key_value (
		HTTP_STATUS_UNAUTHORIZED, "error", "Failed to authenticate!"
	);

	not_found_error = http_response_create_json_key_value (
		HTTP_STATUS_NOT_FOUND, "error", "Not found!"
	);

	server_error = http_response_create_json_key_value (
		HTTP_STATUS_INTERNAL_SERVER_ERROR, "error", "Internal error!"
	);

	catch_all = http_response_create_json_key_value (
		HTTP_STATUS_OK, "msg", "HTTP Cerver!"
	);

	if (
		oki_doki
		&& bad_request_error && bad_user_error
		&& bad_auth_error && not_found_error
		&& server_error
		&& catch_all
	) retval = 0;

	return retval;

}

static unsigned int http_cerver_init_load_jwt_private_key (
	HttpCerver *http_cerver
) {

	unsigned int retval = 1;

	size_t private_keylen = 0;
	char *private_key = file_read (
		http_cerver->jwt_opt_key_name->str, &private_keylen
	);

	if (private_key) {
		http_cerver->jwt_private_key = str_new (NULL);
		http_cerver->jwt_private_key->str = private_key;
		http_cerver->jwt_private_key->len = private_keylen;

		// printf ("\n%s\n", http_cerver->jwt_private_key->str);

		cerver_log_success (
			"Loaded cerver's %s http jwt PRIVATE key (size %ld)!",
			http_cerver->cerver->info->name,
			http_cerver->jwt_private_key->len
		);

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to load cerver's %s http jwt PRIVATE key!",
			http_cerver->cerver->info->name
		);
	}

	return retval;

}

static unsigned int http_cerver_init_load_jwt_public_key (
	HttpCerver *http_cerver
) {

	unsigned int retval = 1;

	size_t public_keylen = 0;
	char *public_key = file_read (
		http_cerver->jwt_opt_pub_key_name->str, &public_keylen
	);

	if (public_key) {
		http_cerver->jwt_public_key = str_new (NULL);
		http_cerver->jwt_public_key->str = public_key;
		http_cerver->jwt_public_key->len = public_keylen;

		// printf ("\n%s\n", http_cerver->jwt_public_key->str);

		cerver_log_success (
			"Loaded cerver's %s http jwt PUBLIC key (size %ld)!",
			http_cerver->cerver->info->name,
			http_cerver->jwt_public_key->len
		);

		retval = 0;
	}

	else {
		cerver_log_error (
			"Failed to load cerver's %s http jwt PUBLIC key!",
			http_cerver->cerver->info->name
		);
	}

	return retval;

}

static unsigned int http_cerver_init_load_jwt_keys (
	HttpCerver *http_cerver
) {

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

static unsigned int http_cerver_init_internal (
	HttpCerver *http_cerver
) {

	unsigned int errors = 0;

	#ifdef HTTP_DEBUG
	cerver_log_msg ("Initializing HTTP cerver...");
	#endif

	// init HTTP jwt pool
	errors |= http_jwt_init_pool ();

	// load jwt keys
	errors |= http_cerver_init_load_jwt_keys (http_cerver);

	// init HTTP responses pool
	errors |= http_responses_init (http_cerver);

	// init common responses
	errors |= http_cerver_init_responses ();

	// init multi-parts pool
	errors |= http_multi_parts_init_pool ();

	return errors;

}

static void http_cerver_init_routes (
	HttpCerver *http_cerver
) {

	#ifdef HTTP_DEBUG
	cerver_log_msg ("Loading HTTP routes...");
	#endif

	if (!http_cerver->main_route) {
		http_cerver->main_route = (HttpRoute *) dlist_start (http_cerver->routes)->data;
	}

	if (http_cerver->enable_admin_routes) {
		(void) http_cerver_admin_init (
			http_cerver,
			(HttpRoute *) http_cerver->main_route
		);
	}

	// init top level routes
	HttpRoute *route = NULL;
	for (
		ListElement *le = dlist_start (http_cerver->routes);
		le; le = le->next
	) {
		route = (HttpRoute *) le->data;

		http_route_init (route);
		http_route_print (route);
	}

	#ifdef HTTP_DEBUG
	cerver_log_success ("Done loading HTTP routes!");
	#endif

}

u8 http_cerver_init (HttpCerver *http_cerver) {

	u8 retval = 1;

	if (http_cerver) {
		if (!http_cerver_init_internal (http_cerver)) {
			if (dlist_size (http_cerver->routes)) {
				http_cerver_init_routes (http_cerver);
			}

			retval = 0;
		}
	}

	return retval;

}

// destroy values allocated in http_cerver_init ()
void http_cerver_end (HttpCerver *http_cerver) {

	if (http_cerver) {
		http_responses_end ();

		http_jwt_end_pool ();

		http_response_delete (oki_doki);
		http_response_delete (bad_request_error);
		http_response_delete (bad_user_error);
		http_response_delete (bad_auth_error);
		http_response_delete (not_found_error);
		http_response_delete (server_error);
		http_response_delete (catch_all);
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
void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
) {

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

// sets the HTTP cerver's main route (top level route)
// used to enable admin routes
// if no route has been set, the first top level route
// will be used
void http_cerver_set_main_route (
	HttpCerver *http_cerver, const HttpRoute *route
) {

	if (http_cerver) {
		http_cerver->main_route = route;
	}

}

void http_cerver_route_register (
	HttpCerver *http_cerver, HttpRoute *route
) {

	if (http_cerver && route) {
		(void) dlist_insert_after (
			http_cerver->routes,
			dlist_end (http_cerver->routes),
			route
		);
	}

}

void http_cerver_set_catch_all_route (
	HttpCerver *http_cerver,
	void (*catch_all_route)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver && catch_all_route) {
		http_cerver->default_handler = catch_all_route;
	}

}

// enables the use of the default not found handler
// which returns 404 status on no matching routes
// by default this option is disabled
// as the catch all handler is used instead
void http_cerver_set_not_found_handler (
	HttpCerver *http_cerver
) {

	if (http_cerver)
		http_cerver->not_found_handler = true;

}

// sets a custom handler to be executed on no matching routes
// it should return status 404
void http_cerver_set_not_found_route (
	HttpCerver *http_cerver,
	void (*not_found)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver && not_found) {
		http_cerver->not_found = not_found;
		http_cerver->not_found_handler = true;
	}

}

#pragma endregion

#pragma region uploads

// sets the default uploads path where any multipart file will be saved
// this method will replace the previous value with the new one
void http_cerver_set_uploads_path (
	HttpCerver *http_cerver, const char *uploads_path
) {

	if (http_cerver && uploads_path) {
		(void) strncpy (
			http_cerver->uploads_path,
			uploads_path,
			HTTP_CERVER_UPLOADS_PATH_SIZE - 1
		);

		http_cerver->uploads_path_len = (int) strlen (
			http_cerver->uploads_path
		);
	}

}

// works like http_cerver_set_uploads_path () but can generate
// a custom path on the fly using variable arguments
void http_cerver_generate_uploads_path (
	HttpCerver *http_cerver, const char *format, ...
) {

	if (http_cerver && format) {
		va_list args;
		va_start (args, format);

		http_cerver->uploads_path_len = vsnprintf (
			http_cerver->uploads_path, HTTP_CERVER_UPLOADS_PATH_SIZE - 1,
			format, args
		);

		va_end (args);
	}

}

// sets the mode_t to be used when creating uploads files
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_FILE_MODE
void http_cerver_set_uploads_file_mode (
	HttpCerver *http_cerver, const unsigned int file_mode
) {

	if (http_cerver) {
		http_cerver->uploads_file_mode = file_mode;
	}

}

// method that can be used to generate multi-part uploads filenames
// with format "%d-%ld-%s" using "sock_fd-time (NULL)-multi_part->filename"
void http_cerver_default_uploads_filename_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const MultiPart *mpart = http_request_get_current_mpart (request);

	http_multi_part_set_generated_filename (
		mpart,
		"%d-%ld-%s",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL),
		http_multi_part_get_filename (mpart)
	);

}

// sets a method that should generate a c string to be used
// to save each incoming file of any multipart request
// the new filename should be placed in generated_filename
// with a max size of HTTP_MULTI_PART_GENERATED_FILENAME_SIZE
void http_cerver_set_uploads_filename_generator (
	HttpCerver *http_cerver,
	void (*uploads_filename_generator)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver) {
		http_cerver->uploads_filename_generator = uploads_filename_generator;
	}

}

// sets the HTTP cerver's uploads filename generator
// to be http_cerver_default_uploads_filename_generator ()
void http_cerver_set_default_uploads_filename_generator (
	HttpCerver *http_cerver
) {

	if (http_cerver) {
		http_cerver->uploads_filename_generator = http_cerver_default_uploads_filename_generator;
	}

}

// sets the mode_t value to be used when creating uploads dirs
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DIR_MODE
void http_cerver_set_uploads_dir_mode (
	HttpCerver *http_cerver, const unsigned int dir_mode
) {

	if (http_cerver) {
		http_cerver->uploads_dir_mode = dir_mode;
	}

}

// method that can be used to generate multi-part uploads dirnames
// with format "%d-%ld" using "sock_fd-time (NULL)"
void http_cerver_default_uploads_dirname_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_set_dirname (
		request,
		"%d-%ld",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL)
	);

}

// sets a method to be called on every new multi-part request
// that will be used to generate a new directory
// inside the uploads path to save all the files from the request
void http_cerver_set_uploads_dirname_generator (
	HttpCerver *http_cerver,
	void (*uploads_dirname_generator)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver) {
		http_cerver->uploads_dirname_generator = uploads_dirname_generator;
	}

}

// sets the HTTP cerver's uploads dirname generator
// to be http_cerver_default_uploads_dirname_generator ()
void http_cerver_set_default_uploads_dirname_generator (
	HttpCerver *http_cerver
) {

	if (http_cerver) {
		http_cerver->uploads_dirname_generator = http_cerver_default_uploads_dirname_generator;
	}

}

// specifies whether uploads are deleted after the requested has ended
// unless the request files have been explicitly saved using
// http_request_multi_part_keep_files ()
// the default value is HTTP_CERVER_DEFAULT_UPLOADS_DELETE
void http_cerver_set_uploads_delete_when_done (
	HttpCerver *http_cerver, bool value
) {

	if (http_cerver) {
		http_cerver->uploads_delete_when_done = value;
	}

}

#pragma endregion

#pragma region auth

// loads a key from a filename that can be used for jwt
// returns a newly allocated c string on success, NULL on error
char *http_cerver_auth_load_key (
	const char *filename, size_t *keylen
) {

	char *key = NULL;

	if (filename && keylen) {
		key = file_read (filename, keylen);
	}

	return key;

}

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256
void http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, const jwt_alg_t jwt_alg
) {

	if (http_cerver) http_cerver->jwt_alg = jwt_alg;

}

// sets the filename from where the jwt private key will be loaded
void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		http_cerver->jwt_opt_key_name = filename ? str_new (filename) : NULL;
	}

}

// sets the filename from where the jwt public key will be loaded
void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		http_cerver->jwt_opt_pub_key_name = filename ? str_new (filename) : NULL;
	}

}

#pragma endregion

#pragma region origins

// adds a new domain to the HTTP cerver's origins whitelist
// returns 0 on success, 1 on error
u8 http_cerver_add_origin_to_whitelist (
	HttpCerver *http_cerver, const char *domain
) {

	u8 retval = 1;

	if (http_cerver && domain) {
		if (http_cerver->n_origins < HTTP_ORIGINS_SIZE) {
			http_origin_init (
				&http_cerver->origins_whitelist[http_cerver->n_origins],
				domain
			);

			http_cerver->n_origins += 1;

			retval = 0;
		}
	}

	return retval;

}

void http_cerver_print_origins_whitelist (
	const HttpCerver *http_cerver
) {

	if (http_cerver) {
		if (http_cerver->n_origins) {
			(void) printf ("Origins whitelist (%u): \n", http_cerver->n_origins);

			const HttpOrigin *origin = NULL;
			for (u8 idx = 0; idx < http_cerver->n_origins; idx++) {
				origin = &http_cerver->origins_whitelist[idx];

				(void) printf (
					"[%u]: (%d) - %s\n",
					idx, origin->len, origin->value
				);
			}
		}

		else {
			(void) printf ("Origins whitelist is empty!\n");
		}
	}

}

#pragma endregion

#pragma region data

const void *http_cerver_get_custom_data (
	const HttpCerver *http_cerver
) {

	return http_cerver->custom_data;

}

void http_cerver_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
) {

	http_cerver->custom_data = custom_data;

}

void http_cerver_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
) {

	http_cerver->delete_custom_data = delete_custom_data;

}

void http_cerver_set_default_delete_custom_data (
	HttpCerver *http_cerver
) {

	http_cerver->delete_custom_data = free;

}

#pragma endregion

#pragma region responses

// adds a new global responses header
// this header will be added to all the responses
// if the response has the same header type,
// it will be used instead of the global header
// returns 0 on success, 1 on error
u8 http_cerver_add_responses_header (
	HttpCerver *http_cerver,
	const http_header type, const char *actual_header
) {

	u8 retval = 1;

	if (http_cerver && actual_header && (type < HTTP_HEADERS_SIZE)) {
		if (http_cerver->response_headers[type].len <= 0) {
			http_cerver->n_response_headers += 1;
		}

		http_response_header_set_with_type (
			&http_cerver->response_headers[type],
			type, actual_header
		);

		retval = 0;
	}

	return retval;

}

#pragma endregion

#pragma region stats

size_t http_cerver_stats_get_children_routes (
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
	cerver_log_msg ("\t\tMean file size: %.2f", file_stats->mean_file_size);

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
	cerver_log_msg ("\t\t\tMean file size: %.2f", file_stats->mean_file_size);

}

static void http_cerver_route_children_stats_print (
	const HttpRoute *route
) {

	cerver_log_msg ("\t\t%ld children:", route->children->size);

	RequestMethod method = (RequestMethod) REQUEST_METHOD_UNDEFINED;

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

		RequestMethod method = (RequestMethod) REQUEST_METHOD_UNDEFINED;
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

		// print general stats
		cerver_log_msg ("Incompleted requests: %ld", http_cerver->n_incompleted_requests);
		cerver_log_msg ("Unhandled requests: %ld", http_cerver->n_unhandled_requests);

		cerver_log_msg ("Catch requests: %ld", http_cerver->n_catch_all_requests);
		cerver_log_msg ("Failed auth requests: %ld", http_cerver->n_failed_auth_requests);
	}

}

#pragma endregion

#pragma region admin

// enables the ability to have admin routes
// to fetch cerver's HTTP stats
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN
void http_cerver_enable_admin_routes (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_routes = enable;
	}

}

// enables the ability to have an admin info route
// to fetch cerver's information
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO
void http_cerver_enable_admin_info_route (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_info_route = enable;
	}

}

// enables HTTP admin routes to handle HEAD requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS
void http_cerver_enable_admin_head_handlers (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_head_handlers = enable;
	}

}

// enables HTTP admin routes to handle OPTIONS requests
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS
void http_cerver_enable_admin_options_handlers (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_options_handlers = enable;
	}

}

// enables authentication in admin routes
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH
void http_cerver_enable_admin_routes_authentication (
	HttpCerver *http_cerver, const HttpRouteAuthType auth_type
) {

	if (http_cerver) {
		http_cerver->enable_admin_routes_auth = true;
		http_cerver->admin_auth_type = auth_type;
	}

}

// sets the method to be used to decode incoming data from JWT
// and sets a method to delete it after use
// if no delete method is set, data won't be freed
void http_cerver_admin_routes_auth_set_decode_data (
	HttpCerver *http_cerver,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
) {

	if (http_cerver) {
		http_cerver->admin_decode_data = decode_data;
		http_cerver->admin_delete_decoded_data = delete_decoded_data;
	}

}

// works like http_cerver_enable_admin_routes_authentication ()
// but sets a method to decode data from a JWT into a json string
void http_cerver_admin_routes_auth_decode_to_json (
	HttpCerver *http_cerver
) {

	if (http_cerver) {
		http_cerver->admin_decode_data = http_decode_data_into_json;
		http_cerver->admin_delete_decoded_data = free;
	}

}

// sets a method to be used to handle auth in admin routes
// HTTP cerver must had been configured with HTTP_ROUTE_AUTH_TYPE_CUSTOM
// method must return 0 on success and 1 on error
void http_cerver_admin_routes_set_authentication_handler (
	HttpCerver *http_cerver,
	unsigned int (*authentication_handler)(
		const HttpReceive *http_receive,
		const HttpRequest *request
	)
) {

	if (http_cerver) {
		http_cerver->admin_auth_handler = authentication_handler;
	}

}

// enables CORS headers in admin routes responses
// always uses admin origin's value
// if there is no dedicated origin, it will dynamically
// set the header based on the origins whitelist
// the initial value is HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS
void http_cerver_enable_admin_cors_headers (
	HttpCerver *http_cerver, const bool enable
) {

	if (http_cerver) {
		http_cerver->enable_admin_cors_headers = enable;
	}

}

// sets the dedicated domain that will be walways set
// in the admin responses CORS headers
void http_cerver_admin_set_origin (
	HttpCerver *http_cerver, const char *domain
) {

	if (http_cerver) {
		http_origin_init (
			&http_cerver->admin_origin,
			domain
		);
	}

}

// registers a new file system to be handled
// when requesting for fs stats
void http_cerver_register_admin_file_system (
	HttpCerver *http_cerver, const char *path
) {

	if (http_cerver) {
		(void) dlist_insert_at_end_unsafe (
			http_cerver->admin_file_systems_stats,
			http_admin_file_system_stats_create (path)
		);
	}

}

// registers an existing worker to be handled
// when requisting for workers states
void http_cerver_register_admin_worker (
	HttpCerver *http_cerver, const Worker *worker
) {

	if (http_cerver && worker) {
		(void) dlist_insert_at_end_unsafe (
			http_cerver->admin_workers,
			worker
		);
	}

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

					(void) dlist_insert_after (
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

		(void) dlist_insert_after (
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

const String *http_query_pairs_get_value (
	const DoubleList *pairs, const char *key
) {

	return key_value_pairs_get_value (pairs, key);

}

void http_query_pairs_print (const DoubleList *pairs) {

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

static int http_receive_handle_header_field (
	http_parser *parser, const char *at, size_t length
) {

	HttpRequest *request = ((HttpReceive *) parser->data)->request;

	char temp_header[HTTP_HEADER_TEMP_SIZE] = { 0 };
	(void) snprintf (
		temp_header, HTTP_HEADER_TEMP_SIZE - 1,
		"%.*s", (int) length, at
	);

	// printf ("\nHeader field: /%.*s/\n", (int) length, at);

	const http_header next_header = http_header_type_by_string (temp_header);
	if (next_header == HTTP_HEADER_UNDEFINED) {
		http_request_set_current_custom_header (request, temp_header);
	}

	request->next_header = next_header;

	return 0;

}

static int http_receive_handle_header_value (
	http_parser *parser, const char *at, size_t length
) {

	// printf ("\nHeader value: %.*s\n", (int) length, at);

	HttpRequest *request = ((HttpReceive *) parser->data)->request;
	if (request->next_header != HTTP_HEADER_UNDEFINED) {
		request->headers[request->next_header] = str_new (NULL);
		request->headers[request->next_header]->str = c_string_create ("%.*s", (int) length, at);
		request->headers[request->next_header]->len = length;
	}

	else {
		(void) snprintf (
			request->current_custom_header->header_value,
			HTTP_CUSTOM_HEADER_VALUE_SIZE - 1,
			"%.*s", (int) length, at
		);

		request->current_custom_header->header_value_len = (unsigned int) strlen (
			request->current_custom_header->header_value
		);
	}

	return 0;

}

static int http_receive_handle_body (
	http_parser *parser, const char *at, size_t length
) {

	// (void) printf ("http_receive_handle_body ()\n");

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive->receive_status == HTTP_RECEIVE_STATUS_HEADERS)
		http_receive->receive_status = HTTP_RECEIVE_STATUS_BODY;

	// (void) printf ("Body: %.*s", (int) length, at);

	if (!http_receive->request->body) {
		http_receive->request->body = str_create ("%.*s", (int) length, at);
	}

	else {
		str_append_n_from_c_string (http_receive->request->body, at, length);
	}

	return 0;

}

#pragma endregion

#pragma region mpart

static unsigned int http_multi_parts_init_pool (void) {

	unsigned int retval = 1;

	multi_parts_pool = pool_create (http_multi_part_delete);
	if (multi_parts_pool) {
		pool_set_create (multi_parts_pool, http_multi_part_new);
		pool_set_produce_if_empty (multi_parts_pool, true);
		if (!pool_init (
			multi_parts_pool,
			http_multi_part_new,
			HTTP_CERVER_MULTI_PARTS_POOL_INIT
		)) {
			retval = 0;
		}

		else {
			cerver_log_error ("Failed to init HTTP multi-parts pool!");
		}
	}

	else {
		cerver_log_error ("Failed to create HTTP multi-parts pool!");
	}

	return retval;

}

static inline MultiPart *http_multi_parts_get (void) {

	return (MultiPart *) pool_pop (multi_parts_pool);

}

static void http_multi_parts_return (void *multi_part_ptr) {

	if (multi_part_ptr) {
		http_multi_part_reset ((MultiPart *) multi_part_ptr);

		(void) pool_push (multi_parts_pool, multi_part_ptr);
	}

}

static inline bool is_multipart (const char *content) {

	return !strncmp (
		content,
		multi_part_header_value, multi_part_header_value_len
	) ? true : false;

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

		(void) dlist_insert_after (
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

static unsigned int http_mpart_get_boundary (
	char *boundary,
	const char *content_type, const unsigned int content_type_len
) {

	unsigned int retval = 1;

	if (content_type_len > multi_part_header_value_len) {
		char *end = (char *) content_type;
		// end += strlen ("multipart/form-data;");
		end += multi_part_header_value_len;

		DoubleList *attributes = http_mpart_attributes_parse (end);
		if (attributes) {
			// key_value_pairs_print (attributes);

			const String *original_boundary = http_query_pairs_get_value (attributes, "boundary");
			if (original_boundary) {
				(void) snprintf (
					boundary, HTTP_MULTI_PART_BOUNDARY_MAX_SIZE - 1,
					"--%s", original_boundary->str
				);

				retval = 0;
			}

			dlist_delete (attributes);
		}
	}

	return retval;

}

static int http_receive_handle_mpart_part_data_begin (
	multipart_parser *parser
) {

	// create a new multipart structure to handle new data
	HttpRequest *request = ((HttpReceive *) parser->data)->request;

	request->current_part = http_multi_parts_get ();
	(void) dlist_insert_after (
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

	return MULTI_PART_HEADER_INVALID;		// unknown header

}

static int http_receive_handle_mpart_header_field (
	multipart_parser *parser, const char *at, size_t length
) {

	MultiPart *mpart = (((HttpReceive *) parser->data)->request)->current_part;

	#ifdef HTTP_MPART_DEBUG
	(void) printf ("Header field original: /%.*s/\n", (int) length, at);
	#endif

	// build header in a temp location
	char *end = mpart->temp_header.value + mpart->temp_header.len;

	(void) snprintf (
		end, HTTP_HEADER_VALUE_SIZE - mpart->temp_header.len,
		"%.*s", (int) length, at
	);

	mpart->temp_header.len = (int) strlen (mpart->temp_header.value);

	#ifdef HTTP_MPART_DEBUG
	(void) printf ("Header field build: /%s/\n", mpart->temp_header.value);
	#endif

	mpart->next_header = http_receive_handle_mpart_header_field_handle (
		mpart->temp_header.value
	);

	return 0;

}

static int http_receive_handle_mpart_header_value (
	multipart_parser *parser, const char *at, size_t length
) {

	#ifdef HTTP_MPART_DEBUG
	(void) printf ("Header value: %.*s\n", (int) length, at);
	#endif

	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;
	if (multi_part->next_header != MULTI_PART_HEADER_INVALID) {
		HttpHeader *header = &multi_part->headers[multi_part->next_header];
		char *end = header->value + header->len;

		(void) snprintf (
			end, HTTP_HEADER_VALUE_SIZE - header->len,
			"%.*s", (int) length, at
		);

		header->len = (int) strlen (header->value);
	}

	// reset temp header after the actual value has been handled
	http_header_reset (&multi_part->temp_header);

	return 0;

}

static int http_receive_handle_mpart_headers_completed (multipart_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = (http_receive)->request->current_part;

	#ifdef HTTP_MPART_DEBUG
	http_multi_part_headers_print (multi_part);
	#endif

	if (multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].len > 0) {
		if (c_string_starts_with (
			multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].value,
			multi_part_form_data
		)) {
			char *end = (char *) multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].value;
			// end += strlen ("form-data;");
			end += multi_part_form_data_len;

			multi_part->params = http_mpart_attributes_parse (end);
			// key_value_pairs_print (multi_part->params);

			multi_part->name = key_value_pairs_get_value (multi_part->params, "name");
			// multi_part->filename = key_value_pairs_get_value (multi_part->params, "filename");
			const String *original_filename = key_value_pairs_get_value (
				multi_part->params, "filename"
			);

			if (original_filename) {
				multi_part->type = MULTI_PART_TYPE_FILE;

				// stats
				http_receive->file_stats->n_uploaded_files += 1;

				// sanitize file
				(void) snprintf (
					multi_part->filename, HTTP_MULTI_PART_FILENAME_SIZE,
					"%s", original_filename->str
				);

				files_sanitize_filename (multi_part->filename);

				multi_part->filename_len = (int) strlen (multi_part->filename);

				http_receive->request->n_files += 1;

				if (http_receive->http_cerver->uploads_path_len > 0) {
					if (http_receive->request->dirname_len > 0) {
						if (http_receive->http_cerver->uploads_filename_generator) {
							// TODO: check for errors
							http_receive->http_cerver->uploads_filename_generator (
								http_receive, http_receive->request
							);

							multi_part->saved_filename_len = snprintf (
								multi_part->saved_filename,
								HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
								"%s/%s/%s",
								http_receive->http_cerver->uploads_path,
								http_receive->request->dirname,
								multi_part->generated_filename
							);
						}

						else {
							multi_part->saved_filename_len = snprintf (
								multi_part->saved_filename,
								HTTP_MULTI_PART_SAVED_FILENAME_SIZE - 1,
								"%s/%s/%s",
								http_receive->http_cerver->uploads_path,
								http_receive->request->dirname,
								multi_part->filename
							);
						}
					}

					else {
						if (http_receive->http_cerver->uploads_filename_generator) {
							// TODO: check for errors
							http_receive->http_cerver->uploads_filename_generator (
								http_receive, http_receive->request
							);

							multi_part->saved_filename_len = snprintf (
								multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
								"%s/%s",
								http_receive->http_cerver->uploads_path,
								multi_part->generated_filename
							);
						}

						else {
							multi_part->saved_filename_len = snprintf (
								multi_part->saved_filename,
								HTTP_MULTI_PART_SAVED_FILENAME_SIZE - 1,
								"%s/%s",
								http_receive->http_cerver->uploads_path,
								multi_part->filename
							);
						}
					}

					multi_part->fd = open (
						multi_part->saved_filename,
						O_CREAT | O_WRONLY,
						http_receive->http_cerver->uploads_file_mode
					);

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
				multi_part->type = MULTI_PART_TYPE_VALUE;

				http_receive->request->n_values += 1;
			}
		}

		#ifdef HTTP_MPART_DEBUG
		else {
			cerver_log_error ("c_string_starts_with");
		}
		#endif
	}

	#ifdef HTTP_MPART_DEBUG
	else {
		cerver_log_error ("multi_part->headers[MULTI_PART_HEADER_CONTENT_DISPOSITION].len > 0");
	}
	#endif

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
		// multi_part->value = str_create ("%.*s", (int) length, at);

		char *end = multi_part->value + multi_part->value_len;

		(void) snprintf (
			end, HTTP_MULTI_PART_VALUE_SIZE - multi_part->value_len,
			"%.*s", (int) length, at
		);

		multi_part->value_len = (int) strlen (multi_part->value);
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

static int http_receive_handle_mpart_body_look_for_boundary (
	http_parser *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	char boundary[HTTP_MULTI_PART_BOUNDARY_MAX_SIZE] = { 0 };

	char *found = c_string_find_sub_in_len (at, webkit_multi_part_boundary_value, length);
	if (found) {
		#ifdef HTTP_MPART_DEBUG
		cerver_log_success ("Found multi-part boundary in body!");
		#endif

		(void) snprintf (
			boundary,
			HTTP_MULTI_PART_BOUNDARY_MAX_SIZE - 1,
			"------WebKitFormBoundary%.*s",
			(int) HTTP_MULTI_PART_WEBKIT_BOUNDARY_ID_SIZE,
			found + webkit_multi_part_boundary_value_len
		);

		#ifdef HTTP_MPART_DEBUG
		cerver_log_debug ("Multi-part bundary from body is: %s", boundary);
		#endif

		// set mpart parser values
		http_receive->settings.on_body = http_receive_handle_mpart_body;
		http_receive_init_mpart_parser (http_receive, boundary);

		// we can parse the current data
		(void) multipart_parser_execute (http_receive->mpart_parser, at, length);
	}

	return 0;

}

#pragma endregion

#pragma region handler

static int http_receive_handle_headers_completed (http_parser *parser);

static int http_receive_handle_message_completed (http_parser *parser);

const char *http_receive_status_str (
	const HttpReceiveStatus status
) {

	switch (status) {
		#define XX(num, name, string) case HTTP_RECEIVE_STATUS_##name: return #string;
		HTTP_RECEIVE_STATUS_MAP(XX)
		#undef XX
	}

	return http_receive_status_str (HTTP_RECEIVE_STATUS_NONE);

}

HttpReceive *http_receive_new (void) {

	HttpReceive *http_receive = (HttpReceive *) malloc (sizeof (HttpReceive));
	if (http_receive) {
		http_receive->receive_status = HTTP_RECEIVE_STATUS_NONE;

		http_receive->cr = NULL;

		http_receive->keep_alive = false;

		http_receive->handler = NULL;

		http_receive->http_cerver = NULL;

		http_receive->parser = NULL;
		(void) memset (&http_receive->settings, 0, sizeof (http_parser_settings));

		http_receive->mpart_parser = NULL;
		(void) memset (&http_receive->mpart_settings, 0, sizeof (multipart_parser_settings));

		http_receive->request = NULL;

		http_receive->type = HTTP_RECEIVE_TYPE_NONE;
		http_receive->route = NULL;
		http_receive->served_file = NULL;

		http_receive->status = HTTP_STATUS_NONE;
		http_receive->sent = 0;

		http_receive->is_multi_part = false;
		http_receive->file_stats = NULL;
	}

	return http_receive;

}

HttpReceive *http_receive_create (CerverReceive *cerver_receive) {

	HttpReceive *http_receive = http_receive_new ();
	if (http_receive) {
		http_receive->cr = cerver_receive;

		http_receive->handler = http_receive_handle;

		http_receive->http_cerver = (HttpCerver *) cerver_receive->cerver->cerver_data;

		http_receive->parser = (http_parser *) malloc (sizeof (http_parser));
		http_parser_init (http_receive->parser, HTTP_REQUEST);

		// http_receive->parser->data = http_receive->request;
		http_receive->parser->data = http_receive;

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

		http_receive->request = http_request_new ();
		http_receive->request->keep_files = !http_receive->http_cerver->uploads_delete_when_done;

		http_receive->file_stats = http_route_file_stats_new ();
	}

	return http_receive;

}

void http_receive_delete (HttpReceive *http_receive) {

	if (http_receive) {
		http_receive->cr = NULL;

		http_receive->http_cerver = NULL;

		free (http_receive->parser);

		http_request_destroy (http_receive->request);
		http_request_delete (http_receive->request);

		http_receive->route = NULL;
		http_route_file_stats_delete (http_receive->file_stats);

		if (http_receive->mpart_parser)
			multipart_parser_free (http_receive->mpart_parser);

		free (http_receive);
	}

}

const CerverReceive *http_receive_get_cerver_receive (
	const HttpReceive *http_receive
) {

	return http_receive->cr;

}

const int http_receive_get_sock_fd (
	const HttpReceive *http_receive
) {

	return http_receive->cr->connection->socket->sock_fd;

}

const HttpCerver *http_receive_get_cerver (
	const HttpReceive *http_receive
) {

	return http_receive->http_cerver;

}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"

static void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_response_send (catch_all, http_receive);

}

static void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_response_send (not_found_error, http_receive);

}

#pragma GCC diagnostic pop

// catch all mismatches and handle with cath all route
static void http_receive_handle_catch_all (
	HttpCerver *http_cerver, HttpReceive *http_receive, HttpRequest *request
) {

	http_receive->receive_status = HTTP_RECEIVE_STATUS_UNHANDLED;

	cerver_log_warning (
		"No matching route for %s %s",
		http_method_str ((enum http_method) request->method),
		request->url->str
	);

	if (http_cerver->not_found_handler) {
		// handle with not found route
		http_cerver->not_found (http_receive, request);
	}

	else {
		// handle with default route
		http_cerver->default_handler (http_receive, request);

		(void) pthread_mutex_lock (http_cerver->mutex);
		http_cerver->n_catch_all_requests += 1;
		(void) pthread_mutex_unlock (http_cerver->mutex);
	}

}

// handles an actual route match
// selects the right handler based on the request's method
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
				if (request->headers[HTTP_HEADER_CONTENT_TYPE]) {
					if (!strncmp (
						"application/x-www-form-urlencoded",
						request->headers[HTTP_HEADER_CONTENT_TYPE]->str, 33
					)) {
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
		char *end_sub = request->url->str;
		if (strcmp ("/", route->route->str)) {
			end_sub += route->route->len;
		}

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

static void http_receive_handle_select_failed_auth (
	HttpReceive *http_receive
) {

	(void) http_response_send (bad_auth_error, http_receive);

	(void) pthread_mutex_lock (http_receive->http_cerver->mutex);
	http_receive->http_cerver->n_failed_auth_requests += 1;
	(void) pthread_mutex_unlock (http_receive->http_cerver->mutex);

}

static void http_receive_handle_select_auth_bearer (
	HttpCerver *http_cerver,
	HttpReceive *http_receive,
	HttpRoute *found, HttpRequest *request
) {

	// get the bearer token
	// printf ("\nComplete Token -> %s\n", request->headers[REQUEST_HEADER_AUTHORIZATION]->str);

	char *token = request->headers[HTTP_HEADER_AUTHORIZATION]->str + sizeof ("Bearer");

	// printf ("\nToken -> %s\n", token);

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
		}
	}

	jwt_valid_free (jwt_valid);

}

static void http_receive_handle_select_auth_custom (
	HttpCerver *http_cerver,
	HttpReceive *http_receive,
	HttpRoute *found, HttpRequest *request
) {

	if (found->authentication_handler) {
		if (!found->authentication_handler (
			http_receive, request
		)) {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_success ("Success authentication with custom method!");
			#endif

			http_receive_handle_match (
				http_cerver,
				http_receive,
				request,
				found
			);
		}

		else {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_error ("Failed authentication with custom method!");
			#endif

			http_receive_handle_select_failed_auth (http_receive);
			http_cerver->n_failed_auth_requests += 1;
		}
	}

	// no authentication method was provided
	else {
		#ifdef HTTP_AUTH_DEBUG
		cerver_log_error ("No authentication method was provided!");
		#endif

		http_receive_handle_select_failed_auth (http_receive);
		http_cerver->n_failed_auth_requests += 1;
	}

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
				match = http_receive_handle_select_children (
					route, request, &found
				);
			}
		}

		le = le->next;
	}

	// we have found a route!
	if (match) {
		http_receive->route = found;

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

			// a bearer JWT is expected in the authorization header
			case HTTP_ROUTE_AUTH_TYPE_BEARER: {
				if (request->headers[HTTP_HEADER_AUTHORIZATION]) {
					http_receive_handle_select_auth_bearer (
						http_cerver, http_receive, found, request
					);
				}

				// no authentication header was provided
				else {
					http_receive_handle_select_failed_auth (http_receive);
					http_cerver->n_failed_auth_requests += 1;
				}
			} break;

			// a custom method is used to handle authentication
			case HTTP_ROUTE_AUTH_TYPE_CUSTOM: {
				http_receive_handle_select_auth_custom (
					http_cerver, http_receive, found, request
				);
			} break;

			default: break;
		}
	}

	else {
		http_receive_handle_catch_all (http_cerver, http_receive, request);
	}

}

static void http_receive_init_mpart_parser (
	HttpReceive *http_receive,
	const char *boundary
) {

	char dirname[HTTP_MULTI_PART_DIRNAME_SIZE] = { 0 };

	http_receive->mpart_settings.on_header_field = http_receive_handle_mpart_header_field;
	http_receive->mpart_settings.on_header_value = http_receive_handle_mpart_header_value;
	http_receive->mpart_settings.on_part_data = http_receive_handle_mpart_data;

	http_receive->mpart_settings.on_part_data_begin = http_receive_handle_mpart_part_data_begin;
	http_receive->mpart_settings.on_headers_complete = http_receive_handle_mpart_headers_completed;
	http_receive->mpart_settings.on_part_data_end = http_receive_handle_mpart_data_end;
	http_receive->mpart_settings.on_body_end = NULL;

	http_receive->mpart_parser = multipart_parser_init (boundary, &http_receive->mpart_settings);
	http_receive->mpart_parser->data = http_receive;

	http_receive->request->multi_parts = dlist_init (http_multi_parts_return, NULL);

	// TODO: handler errors
	if (http_receive->http_cerver->uploads_dirname_generator) {
		if (http_receive->http_cerver->uploads_path) {
			http_receive->http_cerver->uploads_dirname_generator (
				http_receive, http_receive->request
			);

			if (http_receive->request->dirname_len > 0) {
				(void) snprintf (
					dirname,
					HTTP_MULTI_PART_DIRNAME_SIZE - 1,
					"%s/%s",
					http_receive->http_cerver->uploads_path,
					http_receive->request->dirname
				);

				(void) files_create_recursive_dir (
					dirname, http_receive->http_cerver->uploads_dir_mode
				);
			}
		}
	}

}

static int http_receive_handle_headers_completed (http_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->receive_status = HTTP_RECEIVE_STATUS_HEADERS;

	// #ifdef HTTP_HEADERS_DEBUG
	// http_request_headers_print (http_receive->request);
	// #endif

	// check if we are going to get any file(s)
	if (http_receive->request->headers[HTTP_HEADER_CONTENT_TYPE]) {
		if (is_multipart (
			http_receive->request->headers[HTTP_HEADER_CONTENT_TYPE]->str
		)) {
			// printf ("\nis multipart!\n");
			http_receive->is_multi_part = true;

			char boundary[HTTP_MULTI_PART_BOUNDARY_MAX_SIZE] = { 0 };

			if (!http_mpart_get_boundary (
				boundary,
				http_receive->request->headers[HTTP_HEADER_CONTENT_TYPE]->str,
				http_receive->request->headers[HTTP_HEADER_CONTENT_TYPE]->len
			)) {
				// printf ("\n%s\n", boundary);
				http_receive->settings.on_body = http_receive_handle_mpart_body;

				http_receive_init_mpart_parser (http_receive, boundary);
			}

			else {
				#ifdef HTTP_MPART_DEBUG
				cerver_log_warning (
					"Unable to find the multi-part boundary in the header, "
					"searching for it in the body..."
				);
				#endif

				// we need to search for the boundary in the response
				http_receive->settings.on_body = http_receive_handle_mpart_body_look_for_boundary;
			}
		}
	}

	return 0;

}

// TODO: handle authentication
static void http_receive_handle_serve_file (HttpReceive *http_receive) {

	bool found = false;

	char filename[HTTP_CERVER_STATIC_FILE_SIZE] = { 0 };
	struct stat filestatus = { 0 };
	HttpStaticPath *static_path = NULL;

	for (
		ListElement *le = dlist_start (http_receive->http_cerver->static_paths);
		le; le = le->next
	) {
		static_path = (HttpStaticPath *) le->data;

		(void) c_string_concat_safe (
			static_path->path->str, http_receive->request->url->str,
			filename, HTTP_CERVER_STATIC_FILE_SIZE
		);

		// check if file exists
		(void) memset (&filestatus, 0, sizeof (struct stat));
		if (!stat (filename, &filestatus)) {
			http_receive->served_file = http_receive->request->url->str;

			// serve the file
			(void) http_response_send_file_internal (
				http_receive, HTTP_STATUS_OK,
				filename, &filestatus
			);

			found = true;
			break;
		}
	}

	if (!found) {
		cerver_log_warning (
			"Unable to find file %s",
			http_receive->request->url->str
		);

		http_receive_handle_catch_all (
			http_receive->http_cerver,
			http_receive, http_receive->request
		);
	}

}

static int http_receive_handle_message_completed (http_parser *parser) {

	// printf ("\non message complete!\n");

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->receive_status = HTTP_RECEIVE_STATUS_COMPLETED;

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
					http_receive->type = HTTP_RECEIVE_TYPE_FILE;
				}

				// unable to serve a file, try for matching route
				else {
					http_receive_handle_select (http_receive, http_receive->request);
					http_receive->type = HTTP_RECEIVE_TYPE_ROUTE;
				}
			}

			else {
				http_receive_handle_select (http_receive, http_receive->request);
				http_receive->type = HTTP_RECEIVE_TYPE_ROUTE;
			}
		} break;

		default:
			http_receive_handle_select (http_receive, http_receive->request);
			http_receive->type = HTTP_RECEIVE_TYPE_ROUTE;
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

	ssize_t n_parsed = http_parser_execute (
		http_receive->parser, &http_receive->settings, packet_buffer, rc
	);

	if (n_parsed != rc) {
		cerver_log_error (
			"http_parser_execute () failed - n parsed %ld / received %ld",
			n_parsed, rc
		);

		// send back error message
		(void) http_response_send (server_error, http_receive);

		connection_end (http_receive->cr->connection);
	}

	// printf ("http_receive_handle () - end!\n");

}

#pragma endregion
