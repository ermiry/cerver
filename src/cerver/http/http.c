#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <stdarg.h>

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <openssl/crypto.h>
#include <openssl/sha.h>

#include "cerver/types/types.h"
#include "cerver/types/string.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/connection.h"
#include "cerver/errors.h"
#include "cerver/files.h"
#include "cerver/handler.h"
#include "cerver/network.h"
#include "cerver/packets.h"
#include "cerver/timer.h"
#include "cerver/version.h"

#include "cerver/threads/worker.h"

#include "cerver/http/admin.h"
#include "cerver/http/auth.h"
#include "cerver/http/compression.h"
#include "cerver/http/custom.h"
#include "cerver/http/headers.h"
#include "cerver/http/http.h"
#include "cerver/http/method.h"
#include "cerver/http/multipart.h"
#include "cerver/http/proxy.h"
#include "cerver/http/recycle.h"
#include "cerver/http/request.h"
#include "cerver/http/response.h"
#include "cerver/http/route.h"
#include "cerver/http/stream.h"
#include "cerver/http/url.h"

#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"

#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/http/json/json.h"

#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"

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

static HttpResponse *payload_too_large_error = NULL;
static HttpResponse *uri_too_long_error = NULL;
static HttpResponse *header_fields_too_large_error = NULL;
static HttpResponse *request_timeout_error = NULL;
static HttpResponse *method_not_allowed_error = NULL;
static HttpResponse *not_implemented_error = NULL;

// the rejections of an upgrade to a WebSocket endpoint
static HttpResponse *upgrade_required_error = NULL;
static HttpResponse *forbidden_error = NULL;
static HttpResponse *web_socket_unavailable_error = NULL;

static const char *upgrade_required_json = { "{\"error\": \"Upgrade to WebSocket required!\"}" };
static const char *web_socket_unavailable_json = { "{\"error\": \"Too many WebSocket connections!\"}" };

static const char *form_urlencoded_value = { "application/x-www-form-urlencoded" };
static const size_t form_urlencoded_value_len = 33;

static const char *query_separator = { "?" };

// a space in a query and in the values of a form
static const char query_encoded_space = '+';
static const char query_space = ' ';

static const char static_file_extension_separator = '.';

static const char *expect_continue_value = { "100-continue" };
static const char *continue_response = { "HTTP/1.1 100 Continue\r\n\r\n" };
static const size_t continue_response_len = 25;

static const char *http_receive_parser_no_reason = { "No reason" };

static Pool *multi_parts_pool = NULL;

// the HTTP cervers of the process that hold a reference to the shared state
static DoubleList *http_cervers = NULL;
static pthread_mutex_t http_cervers_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *multi_part_header_value = { "multipart/form-data;" };
static const size_t multi_part_header_value_len = 20;

static const char *multi_part_form_data = { "form-data;" };
static const size_t multi_part_form_data_len = 10;


static void http_static_path_delete (void *http_static_path_ptr);

static int http_static_path_comparator (const void *a, const void *b);

static void http_receive_handle_default_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

static void http_receive_handle_not_found_route (
	const HttpReceive *http_receive, const HttpRequest *request
);

static unsigned int http_multi_parts_init_pool (void);

static void http_multi_parts_end_pool (void);

static int http_receive_init_mpart_parser (
	HttpReceive *http_receive,
	const char *boundary
);

static void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
);

static void http_receive_web_socket_release (HttpReceive *http_receive);

static void http_receive_close (
	HttpReceive *http_receive, const bool rejected
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

		http_cerver->tree = NULL;

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

		http_cerver->uploads_sequence = 0;

		http_cerver->jwt_alg = JWT_ALG_NONE;

		http_cerver->jwt_opt_key_name = NULL;
		http_cerver->jwt_private_key = NULL;

		http_cerver->jwt_opt_pub_key_name = NULL;
		http_cerver->jwt_public_key = NULL;

		http_cerver->jwt_max_size = HTTP_CERVER_DEFAULT_JWT_MAX_SIZE;
		http_cerver->jwt_require_expiration = HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION;
		http_cerver->jwt_leeway_seconds = HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS;
		http_cerver->jwt_issuer = NULL;
		http_cerver->jwt_audience = NULL;

		http_cerver->n_origins = 0;
		for (u8 i = 0; i < HTTP_ORIGINS_SIZE; i++)
			http_origin_reset (&http_cerver->origins_whitelist[i]);

		http_cerver->custom_data = NULL;
		http_cerver->delete_custom_data = NULL;

		http_headers_init (&http_cerver->response_headers);

		http_cerver->global_reference = false;

		http_cerver->n_incompleted_requests = 0;
		http_cerver->n_unhandled_requests = 0;

		http_cerver->n_catch_all_requests = 0;
		http_cerver->n_failed_auth_requests = 0;

		http_cerver->enable_admin_routes = HTTP_CERVER_DEFAULT_ENABLE_ADMIN;
		http_cerver->admin_routes_ready = false;
		http_cerver->enable_admin_info_route = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_INFO;
		http_cerver->enable_admin_head_handlers = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_HEADS;
		http_cerver->enable_admin_options_handlers = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_OPTIONS;

		http_cerver->enable_admin_routes_auth = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_AUTH;
		http_cerver->admin_auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;
		http_cerver->admin_decode_data = NULL;
		http_cerver->admin_delete_decoded_data = NULL;
		http_cerver->admin_auth_handler = NULL;

		http_cerver->admin_routes_custom_data = NULL;
		http_cerver->delete_admin_routes_custom_data = NULL;

		http_cerver->enable_admin_cors_headers = HTTP_CERVER_DEFAULT_ENABLE_ADMIN_CORS;
		http_origin_reset (&http_cerver->admin_origin);

		http_cerver->admin_file_systems_stats = NULL;
		http_cerver->admin_workers = NULL;
		http_cerver->admin_mutex = NULL;

		http_cerver->mutex = NULL;

		http_cerver->max_body_size = HTTP_CERVER_DEFAULT_MAX_BODY_SIZE;
		http_cerver->max_url_size = HTTP_CERVER_DEFAULT_MAX_URL_SIZE;
		http_cerver->max_header_size = HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE;
		http_cerver->max_headers = HTTP_CERVER_DEFAULT_MAX_HEADERS;
		http_cerver->max_multipart_parts = HTTP_CERVER_DEFAULT_MAX_MULTIPART_PARTS;
		http_cerver->max_file_size = HTTP_CERVER_DEFAULT_MAX_FILE_SIZE;

		http_cerver->n_limited_requests = 0;

		http_cerver->headers_timeout = HTTP_CERVER_DEFAULT_HEADERS_TIMEOUT_MILLISECONDS;
		http_cerver->request_timeout = HTTP_CERVER_DEFAULT_REQUEST_TIMEOUT_MILLISECONDS;
		http_cerver->idle_timeout = HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS;

		http_cerver->send_timeout = 0;

		http_cerver->web_sockets = NULL;

		http_cerver->n_timeout_requests = 0;

		http_cerver->n_unanswered_requests = 0;

		http_cerver->date_header = HTTP_CERVER_DEFAULT_DATE_HEADER;
		http_cerver->request_id = HTTP_CERVER_DEFAULT_REQUEST_ID;

		http_cerver->compression_min_size = HTTP_COMPRESSION_DEFAULT_MIN_SIZE;
		http_cerver->compression_level = HTTP_COMPRESSION_DEFAULT_LEVEL;

		http_cerver->trusted_proxies = HTTP_CERVER_DEFAULT_TRUSTED_PROXIES;
		http_cerver->n_trusted_proxies = 0;
		(void) memset (
			http_cerver->trusted_proxies_networks, 0,
			HTTP_TRUSTED_PROXIES_SIZE * sizeof (HttpProxyNetwork)
		);

		(void) memset (
			http_cerver->n_responses_by_status, 0,
			HTTP_CERVER_RESPONSES_STATUS_SIZE * sizeof (size_t)
		);

		http_cerver->half_close_is_gone = HTTP_CERVER_DEFAULT_HALF_CLOSE_IS_GONE;

		http_cerver->parser_lenient = HTTP_PARSER_LENIENT_NONE;

		(void) memset (http_cerver->n_aborted_requests, 0, sizeof (http_cerver->n_aborted_requests));
		(void) memset (http_cerver->n_vanished_requests, 0, sizeof (http_cerver->n_vanished_requests));
		http_cerver->n_failed_responses = 0;
	}

	return http_cerver;

}

void http_cerver_delete (void *http_cerver_ptr) {

	if (http_cerver_ptr) {
		HttpCerver *http_cerver = (HttpCerver *) http_cerver_ptr;

		dlist_delete (http_cerver->static_paths);

		// the endpoints of the first routes API keep references to their routes
		http_tree_delete (http_cerver->tree);

		dlist_delete (http_cerver->routes);

		str_delete (http_cerver->jwt_opt_key_name);
		jwt_key_delete (http_cerver->jwt_private_key);

		str_delete (http_cerver->jwt_opt_pub_key_name);
		jwt_key_delete (http_cerver->jwt_public_key);

		str_delete (http_cerver->jwt_issuer);
		str_delete (http_cerver->jwt_audience);

		if (http_cerver->custom_data) {
			if (http_cerver->delete_custom_data) {
				http_cerver->delete_custom_data (
					http_cerver->custom_data
				);
			}
		}

		http_headers_reset (&http_cerver->response_headers);

		if (http_cerver->admin_routes_custom_data) {
			if (http_cerver->delete_admin_routes_custom_data) {
				http_cerver->delete_admin_routes_custom_data (
					http_cerver->admin_routes_custom_data
				);
			}
		}

		dlist_delete (http_cerver->admin_file_systems_stats);
		dlist_delete (http_cerver->admin_workers);
		thread_mutex_delete (http_cerver->admin_mutex);

		thread_mutex_delete (http_cerver->mutex);

		http_web_socket_registry_delete (http_cerver->web_sockets);

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

		http_cerver->tree = http_tree_create (http_cerver);

		http_cerver->default_handler = http_receive_handle_default_route;

		http_cerver->not_found = http_receive_handle_not_found_route;

		http_cerver->jwt_alg = JWT_DEFAULT_ALG;

		http_cerver->admin_file_systems_stats = dlist_init (
			http_admin_file_system_stats_delete, NULL
		);

		http_cerver->admin_workers = dlist_init (NULL, NULL);

		http_cerver->admin_mutex = thread_mutex_new ();

		http_cerver->mutex = thread_mutex_new ();

		http_cerver->web_sockets = http_web_socket_registry_create ();
	}

	return http_cerver;

}

// a JSON response of a rejected upgrade, with the Upgrade header,
// and with one more header when its type is not HTTP_HEADER_UNDEFINED
static HttpResponse *http_cerver_web_socket_response (
	const http_status status, const char *json,
	const http_header type, const char *value
) {

	HttpResponse *response = (HttpResponse *) http_response_new ();
	if (response) {
		response->status = status;

		const size_t json_len = strlen (json);
		(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_JSON);
		(void) http_response_add_content_length_header (response, json_len);
		(void) http_response_add_header (response, HTTP_HEADER_UPGRADE, HTTP_WEB_SOCKET_UPGRADE_TOKEN);
		if (type != HTTP_HEADER_UNDEFINED) (void) http_response_add_header (response, type, value);

		// the json is a static string
		response->data = (char *) json;
		response->data_len = json_len;
		response->data_ref = true;

		(void) http_response_compile (response);
	}

	return response;

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

	payload_too_large_error = http_response_create_json_key_value (
		HTTP_STATUS_PAYLOAD_TOO_LARGE, "error", "Payload too large!"
	);

	uri_too_long_error = http_response_create_json_key_value (
		HTTP_STATUS_URI_TOO_LONG, "error", "URI too long!"
	);

	header_fields_too_large_error = http_response_create_json_key_value (
		HTTP_STATUS_REQUEST_HEADER_FIELDS_TOO_LARGE, "error", "Request header fields too large!"
	);

	request_timeout_error = http_response_create_json_key_value (
		HTTP_STATUS_REQUEST_TIMEOUT, "error", "Request timeout!"
	);

	method_not_allowed_error = http_response_create_json_key_value (
		HTTP_STATUS_METHOD_NOT_ALLOWED, "error", "Method not allowed!"
	);

	not_implemented_error = http_response_create_json_key_value (
		HTTP_STATUS_NOT_IMPLEMENTED, "error", "Not implemented!"
	);

	upgrade_required_error = http_cerver_web_socket_response (
		HTTP_STATUS_UPGRADE_REQUIRED, upgrade_required_json, HTTP_HEADER_SEC_WEBSOCKET_VERSION, HTTP_WEB_SOCKET_VERSION
	);

	forbidden_error = http_response_create_json_key_value (
		HTTP_STATUS_FORBIDDEN, "error", "Forbidden!"
	);

	web_socket_unavailable_error = http_cerver_web_socket_response (
		HTTP_STATUS_SERVICE_UNAVAILABLE, web_socket_unavailable_json, HTTP_HEADER_UNDEFINED, NULL
	);

	if (
		oki_doki
		&& bad_request_error && bad_user_error
		&& bad_auth_error && not_found_error
		&& server_error
		&& catch_all
		&& payload_too_large_error && uri_too_long_error
		&& header_fields_too_large_error
		&& request_timeout_error
		&& method_not_allowed_error && not_implemented_error
		&& upgrade_required_error && forbidden_error && web_socket_unavailable_error
	) retval = 0;

	return retval;

}

// the key of the file is created for the algorithm of the cerver,
// it must have the type, the curve and the strength that the algorithm asks
// returns the key, or NULL and the reason in error
static JwtKey *http_cerver_init_create_jwt_key (
	const HttpCerver *http_cerver,
	const unsigned char *material, const size_t material_length,
	const JwtKeyType type, JwtKeyError *error
) {

	JwtKey *key = jwt_key_create (
		http_cerver->jwt_alg, type, material, material_length, error
	);

	if (key) {
		*error = jwt_key_check_strength (key);
		if (*error != JWT_KEY_ERROR_NONE) {
			jwt_key_delete (key);
			key = NULL;
		}
	}

	return key;

}

// reads the file of a key, its content is cleared after the key is created
// returns the key, or NULL if it can not be used: the cerver does not start
static JwtKey *http_cerver_init_load_jwt_key (
	const HttpCerver *http_cerver,
	const String *filename, const JwtKeyType type
) {

	JwtKey *key = NULL;
	JwtKeyError error = JWT_KEY_ERROR_EMPTY;

	size_t material_length = 0;
	char *material = file_read (filename->str, &material_length);
	const bool read = (material != NULL);
	if (read) {
		key = http_cerver_init_create_jwt_key (
			http_cerver,
			(const unsigned char *) material, material_length,
			type, &error
		);

		OPENSSL_cleanse (material, material_length);
		free (material);
	}

	if (key) {
		cerver_log_success (
			"Loaded cerver's %s http jwt %s key for %s!",
			http_cerver->cerver->info->name,
			jwt_key_type_to_string (type), jwt_alg_str (http_cerver->jwt_alg)
		);
	}

	else {
		cerver_log_error (
			"Failed to load cerver's %s http jwt %s key %s for %s: %s",
			http_cerver->cerver->info->name,
			jwt_key_type_to_string (type), filename->str,
			jwt_alg_str (http_cerver->jwt_alg),
			read ? jwt_key_error_to_string (error) : "The file can not be read"
		);
	}

	return key;

}

// a key that can not be used is an error, so the cerver does not start
static unsigned int http_cerver_init_load_jwt_keys (
	HttpCerver *http_cerver
) {

	unsigned int errors = 0;

	if (http_cerver->jwt_opt_key_name && !http_cerver->jwt_private_key) {
		http_cerver->jwt_private_key = http_cerver_init_load_jwt_key (
			http_cerver, http_cerver->jwt_opt_key_name, JWT_KEY_TYPE_PRIVATE
		);

		if (!http_cerver->jwt_private_key) errors |= 1;
	}

	if (http_cerver->jwt_opt_pub_key_name && !http_cerver->jwt_public_key) {
		http_cerver->jwt_public_key = http_cerver_init_load_jwt_key (
			http_cerver, http_cerver->jwt_opt_pub_key_name, JWT_KEY_TYPE_PUBLIC
		);

		if (!http_cerver->jwt_public_key) errors |= 1;
	}

	return errors;

}

// the common responses are global, so they are reset
// for the next cerver that calls http_cerver_init ()
static void http_cerver_end_responses (void) {

	http_response_delete (oki_doki);
	oki_doki = NULL;
	http_response_delete (bad_request_error);
	bad_request_error = NULL;
	http_response_delete (bad_user_error);
	bad_user_error = NULL;
	http_response_delete (bad_auth_error);
	bad_auth_error = NULL;
	http_response_delete (not_found_error);
	not_found_error = NULL;
	http_response_delete (server_error);
	server_error = NULL;
	http_response_delete (catch_all);
	catch_all = NULL;

	http_response_delete (payload_too_large_error);
	payload_too_large_error = NULL;
	http_response_delete (uri_too_long_error);
	uri_too_long_error = NULL;
	http_response_delete (header_fields_too_large_error);
	header_fields_too_large_error = NULL;
	http_response_delete (request_timeout_error);
	request_timeout_error = NULL;
	http_response_delete (method_not_allowed_error);
	method_not_allowed_error = NULL;
	http_response_delete (not_implemented_error);
	not_implemented_error = NULL;

	http_response_delete (upgrade_required_error);
	upgrade_required_error = NULL;
	http_response_delete (forbidden_error);
	forbidden_error = NULL;
	http_response_delete (web_socket_unavailable_error);
	web_socket_unavailable_error = NULL;

}

// releases the state shared by every HTTP cerver of the process
static void http_cerver_global_end (void) {

	http_responses_end ();

	http_jwt_end_pool ();

	http_cerver_end_responses ();

	http_multi_parts_end_pool ();

}

// creates the state shared by every HTTP cerver of the process,
// the common responses are compiled with the default headers of this cerver
// what was created is released if something fails
static unsigned int http_cerver_global_init (
	const HttpCerver *http_cerver
) {

	unsigned int errors = 0;

	// the random seed of the json objects, before the threads of the cerver use them
	json_object_seed (0);

	// init HTTP jwt pool
	errors |= http_jwt_init_pool ();

	// init HTTP responses pool
	errors |= http_responses_init (http_cerver);

	// init common responses
	errors |= http_cerver_init_responses ();

	// init multi-parts pool
	errors |= http_multi_parts_init_pool ();

	if (errors) http_cerver_global_end ();

	return errors;

}

static int http_cervers_compare (const void *one, const void *two) {

	return (one == two) ? 0 : 1;

}

// the shared state lives while the list has a cerver
static void http_cervers_release (void) {

	http_cerver_global_end ();

	dlist_delete (http_cervers);
	http_cervers = NULL;

}

// the first HTTP cerver of the process creates the shared state,
// every cerver takes a reference to it
// returns 0 on success, 1 on error
static unsigned int http_cervers_add (
	HttpCerver *http_cerver
) {

	unsigned int errors = 0;

	(void) pthread_mutex_lock (&http_cervers_mutex);

	if (!http_cervers) http_cervers = dlist_init (NULL, http_cervers_compare);

	if (dlist_is_empty (http_cervers)) errors |= http_cerver_global_init (http_cerver);

	if (!errors) errors |= (unsigned int) dlist_insert_at_end_unsafe (http_cervers, http_cerver);

	http_cerver->global_reference = !errors;

	(void) pthread_mutex_unlock (&http_cervers_mutex);

	return errors;

}

// the last HTTP cerver of the process releases the shared state,
// otherwise the responses producer follows a cerver that is still alive
static void http_cervers_remove (
	HttpCerver *http_cerver
) {

	(void) pthread_mutex_lock (&http_cervers_mutex);

	(void) dlist_remove (http_cervers, http_cerver, NULL);

	if (dlist_is_empty (http_cervers)) http_cervers_release ();

	else http_responses_follow (http_cerver, (const HttpCerver *) dlist_start (http_cervers)->data);

	http_cerver->global_reference = false;

	(void) pthread_mutex_unlock (&http_cervers_mutex);

}

static unsigned int http_cerver_init_internal (
	HttpCerver *http_cerver
) {

	unsigned int errors = 0;

	#ifdef HTTP_DEBUG
	cerver_log_msg ("Initializing HTTP cerver...");
	#endif

	// the jwt pool, the responses and the multi-parts pool
	if (!http_cerver->global_reference) errors |= http_cervers_add (http_cerver);

	// load jwt keys
	errors |= http_cerver_init_load_jwt_keys (http_cerver);

	return errors;

}

// the admin routes are children of the main route of the first routes API,
// or endpoints of the first top level group if the cerver only uses groups
// they are added only once, the routes can be compiled before the cerver starts
static void http_cerver_init_admin_routes (
	HttpCerver *http_cerver
) {

	if (dlist_size (http_cerver->routes)) {
		if (!http_cerver->main_route) {
			http_cerver->main_route = (HttpRoute *) dlist_start (http_cerver->routes)->data;
		}
	}

	if (http_cerver->enable_admin_routes && !http_cerver->admin_routes_ready) {
		if (http_cerver->main_route) {
			(void) http_cerver_admin_init (
				http_cerver,
				(HttpRoute *) http_cerver->main_route
			);
		}

		else if (dlist_size (http_cerver->tree->groups)) {
			(void) http_cerver_admin_group_init (
				http_cerver,
				(HttpGroup *) dlist_start (http_cerver->tree->groups)->data
			);
		}

		http_cerver->admin_routes_ready = true;
	}

}

// builds the routes tree, a registration error stops the cerver
// returns 0 on success, 1 on error
static u8 http_cerver_init_routes (
	HttpCerver *http_cerver
) {

	#ifdef HTTP_DEBUG
	cerver_log_msg ("Loading HTTP routes...");
	#endif

	http_cerver_init_admin_routes (http_cerver);

	const u8 retval = http_tree_compile (http_cerver->tree);

	if (!retval) {
		http_tree_print (http_cerver->tree);

		#ifdef HTTP_DEBUG
		cerver_log_success ("Done loading HTTP routes!");
		#endif
	}

	return retval;

}

u8 http_cerver_init (HttpCerver *http_cerver) {

	u8 retval = 1;

	if (http_cerver) {
		if (!http_cerver_init_internal (http_cerver)) {
			retval = http_cerver_init_routes (http_cerver);
		}
	}

	return retval;

}

// destroy values allocated in http_cerver_init (),
// the shared state is released by the last HTTP cerver of the process
// a cerver that never started has no reference
void http_cerver_end (HttpCerver *http_cerver) {

	if (http_cerver && http_cerver->global_reference) {
		http_cervers_remove (http_cerver);
	}

}

#pragma endregion

#pragma region public

static HttpStaticPath *http_static_path_new (void) {

	HttpStaticPath *http_static_path = (HttpStaticPath *) malloc (sizeof (HttpStaticPath));
	if (http_static_path) {
		http_static_path->path = NULL;
		http_static_path->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;

		http_static_path->decode_data = NULL;
		http_static_path->delete_decoded_data = NULL;

		http_static_path->authentication_handler = NULL;
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

// sets the authentication of the files of a whole static path
void http_static_path_set_auth (
	HttpStaticPath *static_path, HttpRouteAuthType auth_type
) {

	if (static_path) static_path->auth_type = auth_type;

}

void http_static_path_set_decode_data (
	HttpStaticPath *static_path,
	void *(*decode_data)(void *),
	void (*delete_decoded_data)(void *)
) {

	if (static_path) {
		static_path->decode_data = decode_data;
		static_path->delete_decoded_data = delete_decoded_data;
	}

}

void http_static_path_set_authentication_handler (
	HttpStaticPath *static_path,
	HttpAuthenticationHandler authentication_handler
) {

	if (static_path) static_path->authentication_handler = authentication_handler;

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

		(void) dlist_insert_at_end (http_cerver->static_paths, http_static_path);
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
		// the list's comparator receives static paths
		HttpStaticPath *query = http_static_path_create (static_path);

		void *data = query ? dlist_remove (
			http_cerver->static_paths,
			query,
			NULL
		) : NULL;

		if (data) {
			http_static_path_delete (data);
			retval = 0;
		}

		http_static_path_delete (query);
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
		(void) dlist_insert_at_end (http_cerver->routes, route);
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

void http_cerver_set_method_not_allowed (
	HttpCerver *http_cerver, const bool method_not_allowed
) {

	if (http_cerver) {
		http_cerver->tree->method_not_allowed = method_not_allowed;
	}

}

void http_cerver_set_method_not_allowed_handler (
	HttpCerver *http_cerver, const HttpHandler handler
) {

	if (http_cerver) {
		http_cerver->tree->method_not_allowed_handler = handler;
	}

}

void http_cerver_set_trailing_slash (
	HttpCerver *http_cerver, const HttpTrailingSlash trailing_slash
) {

	if (
		http_cerver
		&& (trailing_slash > HTTP_TRAILING_SLASH_NONE)
		&& (trailing_slash < HTTP_TRAILING_SLASH_MAX)
	) {
		http_cerver->tree->trailing_slash = trailing_slash;
	}

}

void http_cerver_set_max_route_params (
	HttpCerver *http_cerver, const unsigned int max_params
) {

	if (http_cerver && max_params) {
		http_cerver->tree->max_params = max_params;
	}

}

u8 http_cerver_register_param_validator (
	HttpCerver *http_cerver, const char *name, const HttpParamValidate validate
) {

	u8 retval = 1;

	if (http_cerver) {
		HttpParamValidator *validator = http_param_validator_create (name, validate);
		if (validator) {
			retval = (u8) dlist_insert_at_end_unsafe (http_cerver->tree->validators, validator);
		}
	}

	return retval;

}

u8 http_cerver_add_before (
	HttpCerver *http_cerver, const HttpBeforeMiddleware middleware
) {

	return http_cerver ? http_middlewares_add_before (&http_cerver->tree->middlewares, middleware) : 1;

}

u8 http_cerver_add_after (
	HttpCerver *http_cerver, const HttpAfterMiddleware middleware
) {

	return http_cerver ? http_middlewares_add_after (&http_cerver->tree->middlewares, middleware) : 1;

}

const char *http_cerver_routes_error (
	const HttpCerver *http_cerver
) {

	return (http_cerver && http_cerver->tree->n_errors) ? http_cerver->tree->error : NULL;

}

size_t http_cerver_routes_errors_count (
	const HttpCerver *http_cerver
) {

	return http_cerver ? http_cerver->tree->n_errors : 0;

}

u8 http_cerver_routes_compile (
	HttpCerver *http_cerver
) {

	u8 retval = 1;

	if (http_cerver) {
		http_cerver_init_admin_routes (http_cerver);

		retval = http_tree_compile (http_cerver->tree);
	}

	return retval;

}

void http_cerver_routes_print (
	const HttpCerver *http_cerver
) {

	if (http_cerver) http_tree_print (http_cerver->tree);

}

char *http_cerver_routes_generate_json (
	const HttpCerver *http_cerver
) {

	return http_cerver ? http_tree_generate_json (http_cerver->tree) : NULL;

}

void http_cerver_routes_json_delete (char *json) {

	free (json);

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

// gives the request its number in the cerver the first time that a generator asks for it,
// so the directory and the files of a request share it
// a receive without its HTTP cerver has the number 0
static size_t http_receive_upload_sequence (const HttpReceive *http_receive) {

	HttpReceive *receive = (HttpReceive *) http_receive;

	if (!receive->upload_sequence && receive->http_cerver) {
		receive->upload_sequence = __atomic_add_fetch (
			&receive->http_cerver->uploads_sequence, 1, __ATOMIC_RELAXED
		);
	}

	return receive->upload_sequence;

}

// method that can be used to generate multi-part uploads filenames
// with format "%d-%ld-%zu-%u-%s" using "sock_fd-time (NULL)-sequence-file index-multi_part->filename"
// the sequence is unique for each request of the cerver, even if its fd is reused in the same second,
// and the file index keeps unique the files of a request that have the same filename
void http_cerver_default_uploads_filename_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	const MultiPart *mpart = http_request_get_current_mpart (request);

	http_multi_part_set_generated_filename (
		mpart,
		"%d-%ld-%zu-%u-%s",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL),
		http_receive_upload_sequence (http_receive),
		(unsigned int) request->n_files,
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
// with format "%d-%ld-%zu" using "sock_fd-time (NULL)-sequence"
// the files of the request share its sequence
void http_cerver_default_uploads_dirname_generator (
	const HttpReceive *http_receive,
	const HttpRequest *request
) {

	http_request_set_dirname (
		request,
		"%d-%ld-%zu",
		http_receive->cr->connection->socket->sock_fd,
		time (NULL),
		http_receive_upload_sequence (http_receive)
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

#pragma region limits

// the limits are tuned by measuring each service, always with powers of 2
static void http_cerver_limit_check_power_of_two (
	const char *limit_name, const size_t value
) {

	if (value && (value & (value - 1))) {
		cerver_log_warning (
			"http_cerver_set_%s () - %zu is not a power of 2",
			limit_name, value
		);
	}

}

void http_cerver_set_max_body_size (
	HttpCerver *http_cerver, const size_t max_body_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_body_size", max_body_size);
		http_cerver->max_body_size = max_body_size;
	}

}

void http_cerver_set_max_url_size (
	HttpCerver *http_cerver, const size_t max_url_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_url_size", max_url_size);
		http_cerver->max_url_size = max_url_size ?
			max_url_size : HTTP_CERVER_DEFAULT_MAX_URL_SIZE;
	}

}

void http_cerver_set_max_header_size (
	HttpCerver *http_cerver, const size_t max_header_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_header_size", max_header_size);
		http_cerver->max_header_size = max_header_size ?
			max_header_size : HTTP_CERVER_DEFAULT_MAX_HEADER_SIZE;
	}

}

void http_cerver_set_max_headers (
	HttpCerver *http_cerver, const unsigned int max_headers
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_headers", max_headers);
		http_cerver->max_headers = max_headers;
	}

}

void http_cerver_set_max_multipart_parts (
	HttpCerver *http_cerver, const unsigned int max_multipart_parts
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_multipart_parts", max_multipart_parts);
		http_cerver->max_multipart_parts = max_multipart_parts;
	}

}

void http_cerver_set_max_file_size (
	HttpCerver *http_cerver, const size_t max_file_size
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("max_file_size", max_file_size);
		http_cerver->max_file_size = max_file_size;
	}

}

// the milliseconds of a timeout in secs, the most that fits in an unsigned int
static unsigned int http_cerver_timeout_milliseconds (const unsigned int timeout) {

	const unsigned int max_seconds = UINT_MAX / HTTP_MILLISECONDS_PER_SECOND;

	return (timeout > max_seconds) ? UINT_MAX : timeout * HTTP_MILLISECONDS_PER_SECOND;

}

void http_cerver_set_headers_timeout (
	HttpCerver *http_cerver, const unsigned int headers_timeout
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("headers_timeout", headers_timeout);
		http_cerver->headers_timeout = http_cerver_timeout_milliseconds (headers_timeout);
	}

}

void http_cerver_set_headers_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int headers_timeout
) {

	if (http_cerver) http_cerver->headers_timeout = headers_timeout;

}

void http_cerver_set_request_timeout (
	HttpCerver *http_cerver, const unsigned int request_timeout
) {

	if (http_cerver) {
		http_cerver_limit_check_power_of_two ("request_timeout", request_timeout);
		http_cerver->request_timeout = http_cerver_timeout_milliseconds (request_timeout);
	}

}

void http_cerver_set_request_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int request_timeout
) {

	if (http_cerver) http_cerver->request_timeout = request_timeout;

}

void http_cerver_set_idle_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int idle_timeout
) {

	if (http_cerver) {
		http_cerver->idle_timeout = idle_timeout ?
			idle_timeout : HTTP_CERVER_DEFAULT_IDLE_TIMEOUT_MILLISECONDS;
	}

}

void http_cerver_set_send_timeout_milliseconds (
	HttpCerver *http_cerver, const unsigned int send_timeout
) {

	if (http_cerver) http_cerver->send_timeout = send_timeout;

}

void http_cerver_set_half_close_is_gone (
	HttpCerver *http_cerver, const bool half_close_is_gone
) {

	if (http_cerver) http_cerver->half_close_is_gone = half_close_is_gone;

}

static void http_cerver_parser_lenient_warning (
	const HttpCerver *http_cerver, const HttpParserLenient lenient
) {

	if (http_cerver->parser_lenient & (unsigned int) lenient) {
		cerver_log_warning (
			"HTTP cerver - the parser accepts %s (lenient %s), "
			"it can open the cerver to request smuggling or cache poisoning attacks",
			http_parser_lenient_description (lenient), http_parser_lenient_string (lenient)
		);
	}

}

void http_cerver_set_parser_lenient (
	HttpCerver *http_cerver, const unsigned int lenient
) {

	if (http_cerver) {
		http_cerver->parser_lenient = lenient & HTTP_PARSER_LENIENT_ALL;

		#define XX(num, name, string, description) \
			http_cerver_parser_lenient_warning (http_cerver, HTTP_PARSER_LENIENT_##name);
		LLHTTP_LENIENT_MAP(XX)
		#undef XX
	}

}

#pragma endregion

#pragma region headers

void http_cerver_set_date_header (
	HttpCerver *http_cerver, const bool date_header
) {

	if (http_cerver) http_cerver->date_header = date_header;

}

void http_cerver_set_request_id (
	HttpCerver *http_cerver, const bool request_id
) {

	if (http_cerver) http_cerver->request_id = request_id;

}

void http_cerver_set_compression_min_size (
	HttpCerver *http_cerver, const size_t min_size
) {

	if (http_cerver) http_cerver->compression_min_size = min_size;

}

void http_cerver_set_compression_level (
	HttpCerver *http_cerver, const int level
) {

	if (http_cerver) {
		if ((level >= HTTP_COMPRESSION_MIN_LEVEL) && (level <= HTTP_COMPRESSION_MAX_LEVEL)) {
			http_cerver->compression_level = level;
		}

		else {
			cerver_log_warning (
				"http_cerver_set_compression_level () - %d is not between %d and %d",
				level, HTTP_COMPRESSION_MIN_LEVEL, HTTP_COMPRESSION_MAX_LEVEL
			);
		}
	}

}

#pragma endregion

#pragma region proxies

void http_cerver_set_trusted_proxies (
	HttpCerver *http_cerver, const HttpTrustedProxies trusted_proxies
) {

	if (http_cerver) http_cerver->trusted_proxies = trusted_proxies;

}

u8 http_cerver_add_trusted_proxy (
	HttpCerver *http_cerver, const char *cidr
) {

	u8 retval = 1;

	if (http_cerver) {
		if (http_cerver->n_trusted_proxies >= HTTP_TRUSTED_PROXIES_SIZE) {
			http_tree_error (
				http_cerver->tree, "Trusted proxy %s exceeds the %d networks",
				cidr ? cidr : "NULL", HTTP_TRUSTED_PROXIES_SIZE
			);
		}

		else if (!http_proxy_network_parse (
			cidr, &http_cerver->trusted_proxies_networks[http_cerver->n_trusted_proxies]
		)) {
			http_tree_error (
				http_cerver->tree, "Trusted proxy %s is not a valid network",
				cidr ? cidr : "NULL"
			);
		}

		else {
			http_cerver->n_trusted_proxies += 1;
			retval = 0;
		}
	}

	return retval;

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

// the algorithms of the tokens of a cerver always sign them
static bool http_cerver_auth_jwt_algorithm_is_valid (const jwt_alg_t jwt_alg) {

	return (jwt_alg > JWT_ALG_NONE) && (jwt_alg < JWT_ALG_INVAL);

}

// sets the jwt algorithm used for encoding & decoding jwt tokens
// the default value is JWT_ALG_HS256, JWT_ALG_NONE is not accepted
// returns 0 on success, 1 on error
u8 http_cerver_auth_set_jwt_algorithm (
	HttpCerver *http_cerver, const jwt_alg_t jwt_alg
) {

	u8 retval = 1;

	if (http_cerver && http_cerver_auth_jwt_algorithm_is_valid (jwt_alg)) {
		http_cerver->jwt_alg = jwt_alg;

		retval = 0;
	}

	return retval;

}

// a setter called again replaces the value it set before
static void http_cerver_auth_set_string (
	String **destination, const char *value
) {

	str_delete (*destination);

	*destination = value ? str_new (value) : NULL;

}

// sets the filename from where the jwt private key will be loaded
void http_cerver_auth_set_jwt_priv_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_opt_key_name, filename);
	}

}

// sets the filename from where the jwt public key will be loaded
void http_cerver_auth_set_jwt_pub_key_filename (
	HttpCerver *http_cerver, const char *filename
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_opt_pub_key_name, filename);
	}

}

// sets the longest bearer token that is decoded,
// a longer one is rejected without decoding it
// the default value is HTTP_CERVER_DEFAULT_JWT_MAX_SIZE, 0 has no limit
void http_cerver_auth_set_jwt_max_size (
	HttpCerver *http_cerver, const size_t max_size
) {

	if (http_cerver) http_cerver->jwt_max_size = max_size;

}

// sets if a token without "exp" is rejected
// the default value is HTTP_CERVER_DEFAULT_JWT_REQUIRE_EXPIRATION
void http_cerver_auth_set_jwt_require_expiration (
	HttpCerver *http_cerver, const bool require
) {

	if (http_cerver) http_cerver->jwt_require_expiration = require;

}

// sets the seconds of clock skew that are accepted in "exp" and "nbf"
// the default value is HTTP_CERVER_DEFAULT_JWT_LEEWAY_SECONDS
void http_cerver_auth_set_jwt_leeway_seconds (
	HttpCerver *http_cerver, const unsigned int leeway_seconds
) {

	if (http_cerver) http_cerver->jwt_leeway_seconds = leeway_seconds;

}

// sets the "iss" that every token must have, NULL accepts any
void http_cerver_auth_set_jwt_issuer (
	HttpCerver *http_cerver, const char *issuer
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_issuer, issuer);
	}

}

// sets the "aud" that every token must have, as its value or in its array,
// NULL accepts any
void http_cerver_auth_set_jwt_audience (
	HttpCerver *http_cerver, const char *audience
) {

	if (http_cerver) {
		http_cerver_auth_set_string (&http_cerver->jwt_audience, audience);
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

	return (http_cerver && actual_header) ? http_headers_set (
		&http_cerver->response_headers, type, actual_header, strlen (actual_header)
	) : 1;

}

// works like http_cerver_add_responses_header () with the name of any header,
// it replaces the ones with the same name
// returns 0 on success, 1 on error
u8 http_cerver_add_responses_header_by_name (
	HttpCerver *http_cerver, const char *name, const char *value
) {

	return (http_cerver && name && value) ? http_headers_set_by_name (
		&http_cerver->response_headers, name, strlen (name), value, strlen (value)
	) : 1;

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
		cerver_log_msg ("Main routes: %zu", http_cerver->routes->size);

		size_t total_handlers = 0;
		size_t children_routes = http_cerver_stats_get_children_routes (
			http_cerver, &total_handlers
		);

		cerver_log_msg ("Children routes: %zu", children_routes);
		cerver_log_msg ("Total handlers: %zu", total_handlers);
	}

}

static void http_cerver_route_handler_stats_print (
	const RequestMethod method, const HttpRouteStats *route_stats
) {

	HttpRouteStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteStats));
	http_route_stats_snapshot (route_stats, &snapshot);
	const HttpRouteStats *stats = &snapshot;

	cerver_log_msg (
		"\t\t%s\t%zu",
		http_request_method_str (method),
		stats->n_requests
	);

	// route stats
	cerver_log_msg ("\t\tMin process time: %.4f secs", stats->first ? 0 : stats->min_process_time);
	cerver_log_msg ("\t\tMax process time: %.4f secs", stats->max_process_time);
	cerver_log_msg ("\t\tMean process time: %.4f secs", stats->mean_process_time);

	cerver_log_msg ("\t\tMin request size: %zu", stats->first ? 0 : stats->min_request_size);
	cerver_log_msg ("\t\tMax request size: %zu", stats->max_request_size);
	cerver_log_msg ("\t\tMean request size: %zu", stats->mean_request_size);

	cerver_log_msg ("\t\tMin response size: %zu", stats->first ? 0 : stats->min_response_size);
	cerver_log_msg ("\t\tMax response size: %zu", stats->max_response_size);
	cerver_log_msg ("\t\tMean response size: %zu", stats->mean_response_size);

}

static void http_cerver_route_file_stats_print (
	const HttpRouteFileStats *route_file_stats
) {

	HttpRouteFileStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteFileStats));
	http_route_file_stats_snapshot (route_file_stats, &snapshot);
	const HttpRouteFileStats *file_stats = &snapshot;

	cerver_log_line_break ();
	cerver_log_msg ("\t\tUploaded files: %zu", file_stats->n_uploaded_files);

	cerver_log_msg ("\t\tMin n files: %zu", file_stats->first ? 0 : file_stats->min_n_files);
	cerver_log_msg ("\t\tMax n files: %zu", file_stats->max_n_files);
	cerver_log_msg ("\t\tMean n files: %.2f", file_stats->mean_n_files);

	cerver_log_msg ("\t\tMin file size: %zu", file_stats->first ? 0 : file_stats->min_file_size);
	cerver_log_msg ("\t\tMax file size: %zu", file_stats->max_file_size);
	cerver_log_msg ("\t\tMean file size: %.2f", file_stats->mean_file_size);

}

static void http_cerver_child_route_handler_stats_print (
	const RequestMethod method, const HttpRouteStats *route_stats
) {

	HttpRouteStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteStats));
	http_route_stats_snapshot (route_stats, &snapshot);
	const HttpRouteStats *stats = &snapshot;

	cerver_log_msg (
		"\t\t\t%s\t%zu",
		http_request_method_str (method),
		stats->n_requests
	);

	cerver_log_msg ("\t\t\tMin process time: %.4f secs", stats->first ? 0 : stats->min_process_time);
	cerver_log_msg ("\t\t\tMax process time: %.4f secs", stats->max_process_time);
	cerver_log_msg ("\t\t\tMean process time: %.4f secs", stats->mean_process_time);

	cerver_log_msg ("\t\t\tMin request size: %zu", stats->first ? 0 : stats->min_request_size);
	cerver_log_msg ("\t\t\tMax request size: %zu", stats->max_request_size);
	cerver_log_msg ("\t\t\tMean request size: %zu", stats->mean_request_size);

	cerver_log_msg ("\t\t\tMin response size: %zu", stats->first ? 0 : stats->min_response_size);
	cerver_log_msg ("\t\t\tMax response size: %zu", stats->max_response_size);
	cerver_log_msg ("\t\t\tMean response size: %zu", stats->mean_response_size);

}

static void http_cerver_child_route_file_stats_print (
	const HttpRouteFileStats *route_file_stats
) {

	HttpRouteFileStats snapshot;
	(void) memset (&snapshot, 0, sizeof (HttpRouteFileStats));
	http_route_file_stats_snapshot (route_file_stats, &snapshot);
	const HttpRouteFileStats *file_stats = &snapshot;

	cerver_log_msg ("\t\t\tUploaded files: %zu", file_stats->n_uploaded_files);

	cerver_log_msg ("\t\t\tMin n files: %zu", file_stats->first ? 0 : file_stats->min_n_files);
	cerver_log_msg ("\t\t\tMax n files: %zu", file_stats->max_n_files);
	cerver_log_msg ("\t\t\tMean n files: %.2f", file_stats->mean_n_files);

	cerver_log_msg ("\t\t\tMin file size: %zu", file_stats->first ? 0 : file_stats->min_file_size);
	cerver_log_msg ("\t\t\tMax file size: %zu", file_stats->max_file_size);
	cerver_log_msg ("\t\t\tMean file size: %.2f", file_stats->mean_file_size);

}

static void http_cerver_route_children_stats_print (
	const HttpRoute *route
) {

	cerver_log_msg ("\t\t%zu children:", route->children->size);

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
// the responses sent by their status code, only the codes that were sent
static void http_cerver_responses_stats_print (const HttpCerver *http_cerver) {

	for (size_t status = 0; status < HTTP_CERVER_RESPONSES_STATUS_SIZE; status++) {
		const size_t count = CERVER_STATS_GET (http_cerver->n_responses_by_status[status]);
		if (count) cerver_log_msg ("Responses %zu: %zu", status, count);
	}

}

// the requests whose client left and the ones that vanished, only the stages that have any
static void http_cerver_gone_stats_print (const HttpCerver *http_cerver) {

	for (unsigned int stage = 0; stage < HTTP_STAGES; stage++) {
		for (unsigned int reason = 0; reason < HTTP_ABORT_REASONS; reason++) {
			const size_t aborted = CERVER_STATS_GET (http_cerver->n_aborted_requests[stage][reason]);
			if (aborted) {
				cerver_log_msg (
					"Aborted requests (%s, %s): %zu",
					http_stage_string ((HttpStage) stage), http_abort_reason_string ((HttpAbortReason) reason),
					aborted
				);
			}
		}

		const size_t vanished = CERVER_STATS_GET (http_cerver->n_vanished_requests[stage]);
		if (vanished) {
			cerver_log_msg ("Vanished requests (%s): %zu", http_stage_string ((HttpStage) stage), vanished);
		}
	}

}

void http_cerver_all_stats_print (const HttpCerver *http_cerver) {

	if (http_cerver) {
		http_cerver_routes_stats_print (http_cerver);

		// print stats by route
		for (ListElement *le = dlist_start (http_cerver->routes); le; le = le->next) {
			http_cerver_route_stats_print ((HttpRoute *) le->data);
		}

		// print general stats
		cerver_log_msg ("Incompleted requests: %zu", CERVER_STATS_GET (http_cerver->n_incompleted_requests));
		cerver_log_msg ("Unhandled requests: %zu", CERVER_STATS_GET (http_cerver->n_unhandled_requests));

		cerver_log_msg ("Catch requests: %zu", CERVER_STATS_GET (http_cerver->n_catch_all_requests));
		cerver_log_msg ("Failed auth requests: %zu", CERVER_STATS_GET (http_cerver->n_failed_auth_requests));

		cerver_log_msg ("Limited requests: %zu", CERVER_STATS_GET (http_cerver->n_limited_requests));
		cerver_log_msg ("Timeout requests: %zu", CERVER_STATS_GET (http_cerver->n_timeout_requests));
		cerver_log_msg ("Unanswered requests: %zu", CERVER_STATS_GET (http_cerver->n_unanswered_requests));
		cerver_log_msg ("Failed responses: %zu", CERVER_STATS_GET (http_cerver->n_failed_responses));

		http_cerver_responses_stats_print (http_cerver);

		http_cerver_gone_stats_print (http_cerver);
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

// sets a custom data reference to be set in all admin routes
void http_cerver_admin_routes_set_custom_data (
	HttpCerver *http_cerver, void *custom_data
) {

	if (http_cerver) {
		http_cerver->admin_routes_custom_data = custom_data;
	}

}

// sets how the custom data will be disposed when the service gets destroyed
void http_cerver_admin_routes_set_delete_custom_data (
	HttpCerver *http_cerver, void (*delete_custom_data)(void *)
) {

	if (http_cerver) {
		http_cerver->delete_admin_routes_custom_data = delete_custom_data;
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

// sends a response before the request was completed
// and marks it, so the rest of the request is not handled
// returns 1, a parser callback answers it with HTTP_RECEIVE_PARSER_STOP
static int http_receive_reject (
	HttpReceive *http_receive,
	HttpResponse *response, const HttpReceiveStatus status
) {

	if (!http_receive->responded) {
		(void) http_response_send (response, http_receive);

		http_receive->responded = true;
		http_receive->receive_status = status;

		if (status == HTTP_RECEIVE_STATUS_LIMIT) {
			(void) pthread_mutex_lock (http_receive->http_cerver->mutex);
			http_receive->http_cerver->n_limited_requests += 1;
			(void) pthread_mutex_unlock (http_receive->http_cerver->mutex);
		}
	}

	return 1;

}

// keeps the string's memory but empties its contents
static inline void http_receive_buffer_reset (String *buffer) {

	if (buffer) {
		buffer->len = 0;
		if (buffer->str) buffer->str[0] = '\0';
	}

}

// adds a piece of a token to its buffer
// returns 0 on success, 1 on error
static inline int http_receive_buffer_append (
	String **buffer, const char *at, const size_t length
) {

	if (!*buffer) *buffer = str_new (NULL);

	return *buffer ? str_append_bytes (*buffer, at, length) : 1;

}

// splits the url that was received into the request's path and query
// the url, the query and the headers of a request in an arena are created in it
static String *http_receive_string_create (
	const HttpReceive *http_receive, const char *bytes, const size_t length
) {

	return http_receive->request->arena ?
		str_create_from_bytes_arena (http_receive->request->arena, bytes, length) :
		str_create_from_bytes (bytes, length);

}

static void http_receive_close_url (HttpReceive *http_receive) {

	HttpRequest *request = http_receive->request;

	if (!request->url) {
		const String *url = http_receive->url_buffer;
		const char *start = (url && url->str) ? url->str : "";
		const size_t url_len = url ? url->len : 0;

		const char *question = (const char *) memchr (start, '?', url_len);
		const size_t path_len = question ? (size_t) (question - start) : url_len;

		request->url = http_receive_string_create (http_receive, start, path_len);

		// the query does not include the '?'
		if (question) {
			request->query = http_receive_string_create (
				http_receive, question + 1, url_len - path_len - 1
			);
		}
	}

}

// these headers must appear only once (RFC 9110 section 5.3, RFC 6455 section 11.3)
static inline bool http_header_is_unique (const http_header type) {

	return (type == HTTP_HEADER_HOST)
		|| (type == HTTP_HEADER_CONTENT_TYPE)
		|| (type == HTTP_HEADER_CONTENT_LENGTH)
		|| (type == HTTP_HEADER_AUTHORIZATION)
		|| (type == HTTP_HEADER_SEC_WEBSOCKET_KEY)
		|| (type == HTTP_HEADER_SEC_WEBSOCKET_VERSION);

}

static int http_header_values_join_arena (
	Arena *arena, String **header, const char *value, const size_t value_len
);

// joins a new value to a custom header that was already received
// returns 0 on success, 1 if the joined value does not fit
static int http_receive_join_custom_header (
	HttpRequest *request,
	HttpCustomHeader *existing, const char *value, const size_t value_len
) {

	int retval = 1;

	const size_t new_len = existing->value->len
		+ HTTP_HEADER_VALUES_SEPARATOR_LEN + value_len;

	Arena *arena = http_request_get_arena (request);
	if (arena && (new_len < HTTP_CUSTOM_HEADER_VALUE_SIZE)) {
		retval = http_header_values_join_arena (arena, &existing->value, value, value_len);
	}

	return retval;

}

// adds a new custom header to the request
// returns 0 on success, 1 if the value does not fit
static int http_receive_add_custom_header (
	HttpRequest *request,
	const String *field, const char *value, const size_t value_len
) {

	int retval = 1;

	Arena *arena = http_request_get_arena (request);
	if (arena && (value_len < HTTP_CUSTOM_HEADER_VALUE_SIZE)) {
		HttpCustomHeader *previous = request->current_custom_header;
		http_request_set_current_custom_header (request, field->str);

		HttpCustomHeader *custom_header = request->current_custom_header;
		if (custom_header && (custom_header != previous)) {
			retval = (int) http_custom_header_set_value (arena, custom_header, value, value_len);
		}
	}

	return retval;

}

// saves a custom header, a repeated one is joined with HTTP_HEADER_VALUES_SEPARATOR
// returns 0 on success, 1 if the header does not fit
static int http_receive_close_custom_header (
	HttpRequest *request,
	const String *field, const char *value, const size_t value_len
) {

	if (field->len >= HTTP_CUSTOM_HEADER_SIZE) return 1;

	HttpCustomHeader *existing = http_request_custom_header_find (request, field->str);

	return existing ?
		http_receive_join_custom_header (request, existing, value, value_len) :
		http_receive_add_custom_header (request, field, value, value_len);

}

// joins a new value to a header that was already received
// returns 0 on success, 1 on error
static inline int http_header_values_join (
	String *header, const char *value, const size_t value_len
) {

	return str_append_bytes (header, HTTP_HEADER_VALUES_SEPARATOR, HTTP_HEADER_VALUES_SEPARATOR_LEN)
		|| str_append_bytes (header, value, value_len);

}

// a string in an arena can not grow, so the joined value is a new string in it
static int http_header_values_join_arena (
	Arena *arena, String **header, const char *value, const size_t value_len
) {

	int retval = 1;

	const String *current = *header;
	const size_t length = current->len + HTTP_HEADER_VALUES_SEPARATOR_LEN + value_len;

	// the same layout as str_create_from_bytes_arena ()
	String *joined = (String *) arena_alloc (arena, sizeof (String) + length + 1);
	if (joined) {
		joined->max_len = 0;
		joined->len = length;
		joined->str = ((char *) joined) + sizeof (String);

		(void) memcpy (joined->str, current->str, current->len);
		(void) memcpy (joined->str + current->len, HTTP_HEADER_VALUES_SEPARATOR, HTTP_HEADER_VALUES_SEPARATOR_LEN);
		(void) memcpy (joined->str + current->len + HTTP_HEADER_VALUES_SEPARATOR_LEN, value, value_len);
		joined->str[length] = '\0';

		*header = joined;
		retval = 0;
	}

	return retval;

}

static int http_receive_header_join (
	HttpRequest *request, const http_header type,
	const char *value, const size_t value_len
) {

	return request->arena ?
		http_header_values_join_arena (request->arena, &request->headers[type], value, value_len) :
		http_header_values_join (request->headers[type], value, value_len);

}

// saves a header of the HTTP headers map
// a repeated header is joined with HTTP_HEADER_VALUES_SEPARATOR, unless it must be unique
// returns 0 on success, 1 if the request was rejected
static int http_receive_close_known_header (
	HttpReceive *http_receive, const http_header type,
	const char *value, const size_t value_len
) {

	int retval = 0;

	HttpRequest *request = http_receive->request;

	if (!request->headers[type]) {
		request->headers[type] = http_receive_string_create (http_receive, value, value_len);
	}

	else if (http_header_is_unique (type)) {
		retval = http_receive_reject (
			http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	else if (http_receive_header_join (request, type, value, value_len)) {
		retval = http_receive_reject (
			http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	return retval;

}

// saves a complete header in the request
// returns 0 on success, 1 if the request was rejected
static int http_receive_save_header (
	HttpReceive *http_receive, const String *field
) {

	const String *value_buffer = http_receive->value_buffer;
	const char *value = (value_buffer && value_buffer->str) ? value_buffer->str : "";
	const size_t value_len = value_buffer ? value_buffer->len : 0;

	http_receive->n_headers += 1;

	const unsigned int max_headers = http_receive->http_cerver->max_headers;
	if (max_headers && (http_receive->n_headers > max_headers)) {
		return http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);
	}

	const http_header type = http_header_type_by_string (field->str);
	if (type != HTTP_HEADER_UNDEFINED) {
		return http_receive_close_known_header (http_receive, type, value, value_len);
	}

	return http_receive_close_custom_header (http_receive->request, field, value, value_len) ?
		http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}

// closes the header that was received in pieces
// returns 0 on success, 1 if the request was rejected
static int http_receive_close_header (HttpReceive *http_receive) {

	int retval = 0;

	const String *field = http_receive->field_buffer;
	if (field && field->len) {
		retval = http_receive_save_header (http_receive, field);
	}

	http_receive_buffer_reset (http_receive->field_buffer);
	http_receive_buffer_reset (http_receive->value_buffer);

	return retval;

}

// marks that the body is being received and checks its size
// returns 0 on success, 1 if the request was rejected
static int http_receive_check_body_size (
	HttpReceive *http_receive, const size_t length
) {

	if (http_receive->receive_status == HTTP_RECEIVE_STATUS_HEADERS)
		http_receive->receive_status = HTTP_RECEIVE_STATUS_BODY;

	http_receive->body_size += length;

	const size_t max_body_size = http_receive->max_body_size;

	return (max_body_size && (http_receive->body_size > max_body_size)) ?
		http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

}

// the bytes to reserve for the body when its first part arrives,
// the parser has already discounted the part from its content length
// a chunked body or one without Content-Length is not reserved
static size_t http_request_body_reserve_size (
	const HttpReceive *http_receive, const size_t length
) {

	const llhttp_t *parser = &http_receive->parser;

	if ((parser->flags & F_CHUNKED) || !(parser->flags & F_CONTENT_LENGTH)) return 0;

	uint64_t reserve = (uint64_t) length + parser->content_length;

	if (reserve > HTTP_REQUEST_BODY_RESERVE_LIMIT) reserve = HTTP_REQUEST_BODY_RESERVE_LIMIT;

	const size_t max_body_size = http_receive->max_body_size;
	if (max_body_size && (reserve > max_body_size)) reserve = max_body_size;

	// the '\0' of the body
	return (size_t) reserve + 1;

}

// creates the body with the bytes of its Content-Length reserved,
// so its next parts do not reallocate it
// returns 0 on success, 1 on error
static int http_request_body_create (
	HttpReceive *http_receive, const char *at, const size_t length
) {

	HttpRequest *request = http_receive->request;

	// the string of the body lives in the arena of the request when it has one,
	// and its buffer is taken from the ones that the thread reuses
	String *body = request->arena ?
		(String *) arena_alloc (request->arena, sizeof (String)) :
		(String *) malloc (sizeof (String));

	if (!body) return 1;

	const size_t reserve = http_request_body_reserve_size (http_receive, length);

	size_t capacity = 0;
	body->str = (char *) http_recycle_buffer_take (reserve, &capacity);
	if (!body->str) {
		if (!request->arena) free (body);
		return 1;
	}

	body->str[0] = '\0';
	body->len = 0;
	body->max_len = capacity;

	request->body = body;

	return str_append_bytes (request->body, at, length);

}

// the body is binary, it can contain '\0', and it is always terminated with '\0'
// returns 0 on success, 1 on error
static int http_request_body_append (
	HttpReceive *http_receive, const char *at, const size_t length
) {

	HttpRequest *request = http_receive->request;

	return request->body ?
		str_append_bytes (request->body, at, length) :
		http_request_body_create (http_receive, at, length);

}

// checks the size of all the headers that have been received
static inline int http_receive_check_header_size (
	HttpReceive *http_receive, const size_t length
) {

	http_receive->header_size += length;

	const size_t max_header_size = http_receive->http_cerver->max_header_size;

	return (max_header_size && (http_receive->header_size > max_header_size)) ?
		http_receive_reject (
			http_receive, header_fields_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		) : 0;

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

					(void) dlist_insert_at_end (
						pairs,
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

		(void) dlist_insert_at_end (
			pairs,
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

// the value that a parser callback returns when it rejected the request
// or when it found that its client is gone, the parser stops with it
static inline int http_receive_parser_result (const int rejected) {

	return rejected ? HTTP_RECEIVE_PARSER_STOP : 0;

}

// the url can arrive in many pieces, it is closed in on_url_complete
static int http_receive_handle_url (
	llhttp_t *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	const size_t current_len = http_receive->url_buffer ? http_receive->url_buffer->len : 0;
	const size_t max_url_size = http_receive->http_cerver->max_url_size;
	if (max_url_size && ((current_len + length) > max_url_size)) {
		return http_receive_parser_result (http_receive_reject (
			http_receive, uri_too_long_error, HTTP_RECEIVE_STATUS_LIMIT
		));
	}

	return http_receive_parser_result (
		http_receive_buffer_append (&http_receive->url_buffer, at, length) ?
			http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

}

// the url is complete, it is split into the request's path and query
static int http_receive_handle_url_complete (llhttp_t *parser) {

	http_receive_close_url ((HttpReceive *) parser->data);

	return 0;

}

// the fields that the parser finds after on_headers_complete are the trailers of a chunked body
static inline bool http_receive_headers_are_complete (const HttpReceive *http_receive) {

	return http_receive->receive_status != HTTP_RECEIVE_STATUS_NONE;

}

// a header field can arrive in many pieces, it is closed with its value
static int http_receive_handle_header_field (
	llhttp_t *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	// the trailers of a chunked body are not used
	if (http_receive_headers_are_complete (http_receive)) return 0;

	// the first field ends the request line
	http_receive->stage = HTTP_STAGE_HEADERS;

	if (http_receive_check_header_size (http_receive, length)) return HTTP_RECEIVE_PARSER_STOP;

	return http_receive_parser_result (
		http_receive_buffer_append (&http_receive->field_buffer, at, length) ?
			http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

}

// a header value can arrive in many pieces, it is closed in on_header_value_complete
static int http_receive_handle_header_value (
	llhttp_t *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_headers_are_complete (http_receive)) return 0;

	if (http_receive_check_header_size (http_receive, length)) return HTTP_RECEIVE_PARSER_STOP;

	return http_receive_parser_result (
		http_receive_buffer_append (&http_receive->value_buffer, at, length) ?
			http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

}

// the field and the value are complete, the header is saved in the request
// the trailers of a chunked body are discarded
static int http_receive_handle_header_value_complete (llhttp_t *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	int rejected = 0;
	if (http_receive_headers_are_complete (http_receive)) {
		http_receive_buffer_reset (http_receive->field_buffer);
		http_receive_buffer_reset (http_receive->value_buffer);
	}

	else {
		rejected = http_receive_close_header (http_receive);
	}

	return http_receive_parser_result (rejected);

}

static int http_receive_handle_body (
	llhttp_t *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_check_body_size (http_receive, length)) return HTTP_RECEIVE_PARSER_STOP;

	return http_receive_parser_result (
		http_request_body_append (http_receive, at, length) ?
			http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

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

// released by the last HTTP cerver of the process, after its requests ended
static void http_multi_parts_end_pool (void) {

	pool_delete (multi_parts_pool);
	multi_parts_pool = NULL;

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

		// an attribute without a value ("form-data; name") is ignored
		if (!value) continue;

		(void) dlist_insert_at_end (
			attributes,
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

// a file that could not be saved completely (for example, the uploads file system is full)
// is never reported as saved: the part keeps the error and the request is answered with 500
// returns 1 to stop the parser
static int http_receive_mpart_file_failed (
	HttpReceive *http_receive, MultiPart *multi_part,
	const char *operation, const int error
) {

	multi_part->error = error;

	char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
	cerver_log_error (
		"Failed to %s %s to save multipart file - errno %d (%s) - wrote %u",
		operation, multi_part->saved_filename, error,
		strerror_r (error, error_description, sizeof (error_description)),
		multi_part->total_wrote
	);

	// a file that was never opened is not this request's,
	// it may be another one with the same name, so it is never discarded
	if (multi_part->fd == -1) multi_part->saved_filename_len = 0;

	return http_receive_reject (
		http_receive, server_error, HTTP_RECEIVE_STATUS_UPLOAD_ERROR
	);

}

// sets in the part's saved filename the original one with a number,
// before its extension if it has one: "uploads/name.png" -> "uploads/name-1.png"
// returns 0 on success, 1 if the new filename does not fit
static int http_receive_mpart_file_number (
	MultiPart *multi_part, const char *original, const unsigned int number
) {

	const char *basename = strrchr (original, '/');
	const char *extension = strrchr (basename ? basename : original, '.');
	const int name_len = extension ? (int) (extension - original) : (int) strlen (original);

	const int written = snprintf (
		multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
		"%.*s-%u%s", name_len, original, number, extension ? extension : ""
	);

	if ((written < 0) || (written >= HTTP_MULTI_PART_SAVED_FILENAME_SIZE)) return 1;

	multi_part->saved_filename_len = written;

	return 0;

}

// opens the file where the part is saved, a file is never overwritten:
// if the filename already exists, a number is added to it
// returns 0 on success, 1 if the request was rejected
static int http_receive_mpart_file_open (
	HttpReceive *http_receive, MultiPart *multi_part
) {

	char original[HTTP_MULTI_PART_SAVED_FILENAME_SIZE] = { 0 };
	(void) snprintf (original, HTTP_MULTI_PART_SAVED_FILENAME_SIZE, "%s", multi_part->saved_filename);

	const int flags = O_CREAT | O_EXCL | O_WRONLY;
	const unsigned int mode = http_receive->http_cerver->uploads_file_mode;

	multi_part->fd = open (multi_part->saved_filename, flags, mode);

	for (
		unsigned int number = 1;
		(multi_part->fd == -1) && (errno == EEXIST) && (number <= HTTP_MULTI_PART_FILENAME_NUMBERS);
		number++
	) {
		if (http_receive_mpart_file_number (multi_part, original, number)) break;

		multi_part->fd = open (multi_part->saved_filename, flags, mode);
	}

	return (multi_part->fd == -1) ?
		http_receive_mpart_file_failed (http_receive, multi_part, "open", errno) : 0;

}

// writes a piece of a multi-part file, write () can save less bytes than requested
// returns 0 on success, 1 if the request was rejected
static int http_receive_mpart_file_write (
	HttpReceive *http_receive, MultiPart *multi_part,
	const char *at, const size_t length
) {

	multi_part->n_reads += 1;

	const size_t max_file_size = http_receive->http_cerver->max_file_size;
	if (max_file_size && (((size_t) multi_part->total_wrote + length) > max_file_size)) {
		return http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);
	}

	const char *pending = at;
	size_t pending_length = length;
	while (pending_length > 0) {
		const ssize_t wrote = write (multi_part->fd, pending, pending_length);
		if (wrote < 0) {
			if (errno == EINTR) continue;

			return http_receive_mpart_file_failed (
				http_receive, multi_part, "write", errno
			);
		}

		multi_part->total_wrote += (u32) wrote;
		pending += wrote;
		pending_length -= (size_t) wrote;
	}

	return 0;

}

// adds a piece of a multi-part value, a value that does not fit is truncated
static int http_receive_mpart_value_append (
	MultiPart *multi_part, const char *at, const size_t length
) {

	char *end = multi_part->value + multi_part->value_len;

	(void) snprintf (
		end, HTTP_MULTI_PART_VALUE_SIZE - multi_part->value_len,
		"%.*s", (int) length, at
	);

	multi_part->value_len = (int) strlen (multi_part->value);

	return 0;

}

static int http_receive_handle_mpart_part_data_begin (
	multipart_parser *parser
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	HttpRequest *request = http_receive->request;

	http_receive->n_parts += 1;

	const unsigned int max_multipart_parts = http_receive->http_cerver->max_multipart_parts;
	if (max_multipart_parts && (http_receive->n_parts > max_multipart_parts)) {
		return http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);
	}

	// create a new multipart structure to handle new data
	request->current_part = http_multi_parts_get ();
	(void) dlist_insert_at_end_unsafe (
		request->multi_parts, request->current_part
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

// the data of the part is ignored
static int http_receive_mpart_discard (MultiPart *multi_part) {

	multi_part->discard = true;

	return 0;

}

// a name that is empty after it was sanitized, or that refers to a directory,
// is replaced, so the file is always saved inside the uploads path
static void http_receive_mpart_filename_fallback (MultiPart *multi_part) {

	const char *filename = multi_part->filename;

	if (!filename[0] || !strcmp (filename, ".") || !strcmp (filename, "..")) {
		(void) snprintf (
			multi_part->filename, HTTP_MULTI_PART_FILENAME_SIZE,
			"%s", HTTP_MULTI_PART_DEFAULT_FILENAME
		);
	}

}

// the name of the saved file: the one of the uploads filename generator,
// or the sanitized one of the part if there is no generator or it did not set one
static const char *http_receive_mpart_saved_name (
	HttpReceive *http_receive, MultiPart *multi_part
) {

	const char *name = multi_part->filename;

	if (http_receive->http_cerver->uploads_filename_generator) {
		multi_part->generated_filename_len = 0;
		multi_part->generated_filename[0] = '\0';

		http_receive->http_cerver->uploads_filename_generator (
			http_receive, http_receive->request
		);

		if (multi_part->generated_filename_len > 0) {
			name = multi_part->generated_filename;
		}

		else {
			cerver_log_warning (
				"The uploads filename generator did not set a name, using %s",
				multi_part->filename
			);
		}
	}

	return name;

}

// sets the path where the part is saved, inside the uploads path and the request's directory
// returns 0 on success, 1 if the path does not fit
static int http_receive_mpart_saved_filename (
	HttpReceive *http_receive, MultiPart *multi_part
) {

	const HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpRequest *request = http_receive->request;

	const char *name = http_receive_mpart_saved_name (http_receive, multi_part);

	const int written = (request->dirname_len > 0) ?
		snprintf (
			multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
			"%s/%s/%s", http_cerver->uploads_path, request->dirname, name
		) :
		snprintf (
			multi_part->saved_filename, HTTP_MULTI_PART_SAVED_FILENAME_SIZE,
			"%s/%s", http_cerver->uploads_path, name
		);

	const bool fits = (written > 0) && (written < HTTP_MULTI_PART_SAVED_FILENAME_SIZE);

	multi_part->saved_filename_len = fits ? written : 0;

	return fits ? 0 : 1;

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

			// the part already has an empty list from http_multi_part_new ()
			dlist_delete (multi_part->params);
			multi_part->params = http_mpart_attributes_parse (end);

			multi_part->name = key_value_pairs_get_value (multi_part->params, "name");
			// multi_part->filename = key_value_pairs_get_value (multi_part->params, "filename");
			const String *original_filename = key_value_pairs_get_value (
				multi_part->params, "filename"
			);

			if (original_filename) {
				multi_part->type = MULTI_PART_TYPE_FILE;

				// a file input without a file, there is nothing to save
				if (!original_filename->len) return http_receive_mpart_discard (multi_part);

				// stats
				http_receive->file_stats->n_uploaded_files += 1;

				// sanitize file
				(void) snprintf (
					multi_part->filename, HTTP_MULTI_PART_FILENAME_SIZE,
					"%s", original_filename->str
				);

				files_sanitize_filename (multi_part->filename);
				http_receive_mpart_filename_fallback (multi_part);

				multi_part->filename_len = (int) strlen (multi_part->filename);

				http_receive->request->n_files += 1;

				if (http_receive->http_cerver->uploads_path_len > 0) {
					if (http_receive_mpart_saved_filename (http_receive, multi_part)) {
						return http_receive_mpart_file_failed (
							http_receive, multi_part, "name", ENAMETOOLONG
						);
					}

					if (http_receive_mpart_file_open (http_receive, multi_part)) {
						return 1;
					}

					http_receive->stage = HTTP_STAGE_UPLOAD;
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

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = http_receive->request->current_part;

	if (multi_part->discard) return 0;

	return (multi_part->fd != -1) ?
		http_receive_mpart_file_write (http_receive, multi_part, at, length) :
		http_receive_mpart_value_append (multi_part, at, length);

}

static int http_receive_handle_mpart_data_end (multipart_parser *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	MultiPart *multi_part = ((HttpReceive *) parser->data)->request->current_part;

	if (multi_part->fd != -1) {
		(void) close (multi_part->fd);
		multi_part->fd = -1;

		// the file is complete, the rest of the body follows
		http_receive->stage = HTTP_STAGE_BODY;
	}

	if (multi_part->total_wrote < http_receive->file_stats->min_file_size)
		http_receive->file_stats->min_file_size = multi_part->total_wrote;

	if (multi_part->total_wrote > http_receive->file_stats->max_file_size)
		http_receive->file_stats->max_file_size = multi_part->total_wrote;

	http_receive->file_stats->sum_file_size += multi_part->total_wrote;

	return 0;

}

static int http_receive_handle_mpart_body (
	llhttp_t *parser, const char *at, size_t length
) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	if (http_receive_check_body_size (http_receive, length)) return HTTP_RECEIVE_PARSER_STOP;

	const size_t parsed = multipart_parser_execute (
		http_receive->mpart_parser, at, length
	);

	// a callback rejected the request, or the multi-part body is not valid
	return http_receive_parser_result (
		(parsed != length) ?
			http_receive_reject (http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR) : 0
	);

}

#pragma endregion

#pragma region handler

static int http_receive_handle_headers_completed (llhttp_t *parser);

static int http_receive_handle_message_completed (llhttp_t *parser);

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

static const char *http_parser_lenient_none = { "none" };

const char *http_parser_lenient_string (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #string;
		LLHTTP_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *http_parser_lenient_description (const HttpParserLenient lenient) {

	switch (lenient) {
		#define XX(num, name, string, description) case HTTP_PARSER_LENIENT_##name: return #description;
		LLHTTP_LENIENT_MAP(XX)
		#undef XX

		default: break;
	}

	return http_parser_lenient_none;

}

const char *http_stage_string (const HttpStage stage) {

	switch (stage) {
		#define XX(num, name, string, label) case HTTP_STAGE_##name: return #string;
		HTTP_STAGE_MAP(XX)
		#undef XX
	}

	return http_stage_string (HTTP_STAGE_QUEUED);

}

const char *http_stage_label (const HttpStage stage) {

	switch (stage) {
		#define XX(num, name, string, label) case HTTP_STAGE_##name: return #label;
		HTTP_STAGE_MAP(XX)
		#undef XX
	}

	return http_stage_label (HTTP_STAGE_QUEUED);

}

const char *http_abort_reason_string (const HttpAbortReason reason) {

	switch (reason) {
		#define XX(num, name, string, label) case HTTP_ABORT_REASON_##name: return #string;
		HTTP_ABORT_REASON_MAP(XX)
		#undef XX
	}

	return http_abort_reason_string (HTTP_ABORT_REASON_CLOSED);

}

const char *http_abort_reason_label (const HttpAbortReason reason) {

	switch (reason) {
		#define XX(num, name, string, label) case HTTP_ABORT_REASON_##name: return #label;
		HTTP_ABORT_REASON_MAP(XX)
		#undef XX
	}

	return http_abort_reason_label (HTTP_ABORT_REASON_CLOSED);

}

const char *http_client_state_string (const HttpClientState state) {

	switch (state) {
		#define XX(num, name, string) case HTTP_CLIENT_STATE_##name: return #string;
		HTTP_CLIENT_STATE_MAP(XX)
		#undef XX
	}

	return http_client_state_string (HTTP_CLIENT_STATE_CONNECTED);

}

static void http_receive_init (HttpReceive *http_receive);

HttpReceive *http_receive_new (void) {

	HttpReceive *http_receive = (HttpReceive *) malloc (sizeof (HttpReceive));
	if (http_receive) {
		http_receive_init (http_receive);
	}

	return http_receive;

}

// what belongs to a request: every request of a connection starts with these values
static void http_receive_request_init (HttpReceive *http_receive) {

	http_receive->receive_status = HTTP_RECEIVE_STATUS_NONE;

	http_receive->keep_alive_requested = false;
	http_receive->unparsed = 0;

	http_receive->mpart_parser = NULL;
	(void) memset (&http_receive->mpart_settings, 0, sizeof (multipart_parser_settings));

	http_receive->request = NULL;

	http_receive->type = HTTP_RECEIVE_TYPE_NONE;
	http_receive->dispatch = HTTP_RECEIVE_DISPATCH_NONE;
	http_receive->node = NULL;
	http_receive->endpoint = NULL;
	http_receive->route = NULL;
	http_receive->served_file = NULL;

	http_receive->head = false;
	http_receive->max_body_size = 0;

	http_receive->status = HTTP_STATUS_NONE;
	http_receive->sent = 0;

	http_receive->is_multi_part = false;
	http_receive->file_stats = NULL;

	http_receive->upload_sequence = 0;
	http_receive->upload_directory_created = false;

	http_receive->header_size = 0;
	http_receive->n_headers = 0;
	http_receive->body_size = 0;
	http_receive->n_parts = 0;

	http_receive->responded = false;
	http_receive->framed = false;

	http_receive->stream.state = HTTP_STREAM_STATE_NONE;
	http_receive->stream.chunked = false;

	http_receive->start_time = 0;
	http_receive->received = 0;

	http_receive->diagnostics.last_rc = 0;
	http_receive->diagnostics.last_errno = 0;
	http_receive->diagnostics.n_recvs = 0;
	http_receive->diagnostics.timed_out = false;

	http_receive->headers_deadline = 0;
	http_receive->request_deadline = 0;

	http_receive->stage = HTTP_STAGE_QUEUED;
	http_receive->abort_reason = HTTP_ABORT_REASON_CLOSED;

	http_receive->response_failed = false;
	http_receive->response_error = 0;

	http_receive->send_deadline.tv_sec = 0;
	http_receive->send_deadline.tv_nsec = 0;
	http_receive->send_deadline_set = false;

	http_receive->web_socket = NULL;

}

// what belongs to the connection, and the values of its first request
static void http_receive_init (HttpReceive *http_receive) {

	if (http_receive) {
		http_receive->cr = NULL;

		http_receive->keep_alive = false;

		http_receive->handler = NULL;

		http_receive->http_cerver = NULL;

		(void) memset (&http_receive->parser, 0, sizeof (llhttp_t));
		llhttp_settings_init (&http_receive->settings);

		http_receive->url_buffer = NULL;
		http_receive->field_buffer = NULL;
		http_receive->value_buffer = NULL;

		http_receive->arena = NULL;
		http_receive->borrowed_buffers = false;

		http_receive->sock_fd = -1;

		http_receive->upgraded = false;

		http_receive->client_state = HTTP_CLIENT_STATE_CONNECTED;

		http_receive_request_init (http_receive);
	}

}

static void http_receive_parser_init (HttpReceive *http_receive);

static const double http_receive_milliseconds_per_second = 1000.0;
static const double http_receive_nanoseconds_per_second = 1000000000.0;

static double http_receive_monotonic_time (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / http_receive_nanoseconds_per_second);

}

// a deadline in monotonic secs after the milliseconds of a timeout, 0 when it is disabled
static double http_receive_deadline (const double now, const unsigned int timeout) {

	return timeout ? now + ((double) timeout / http_receive_milliseconds_per_second) : 0;

}

// the clock and the deadlines of a request start when its receive is ready for it
static void http_receive_request_begin (HttpReceive *http_receive) {

	const HttpCerver *http_cerver = http_receive->http_cerver;

	http_receive->start_time = timer_get_current_time ();

	if (http_cerver) {
		const double now = http_receive_monotonic_time ();

		http_receive->headers_deadline = http_receive_deadline (now, http_cerver->headers_timeout);
		http_receive->request_deadline = http_receive_deadline (now, http_cerver->request_timeout);
	}

}

// creates the request and the stats of its uploads,
// in the arena that was lent to the receive or allocated
// returns 0 on success, 1 on error
static u8 http_receive_request_create (HttpReceive *http_receive) {

	u8 retval = 1;

	Arena *arena = http_receive->arena;

	HttpRequest *request = arena ? http_request_new_arena (arena) : http_request_new ();
	HttpRouteFileStats *file_stats = arena ?
		(HttpRouteFileStats *) arena_alloc (arena, sizeof (HttpRouteFileStats)) :
		http_route_file_stats_new ();

	if (request && file_stats) {
		request->keep_files = !http_receive->http_cerver->uploads_delete_when_done;

		// the stats in an arena are released with it
		if (arena) http_route_file_stats_init (file_stats);

		http_receive->request = request;
		http_receive->file_stats = file_stats;

		retval = 0;
	}

	else if (!arena) {
		if (request) http_request_delete (request);
		if (file_stats) http_route_file_stats_delete (file_stats);
	}

	return retval;

}

// what a receive takes from its connection: its cerver receive, its cerver,
// its fd and the parser of its requests
static void http_receive_connection_set (
	HttpReceive *http_receive, CerverReceive *cerver_receive
) {

	http_receive->cr = cerver_receive;

	http_receive->handler = http_receive_handle;

	http_receive->http_cerver = (HttpCerver *) cerver_receive->cerver->cerver_data;

	http_receive->sock_fd = cerver_receive->socket ? cerver_receive->socket->sock_fd : -1;

	http_receive_parser_init (http_receive);

}

HttpReceive *http_receive_create (CerverReceive *cerver_receive) {

	HttpReceive *http_receive = http_receive_new ();
	if (http_receive) {
		http_receive_connection_set (http_receive, cerver_receive);

		if (!http_receive_request_create (http_receive)) {
			http_receive_request_begin (http_receive);
		}

		else {
			free (http_receive);
			http_receive = NULL;
		}
	}

	return http_receive;

}

static void http_receive_borrow_buffer (String **destination, String *buffer) {

	http_receive_buffer_reset (buffer);
	*destination = buffer;

}

static bool http_receive_resources_are_complete (const HttpReceiveResources *resources) {

	return resources && resources->arena
		&& resources->url_buffer && resources->field_buffer && resources->value_buffer;

}

// the receive is not in the arena of its request, so the arena can be released
// after each one of them while the receive stays with its connection
u8 http_receive_prepare (
	HttpReceive *http_receive,
	CerverReceive *cerver_receive, const HttpReceiveResources *resources
) {

	u8 retval = 1;

	if (http_receive && cerver_receive && http_receive_resources_are_complete (resources)) {
		http_receive_init (http_receive);

		http_receive->arena = resources->arena;

		http_receive->borrowed_buffers = true;
		http_receive_borrow_buffer (&http_receive->url_buffer, resources->url_buffer);
		http_receive_borrow_buffer (&http_receive->field_buffer, resources->field_buffer);
		http_receive_borrow_buffer (&http_receive->value_buffer, resources->value_buffer);

		http_receive_connection_set (http_receive, cerver_receive);

		retval = http_receive_request_create (http_receive);

		if (!retval) http_receive_request_begin (http_receive);

		// what a request that could not be created left in the arena
		else arena_reset (resources->arena);
	}

	return retval;

}

// only the callbacks that cerver uses, the rest stay NULL
static void http_receive_settings_init (llhttp_settings_t *settings) {

	llhttp_settings_init (settings);

	settings->on_url = http_receive_handle_url;
	settings->on_url_complete = http_receive_handle_url_complete;
	settings->on_header_field = http_receive_handle_header_field;
	settings->on_header_value = http_receive_handle_header_value;
	settings->on_header_value_complete = http_receive_handle_header_value_complete;
	settings->on_headers_complete = http_receive_handle_headers_completed;
	settings->on_body = http_receive_handle_body;
	settings->on_message_complete = http_receive_handle_message_completed;

}

// the parser is embedded in the receive and it uses the receive's settings,
// so its on_body can change for each request
static void http_receive_parser_init (HttpReceive *http_receive) {

	http_receive_settings_init (&http_receive->settings);

	llhttp_init (&http_receive->parser, HTTP_REQUEST, &http_receive->settings);
	http_receive->parser.data = http_receive;

	if (http_receive->http_cerver) {
		llhttp_set_lenient_flags (&http_receive->parser, (uint16_t) http_receive->http_cerver->parser_lenient);
	}

}

static void http_receive_delete_buffers (HttpReceive *http_receive) {

	if (!http_receive->borrowed_buffers) {
		str_delete (http_receive->url_buffer);
		str_delete (http_receive->field_buffer);
		str_delete (http_receive->value_buffer);
	}

	http_receive->url_buffer = NULL;
	http_receive->field_buffer = NULL;
	http_receive->value_buffer = NULL;

}

// a request that was completed keeps its files unless the cerver deletes them when it is done,
// the files of any other request are always discarded, its handler never got them
static bool http_receive_keeps_uploads (const HttpReceive *http_receive) {

	return (http_receive->receive_status == HTTP_RECEIVE_STATUS_COMPLETED)
		|| (http_receive->receive_status == HTTP_RECEIVE_STATUS_RESPONSE_FAILED);

}

// removes the directory that the request created for its uploads,
// rmdir () only removes it if it is empty (a kept file stays with its directory)
static void http_receive_uploads_directory_remove (const HttpReceive *http_receive) {

	const HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpRequest *request = http_receive->request;

	if (http_receive->upload_directory_created && http_cerver && request && (request->dirname_len > 0)) {
		char dirname[HTTP_MULTI_PART_DIRNAME_SIZE] = { 0 };
		const int written = snprintf (
			dirname, HTTP_MULTI_PART_DIRNAME_SIZE,
			"%s/%s", http_cerver->uploads_path, request->dirname
		);

		if ((written > 0) && (written < HTTP_MULTI_PART_DIRNAME_SIZE)) (void) rmdir (dirname);
	}

}

static void http_receive_uploads_release (HttpReceive *http_receive) {

	HttpRequest *request = http_receive->request;

	if (request) {
		if (!http_receive_keeps_uploads (http_receive)) request->keep_files = false;

		http_request_destroy (request);

		http_receive_uploads_directory_remove (http_receive);
	}

}

// what a request leaves when it ends: its uploads, the place of its upgrade,
// the request with its data, the stats of its uploads and the parser of its parts
static void http_receive_request_end (HttpReceive *http_receive) {

	http_receive_uploads_release (http_receive);

	http_receive_web_socket_release (http_receive);

	http_request_delete (http_receive->request);
	http_receive->request = NULL;

	http_receive->node = NULL;
	http_receive->endpoint = NULL;
	http_receive->route = NULL;

	// the stats in an arena are released with it
	if (!http_receive->arena) http_route_file_stats_delete (http_receive->file_stats);
	http_receive->file_stats = NULL;

	if (http_receive->mpart_parser) {
		multipart_parser_free (http_receive->mpart_parser);
		http_receive->mpart_parser = NULL;
	}

}

u8 http_receive_reset (HttpReceive *http_receive) {

	u8 retval = 1;

	if (http_receive) {
		http_receive_request_end (http_receive);

		// the data of the request that ended is released all at once
		if (http_receive->arena) arena_reset (http_receive->arena);

		http_receive_request_init (http_receive);

		// the parser keeps its type and its lenient flags,
		// and the callbacks that a request changed are the ones of a new request
		http_receive_settings_init (&http_receive->settings);
		llhttp_reset (&http_receive->parser);

		http_receive_buffer_reset (http_receive->url_buffer);
		http_receive_buffer_reset (http_receive->field_buffer);
		http_receive_buffer_reset (http_receive->value_buffer);

		retval = http_receive_request_create (http_receive);

		if (!retval) http_receive_request_begin (http_receive);
	}

	return retval;

}

void http_receive_delete (HttpReceive *http_receive) {

	if (http_receive) {
		http_receive_request_end (http_receive);

		http_receive->cr = NULL;

		http_receive->http_cerver = NULL;

		http_receive_delete_buffers (http_receive);

		// a receive that was prepared lives in the memory of its owner
		if (!http_receive->arena) free (http_receive);
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

const HttpEndpoint *http_receive_get_endpoint (
	const HttpReceive *http_receive
) {

	return http_receive ? http_receive->endpoint : NULL;

}

const void *http_receive_get_route_custom_data (
	const HttpReceive *http_receive
) {

	const void *custom_data = NULL;

	if (http_receive) {
		if (http_receive->endpoint) {
			custom_data = http_endpoint_get_custom_data (http_receive->endpoint);
		}

		else if (http_receive->route) {
			custom_data = http_receive->route->custom_data;
		}
	}

	return custom_data;

}

const void *http_receive_get_cerver_custom_data (
	const HttpReceive *http_receive
) {

	return (http_receive && http_receive->http_cerver) ?
		http_receive->http_cerver->custom_data : NULL;

}

bool http_receive_is_head (
	const HttpReceive *http_receive
) {

	return http_receive ? http_receive->head : false;

}

const char *http_receive_get_allow (
	const HttpReceive *http_receive
) {

	return (http_receive && http_receive->node) ? http_receive->node->allow : NULL;

}

HttpStage http_receive_get_stage (const HttpReceive *http_receive) {

	return http_receive ? http_receive->stage : HTTP_STAGE_QUEUED;

}

// an EOF (error 0) is a close, any error of a recv () or a send () is a failed connection
static inline HttpAbortReason http_receive_abort_reason (const int error) {

	return error ? HTTP_ABORT_REASON_RESET : HTTP_ABORT_REASON_CLOSED;

}

// a send () that waited the whole send timeout, the client stopped reading
static inline bool http_receive_error_is_timeout (const int error) {

	return (error == EAGAIN) || (error == EWOULDBLOCK);

}

void http_receive_client_left (HttpReceive *http_receive, const int error) {

	if (http_receive) {
		switch (http_receive->receive_status) {
			case HTTP_RECEIVE_STATUS_NONE:
			case HTTP_RECEIVE_STATUS_HEADERS:
			case HTTP_RECEIVE_STATUS_BODY:
				http_receive->receive_status = HTTP_RECEIVE_STATUS_CLOSED;
				http_receive->abort_reason = http_receive_abort_reason (error);
				break;

			default: break;
		}

		http_receive->client_state = error ? HTTP_CLIENT_STATE_RESET : HTTP_CLIENT_STATE_CLOSED;
	}

}

// counts the request where its client left, once
static void http_receive_count_gone (
	const HttpReceive *http_receive, const int error
) {

	HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpStage stage = http_receive->stage;

	if (http_receive_error_is_timeout (error)) {
		CERVER_STATS_ADD (http_cerver->n_vanished_requests[stage], 1);
	}

	else {
		CERVER_STATS_ADD (http_cerver->n_aborted_requests[stage][http_receive_abort_reason (error)], 1);
	}

}

void http_receive_response_failed (
	const HttpReceive *http_receive, const int error
) {

	HttpReceive *receive = (HttpReceive *) http_receive;

	if (receive && !receive->response_failed) {
		receive->response_failed = true;
		receive->response_error = error;

		if (!http_receive_error_is_timeout (error)) {
			receive->client_state = HTTP_CLIENT_STATE_RESET;
		}

		if (receive->http_cerver) {
			CERVER_STATS_ADD (receive->http_cerver->n_failed_responses, 1);

			http_receive_count_gone (receive, error);
		}
	}

}

// the state that the events of a poll () on the client's socket tell,
// a peek at its data tells an EOF from a failed connection
static HttpClientState http_receive_client_state_from_events (
	const int sock_fd, const short events
) {

	HttpClientState state = HTTP_CLIENT_STATE_CONNECTED;

	char byte = 0;
	const ssize_t peeked = recv (sock_fd, &byte, sizeof (byte), MSG_PEEK | MSG_DONTWAIT);

	if (peeked < 0) {
		if (!http_receive_error_is_timeout (errno)) state = HTTP_CLIENT_STATE_RESET;
		else if (events & (POLLRDHUP | POLLHUP)) state = HTTP_CLIENT_STATE_CLOSED;
	}

	else if (!peeked || (events & (POLLRDHUP | POLLHUP))) {
		state = HTTP_CLIENT_STATE_CLOSED;
	}

	return state;

}

// a gone client never comes back, a closed one can still reset its connection
static HttpClientState http_receive_client_state_check (const HttpReceive *http_receive) {

	HttpClientState state = HTTP_CLIENT_STATE_RESET;

	const Connection *connection = http_receive->cr ? http_receive->cr->connection : NULL;
	const int sock_fd = (connection && connection->socket) ? connection->socket->sock_fd : -1;

	if (sock_fd > -1) {
		struct pollfd descriptor = { sock_fd, POLLIN | POLLRDHUP, 0 };

		const int ready = poll (&descriptor, 1, 0);
		if (!ready) state = HTTP_CLIENT_STATE_CONNECTED;
		else if (ready > 0) state = http_receive_client_state_from_events (sock_fd, descriptor.revents);
	}

	return state;

}

HttpClientState http_receive_get_client_state (const HttpReceive *http_receive) {

	HttpClientState state = HTTP_CLIENT_STATE_RESET;

	HttpReceive *receive = (HttpReceive *) http_receive;
	if (receive) {
		if (receive->client_state != HTTP_CLIENT_STATE_RESET) {
			const HttpClientState found = http_receive_client_state_check (receive);
			if (found > receive->client_state) receive->client_state = found;
		}

		state = receive->client_state;
	}

	return state;

}

bool http_receive_is_client_gone (const HttpReceive *http_receive) {

	bool gone = true;

	if (http_receive) {
		const HttpClientState state = http_receive_get_client_state (http_receive);

		gone = (state == HTTP_CLIENT_STATE_RESET)
			|| ((state == HTTP_CLIENT_STATE_CLOSED) && http_receive->http_cerver->half_close_is_gone)
			|| http_receive->response_failed;
	}

	return gone;

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

	#ifdef HTTP_RECEIVE_DEBUG
	cerver_log_warning (
		"No matching route for %s %s",
		http_request_method_str (request->method),
		request->url->str
	);
	#endif

	if (http_cerver->not_found_handler) {
		// handle with not found route
		http_cerver->not_found (http_receive, request);
	}

	else {
		// handle with default route
		http_cerver->default_handler (http_receive, request);

		CERVER_STATS_ADD (http_cerver->n_catch_all_requests, 1);
	}

}

// a key or a value of a query is decoded in its own memory, the decoded one is never longer:
// a '+' is a space and then the escapes are decoded, so an escaped '+' is kept
static void http_query_piece_decode (String *piece) {

	for (size_t idx = 0; idx < piece->len; idx++) {
		if (piece->str[idx] == query_encoded_space) piece->str[idx] = query_space;
	}

	piece->len = http_url_decode_path (piece->str, piece->str, piece->len);
	piece->str[piece->len] = '\0';

}

// the pairs of a query are split by the separators of the original text
// and then each key and each value is decoded, so an escaped separator is part of its value:
// decoding the whole text first made a new pair out of "%26" and "%3D"
static DoubleList *http_query_pairs_decoded (const char *query) {

	DoubleList *pairs = query ? http_parse_query_into_pairs (query, query + strlen (query)) : NULL;
	if (pairs) {
		KeyValuePair *pair = NULL;
		for (ListElement *le = dlist_start (pairs); le; le = le->next) {
			pair = (KeyValuePair *) le->data;

			http_query_piece_decode (pair->key);
			http_query_piece_decode (pair->value);
		}
	}

	return pairs;

}

// parses the query of the request into its query params
static void http_receive_parse_query (HttpRequest *request) {

	request->query_params = http_query_pairs_decoded (request->query->str);

}

// a x-www-form-urlencoded body is parsed into the request's body values
static void http_receive_parse_body_values (HttpRequest *request) {

	const String *content_type = request->headers[HTTP_HEADER_CONTENT_TYPE];
	if (
		content_type
		&& !strncmp (form_urlencoded_value, content_type->str, form_urlencoded_value_len)
	) {
		request->body_values = http_query_pairs_decoded (request->body->str);
	}

}

// runs the before middlewares, the handler and the after middlewares
#pragma region websocket

// the bytes of a 101 with its accept, its protocol and its extensions
#define HTTP_WEB_SOCKET_RESPONSE_SIZE			(1 << 10)

static const char *http_web_socket_response_start = {
	"HTTP/1.1 101 Switching Protocols\r\n"
	"Server: Cerver/" CERVER_VERSION "\r\n"
	"Upgrade: websocket\r\n"
	"Connection: Upgrade\r\n"
	"Sec-WebSocket-Accept: "
};

static const char *http_web_socket_response_protocol = { "\r\nSec-WebSocket-Protocol: " };
static const char *http_web_socket_response_extensions = { "\r\nSec-WebSocket-Extensions: " };
static const char *http_web_socket_response_end = { "\r\n\r\n" };

static const char *http_web_socket_rejected_key = { "error" };
static const char *http_web_socket_rejected_value = { "WebSocket rejected!" };

// the endpoint takes WebSocket connections, a route of the first API with the modifier does not
static bool http_endpoint_is_web_socket (const HttpEndpoint *endpoint) {

	return (endpoint->resolved.modifier == HTTP_ROUTE_MODIFIER_WEB_SOCKET)
		&& (endpoint->resolved.web_socket.set & HTTP_WEB_SOCKET_SETTING_HANDLERS);

}

// gives back the place of an upgrade that never started its connection
static void http_receive_web_socket_release (HttpReceive *http_receive) {

	HttpWebSocketHandshake *handshake = http_receive->web_socket;
	if (handshake) {
		if (handshake->reserved && http_receive->http_cerver) {
			http_web_socket_registry_unreserve (http_receive->http_cerver->web_sockets);
		}

		http_web_socket_handshake_delete (handshake);
		http_receive->web_socket = NULL;
	}

}

// the request asks for WebSocket: its Upgrade has websocket and its Connection has upgrade
static bool http_request_web_socket_requested (const HttpRequest *request) {

	const String *upgrade = request->headers[HTTP_HEADER_UPGRADE];
	const String *connection = request->headers[HTTP_HEADER_CONNECTION];

	return upgrade && connection
		&& http_web_socket_header_has_token (upgrade->str, upgrade->len, HTTP_WEB_SOCKET_UPGRADE_TOKEN)
		&& http_web_socket_header_has_token (connection->str, connection->len, HTTP_WEB_SOCKET_CONNECTION_TOKEN);

}

// the request of an upgrade (RFC 6455 section 4.2.1): HTTP/1.1 or newer,
// a key of 16 bytes, the version 13 and no body
// returns true when it is valid, and sets the rejection when it is not
static bool http_receive_web_socket_validate (
	const llhttp_t *parser, const HttpRequest *request,
	HttpWebSocketRejection *rejection
) {

	const String *key = request->headers[HTTP_HEADER_SEC_WEBSOCKET_KEY];
	const String *version = request->headers[HTTP_HEADER_SEC_WEBSOCKET_VERSION];

	const u8 major = llhttp_get_http_major (parser);
	const u8 minor = llhttp_get_http_minor (parser);

	bool valid = false;

	if (!major || ((major == 1) && !minor)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;
	}

	else if (!key || !http_web_socket_key_is_valid (key->str, key->len)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;
	}

	else if (!version || strcmp (version->str, HTTP_WEB_SOCKET_VERSION)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_VERSION;
	}

	else if (((parser->flags & F_CONTENT_LENGTH) && parser->content_length) || (parser->flags & F_CHUNKED)) {
		*rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;
	}

	else {
		valid = true;
	}

	return valid;

}

// the Origin of the request is in the origins whitelist of the cerver
static bool http_receive_web_socket_origin_allowed (
	const HttpCerver *http_cerver, const HttpRequest *request
) {

	const String *origin = request->headers[HTTP_HEADER_ORIGIN];

	bool allowed = false;
	for (u8 idx = 0; origin && !allowed && (idx < http_cerver->n_origins); idx++) {
		allowed = !strcmp (http_cerver->origins_whitelist[idx].value, origin->str);
	}

	return allowed;

}

static int http_receive_web_socket_reject (
	HttpReceive *http_receive, HttpResponse *response,
	const HttpWebSocketRejection rejection
) {

	http_web_socket_registry_count_rejection (http_receive->http_cerver->web_sockets, rejection);

	return http_receive_reject (http_receive, response, HTTP_RECEIVE_STATUS_REJECTED);

}

// what the 101 says: its accept, the protocol of the server that the client offered and the compression
static HttpWebSocketHandshake *http_receive_web_socket_handshake (
	const HttpRequest *request, const HttpWebSocketSettings *settings
) {

	HttpWebSocketHandshake *handshake = (HttpWebSocketHandshake *) calloc (1, sizeof (HttpWebSocketHandshake));
	if (handshake) {
		handshake->reserved = true;

		const String *key = request->headers[HTTP_HEADER_SEC_WEBSOCKET_KEY];
		(void) http_web_socket_accept_compute (key->str, key->len, handshake->accept);

		const String *offered = request->headers[HTTP_HEADER_SEC_WEBSOCKET_PROTOCOL];
		const char *protocol = offered ? http_web_socket_protocol_select (
			offered->str, offered->len,
			(const char * const *) settings->protocols, settings->n_protocols
		) : NULL;

		if (protocol) handshake->protocol = strdup (protocol);

		const String *extensions = request->headers[HTTP_HEADER_SEC_WEBSOCKET_EXTENSIONS];
		handshake->compression = extensions && http_web_socket_deflate_negotiate (
			extensions->str, extensions->len, &settings->compression, &handshake->deflate
		);
	}

	return handshake;

}

// the checks of an upgrade before its body, a place for its connection is taken
static int http_receive_web_socket_prepare (
	HttpReceive *http_receive, const llhttp_t *parser,
	const HttpEndpoint *endpoint
) {

	int retval = 0;

	HttpCerver *http_cerver = http_receive->http_cerver;
	const HttpRequest *request = http_receive->request;
	HttpWebSocketRejection rejection = HTTP_WEB_SOCKET_REJECTION_BAD_REQUEST;

	if (!http_receive_web_socket_validate (parser, request, &rejection)) {
		retval = http_receive_web_socket_reject (
			http_receive,
			(rejection == HTTP_WEB_SOCKET_REJECTION_VERSION) ? upgrade_required_error : bad_request_error,
			rejection
		);
	}

	else if (endpoint->resolved.web_socket.origin_check && !http_receive_web_socket_origin_allowed (http_cerver, request)) {
		retval = http_receive_web_socket_reject (http_receive, forbidden_error, HTTP_WEB_SOCKET_REJECTION_ORIGIN);
	}

	else if (!http_web_socket_registry_reserve (http_cerver->web_sockets)) {
		retval = http_receive_web_socket_reject (http_receive, web_socket_unavailable_error, HTTP_WEB_SOCKET_REJECTION_LIMIT);
	}

	else {
		http_receive->web_socket = http_receive_web_socket_handshake (request, &endpoint->resolved.web_socket);
		if (!http_receive->web_socket) {
			http_web_socket_registry_unreserve (http_cerver->web_sockets);
			retval = http_receive_reject (http_receive, server_error, HTTP_RECEIVE_STATUS_REJECTED);
		}
	}

	return retval;

}

// a request to a WebSocket endpoint, after its authentication and before its body:
// an upgrade is checked, a request without one goes to the handler of the endpoint (426 without it)
// returns 0 to continue, 1 when the request was rejected
static int http_receive_handle_headers_web_socket (
	HttpReceive *http_receive, const llhttp_t *parser
) {

	int retval = 0;

	const HttpEndpoint *endpoint = http_receive->endpoint;
	if (endpoint && http_endpoint_is_web_socket (endpoint)) {
		if (llhttp_get_upgrade (parser) && http_request_web_socket_requested (http_receive->request)) {
			retval = http_receive_web_socket_prepare (http_receive, parser, endpoint);
		}

		else if (!endpoint->handler) {
			retval = http_receive_web_socket_reject (
				http_receive, upgrade_required_error, HTTP_WEB_SOCKET_REJECTION_UPGRADE
			);
		}
	}

	return retval;

}

// appends text to the response, returns false when it does not fit
static bool http_web_socket_response_append (
	char *response, size_t *length, const char *text, const size_t text_length
) {

	bool appended = false;

	if ((*length + text_length) < HTTP_WEB_SOCKET_RESPONSE_SIZE) {
		(void) memcpy (response + *length, text, text_length);
		*length += text_length;
		appended = true;
	}

	return appended;

}

// sends the 101, it never has a Content-Length nor a Connection: close
// returns 0 on success, 1 on error
static u8 http_receive_web_socket_switch (
	HttpReceive *http_receive, const HttpWebSocketHandshake *handshake
) {

	char response[HTTP_WEB_SOCKET_RESPONSE_SIZE];
	size_t length = 0;

	bool fits = http_web_socket_response_append (
		response, &length, http_web_socket_response_start, strlen (http_web_socket_response_start)
	) && http_web_socket_response_append (response, &length, handshake->accept, HTTP_WEB_SOCKET_ACCEPT_LENGTH);

	if (fits && handshake->protocol) {
		fits = http_web_socket_response_append (
			response, &length, http_web_socket_response_protocol, strlen (http_web_socket_response_protocol)
		) && http_web_socket_response_append (response, &length, handshake->protocol, strlen (handshake->protocol));
	}

	if (fits && handshake->compression) {
		char extensions[HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE];
		const size_t extensions_length = http_web_socket_deflate_response (
			&handshake->deflate, extensions, HTTP_WEB_SOCKET_DEFLATE_HEADER_SIZE
		);

		fits = extensions_length && http_web_socket_response_append (
			response, &length, http_web_socket_response_extensions, strlen (http_web_socket_response_extensions)
		) && http_web_socket_response_append (response, &length, extensions, extensions_length);
	}

	fits = fits && http_web_socket_response_append (
		response, &length, http_web_socket_response_end, strlen (http_web_socket_response_end)
	);

	struct iovec part = { response, length };

	return fits ?
		http_response_send_pieces_status (http_receive, HTTP_STATUS_SWITCHING_PROTOCOLS, &part, 1) : 1;

}

// the handshake takes what on_upgrade () decided:
// a protocol that the client offered, the data of the connection and the decoded data of the request
static void http_receive_web_socket_decided (
	HttpWebSocketHandshake *handshake, HttpRequest *request,
	const HttpWebSocketUpgrade *upgrade
) {

	const String *offered = request->headers[HTTP_HEADER_SEC_WEBSOCKET_PROTOCOL];

	if (upgrade->protocol != handshake->protocol) {
		const char *protocol = (upgrade->protocol && offered) ? http_web_socket_protocol_select (
			offered->str, offered->len, &upgrade->protocol, 1
		) : NULL;

		free (handshake->protocol);
		handshake->protocol = protocol ? strdup (protocol) : NULL;
	}

	handshake->data = upgrade->data;
	handshake->delete_data = upgrade->delete_data;

	if (upgrade->adopt_decoded_data) {
		handshake->decoded_data = request->decoded_data;
		handshake->delete_decoded_data = request->delete_decoded_data;
		request->decoded_data = NULL;
		request->delete_decoded_data = NULL;
	}

}

// runs on_upgrade (), that accepts the connection or rejects it with its status
static bool http_receive_web_socket_accept (
	HttpReceive *http_receive, HttpRequest *request,
	const HttpEndpoint *endpoint, HttpWebSocketHandshake *handshake
) {

	bool accepted = true;

	const HttpWebSocketHandlers *handlers = &endpoint->resolved.web_socket.handlers;
	if (handlers->on_upgrade) {
		HttpWebSocketUpgrade upgrade = { handshake->protocol, NULL, NULL, false, HTTP_STATUS_FORBIDDEN };

		accepted = handlers->on_upgrade (http_receive, request, &upgrade);
		if (accepted) {
			http_receive_web_socket_decided (handshake, request, &upgrade);
		}

		else {
			if (upgrade.data && upgrade.delete_data) upgrade.delete_data (upgrade.data);

			http_web_socket_registry_count_rejection (
				http_receive->http_cerver->web_sockets, HTTP_WEB_SOCKET_REJECTION_APPLICATION
			);

			// on_upgrade () can answer the request itself
			if (!http_receive->responded) {
				(void) http_response_json_key_value_send (
					http_receive, upgrade.status, http_web_socket_rejected_key, http_web_socket_rejected_value
				);
			}
		}
	}

	return accepted;

}

// the frames that the client sent with its request, before the 101,
// are copied out of the buffer of the thread for its connection
static void http_receive_web_socket_pending (
	HttpReceive *http_receive, const char *unparsed
) {

	HttpWebSocketHandshake *handshake = http_receive->web_socket;
	if (handshake && http_receive->unparsed && !handshake->pending) {
		handshake->pending = (u8 *) malloc (http_receive->unparsed);
		if (handshake->pending) {
			(void) memcpy (handshake->pending, unparsed, http_receive->unparsed);
			handshake->pending_size = http_receive->unparsed;
		}

		// the connection can not start without its first frames
		else {
			http_receive->upgraded = false;
			http_receive_close (http_receive, false);
		}
	}

}

// the upgrade of a request that passed its checks: the before middlewares, on_upgrade () and the 101
static void http_receive_web_socket_upgrade (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
) {

	HttpWebSocketHandshake *handshake = http_receive->web_socket;

	// a before middleware that stops answers the request itself
	if (
		http_middlewares_run_before (&endpoint->resolved.middlewares, http_receive, request)
		&& http_receive_web_socket_accept (http_receive, request, endpoint, handshake)
		&& !http_receive_web_socket_switch (http_receive, handshake)
	) {
		http_receive->upgraded = true;
	}

}

#pragma endregion

static void http_receive_handle_endpoint (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
) {

	const HttpMiddlewares *middlewares = &endpoint->resolved.middlewares;

	if (http_middlewares_run_before (middlewares, http_receive, request)) {
		endpoint->handler (http_receive, request);
	}

	http_middlewares_run_after (middlewares, http_receive, request);

	// a stream that the handler did not end is ended by cerver
	if (http_response_stream_is_active (http_receive)) {
		(void) http_response_stream_end (http_receive);
	}

}

// an upgrade that passed its checks, a request without one goes to the handler of the endpoint,
// a route of the first API with the modifier is answered with 501
static void http_receive_handle_match_web_socket (
	HttpReceive *http_receive, HttpRequest *request, const HttpEndpoint *endpoint
) {

	if (http_receive->web_socket) {
		http_receive_web_socket_upgrade (http_receive, request, endpoint);
	}

	else if (endpoint->handler && http_endpoint_is_web_socket (endpoint)) {
		http_receive_handle_endpoint (http_receive, request, endpoint);
	}

	else {
		(void) http_response_send (not_implemented_error, http_receive);
	}

}

// handles the request with its endpoint
static void http_receive_handle_match (
	HttpReceive *http_receive,
	HttpRequest *request,
	const HttpEndpoint *endpoint
) {

	if (request->query) http_receive_parse_query (request);

	if (request->body) http_receive_parse_body_values (request);

	switch (endpoint->resolved.modifier) {
		case HTTP_ROUTE_MODIFIER_WEB_SOCKET:
			http_receive_handle_match_web_socket (http_receive, request, endpoint);
			break;

		default:
			http_receive_handle_endpoint (http_receive, request, endpoint);
			break;
	}

}

static void http_receive_handle_select_failed_auth (
	HttpReceive *http_receive
) {

	(void) http_receive_reject (
		http_receive, bad_auth_error, HTTP_RECEIVE_STATUS_REJECTED
	);

	CERVER_STATS_ADD (http_receive->http_cerver->n_failed_auth_requests, 1);

}

// returns true if the request has a valid bearer JWT
// the route's decode_data method sets the request's decoded data
static bool http_receive_handle_select_auth_bearer (
	HttpCerver *http_cerver,
	HttpReceive *http_receive,
	const HttpRouteSettings *found, HttpRequest *request
) {

	bool authenticated = false;

	// the header must be "Bearer <token>"
	// and a token can only be validated with the cerver's public key
	const String *authorization = request->headers[HTTP_HEADER_AUTHORIZATION];
	const char *token = authorization ?
		http_cerver_auth_bearer_token (authorization->str, authorization->len) : NULL;

	jwt_t *jwt = token ? http_cerver_auth_jwt_verify (
		http_cerver, token, authorization->len - HTTP_JWT_BEARER_PREFIX_LENGTH
	) : NULL;

	if (jwt) {
		if (found->decode_data) {
			request->decoded_data = found->decode_data (jwt->grants);
			request->delete_decoded_data = found->delete_decoded_data;
		}

		jwt_free (jwt);

		authenticated = true;
	}

	else {
		http_receive_handle_select_failed_auth (http_receive);
	}

	return authenticated;

}

// returns true if the route's custom authentication handler accepts the request
static bool http_receive_handle_select_auth_custom (
	HttpReceive *http_receive,
	const HttpRouteSettings *found, HttpRequest *request
) {

	bool authenticated = false;

	if (found->authentication_handler) {
		if (!found->authentication_handler (
			http_receive, request
		)) {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_success ("Success authentication with custom method!");
			#endif

			authenticated = true;
		}

		else {
			#ifdef HTTP_AUTH_DEBUG
			cerver_log_error ("Failed authentication with custom method!");
			#endif

			http_receive_handle_select_failed_auth (http_receive);
		}
	}

	// no authentication method was provided
	else {
		#ifdef HTTP_AUTH_DEBUG
		cerver_log_error ("No authentication method was provided!");
		#endif

		http_receive_handle_select_failed_auth (http_receive);
	}

	return authenticated;

}

// authenticates the request with the found route's method
// a failed authentication is answered here
// returns true if the request can be handled by the route
static bool http_receive_handle_authenticate (
	HttpCerver *http_cerver,
	HttpReceive *http_receive,
	const HttpRouteSettings *found, HttpRequest *request
) {

	bool authenticated = false;

	switch (found->auth_type) {
		// no authentication, handle the request directly
		case HTTP_ROUTE_AUTH_TYPE_NONE: {
			authenticated = true;
		} break;

		// a bearer JWT is expected in the authorization header
		case HTTP_ROUTE_AUTH_TYPE_BEARER: {
			authenticated = http_receive_handle_select_auth_bearer (
				http_cerver, http_receive, found, request
			);
		} break;

		// a custom method is used to handle authentication
		case HTTP_ROUTE_AUTH_TYPE_CUSTOM: {
			authenticated = http_receive_handle_select_auth_custom (
				http_receive, found, request
			);
		} break;

		default: {
			http_receive_handle_select_failed_auth (http_receive);
		} break;
	}

	return authenticated;

}

// matches the request's path with the routes tree, with the trailing slash policy
// redirect is set when the path only matches without its trailing slash
static bool http_receive_select_node (
	const HttpTree *tree, const HttpRequest *request,
	HttpRequestParam *params, HttpRouteMatch *match, bool *redirect
) {

	const char *path = request->url->str;
	const size_t path_len = request->url->len;
	const bool trailing = (path_len > 1) && (path[path_len - 1] == HTTP_TREE_PATH_SEPARATOR);

	bool found = false;

	switch (tree->trailing_slash) {
		case HTTP_TRAILING_SLASH_TOLERANT:
			found = http_tree_match (tree, path, trailing ? path_len - 1 : path_len, params, match);
			break;

		case HTTP_TRAILING_SLASH_REDIRECT:
			found = http_tree_match (tree, path, path_len, params, match);
			*redirect = !found && trailing && http_tree_match (tree, path, path_len - 1, params, match);
			found = found || *redirect;
			break;

		default:
			found = http_tree_match (tree, path, path_len, params, match);
			break;
	}

	return found;

}

// selects the endpoint of the request's method in the node that matched its path
// a HEAD request uses the GET endpoint, an OPTIONS request without endpoint is answered by the cerver
static void http_receive_select_method (
	HttpReceive *http_receive, const HttpRequest *request
) {

	const HttpRouteNode *node = http_receive->node;
	const RequestMethod method = request->method;

	HttpEndpoint *endpoint = http_request_method_is_valid (method) ? node->endpoints[method] : NULL;
	if (!endpoint && (method == REQUEST_METHOD_HEAD) && node->endpoints[REQUEST_METHOD_GET]) {
		endpoint = node->endpoints[REQUEST_METHOD_GET];
		http_receive->head = true;
	}

	http_receive->endpoint = endpoint;
	http_receive->route = endpoint ? endpoint->route : NULL;

	if (endpoint) http_receive->dispatch = HTTP_RECEIVE_DISPATCH_ENDPOINT;
	else if (method == REQUEST_METHOD_OPTIONS) http_receive->dispatch = HTTP_RECEIVE_DISPATCH_OPTIONS;
	else if (http_receive->http_cerver->tree->method_not_allowed) http_receive->dispatch = HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED;
	else http_receive->dispatch = HTTP_RECEIVE_DISPATCH_CATCH_ALL;

}

// selects how the request will be answered with the routes tree
static void http_receive_select (HttpReceive *http_receive) {

	HttpRequest *request = http_receive->request;
	const HttpTree *tree = http_receive->http_cerver->tree;

	http_receive->dispatch = HTTP_RECEIVE_DISPATCH_CATCH_ALL;

	Arena *arena = http_request_get_arena (request);
	HttpRequestParam *params = arena ? (HttpRequestParam *) arena_alloc (
		arena, tree->max_params * sizeof (HttpRequestParam)
	) : NULL;

	bool redirect = false;
	HttpRouteMatch match = { NULL, 0 };
	if (params && http_receive_select_node (tree, request, params, &match, &redirect)) {
		http_receive->node = match.node;

		if (redirect) {
			http_receive->dispatch = HTTP_RECEIVE_DISPATCH_REDIRECT;
		}

		else {
			http_receive_select_method (http_receive, request);

			request->params = params;
			request->n_params = match.n_params;
			request->params_names = http_receive->endpoint ? http_receive->endpoint->params_names : NULL;
		}
	}

}

// a GET (or HEAD) with a file extension that no endpoint handles is served from the static paths
static bool http_receive_is_static_file (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	return (http_receive->dispatch != HTTP_RECEIVE_DISPATCH_ENDPOINT)
		&& ((request->method == REQUEST_METHOD_GET) || (request->method == REQUEST_METHOD_HEAD))
		&& http_receive->http_cerver->static_paths->size
		&& strrchr (request->url->str, static_file_extension_separator);

}

// the directory of the request's uploads: it asks the dirname generator for its name
// returns true if the request has a directory, its path is written in dirname
static bool http_receive_uploads_directory_path (
	HttpReceive *http_receive, char *dirname
) {

	bool has_directory = false;

	HttpCerver *http_cerver = http_receive->http_cerver;
	HttpRequest *request = http_receive->request;

	if (http_cerver->uploads_dirname_generator && (http_cerver->uploads_path_len > 0)) {
		http_cerver->uploads_dirname_generator (http_receive, request);

		// a name that did not fit is not used
		if (request->dirname_len >= REQUEST_DIRNAME_SIZE) {
			request->dirname_len = 0;
			request->dirname[0] = '\0';
		}

		if (request->dirname_len > 0) {
			const int written = snprintf (
				dirname, HTTP_MULTI_PART_DIRNAME_SIZE,
				"%s/%s", http_cerver->uploads_path, request->dirname
			);

			has_directory = (written > 0) && (written < HTTP_MULTI_PART_DIRNAME_SIZE);
		}
	}

	return has_directory;

}

// creates the directory of the request's uploads if it does not exist yet,
// one that the request creates is removed at the end if it is empty
// returns 0 on success, 1 if it could not be created (the request is answered with 500)
static int http_receive_uploads_directory_create (
	HttpReceive *http_receive, const char *dirname
) {

	int retval = 0;

	struct stat status = CERVER_ZERO_INITIALIZER;
	if (stat (dirname, &status)) {
		if (files_create_recursive_dir (dirname, http_receive->http_cerver->uploads_dir_mode)) {
			const int error = errno;

			char error_description[CERVER_ERRNO_DESCRIPTION_SIZE] = { 0 };
			cerver_log_error (
				"Failed to create the uploads directory %s - errno %d (%s)",
				dirname, error, strerror_r (error, error_description, sizeof (error_description))
			);

			retval = http_receive_reject (
				http_receive, server_error, HTTP_RECEIVE_STATUS_UPLOAD_ERROR
			);
		}

		else {
			http_receive->upload_directory_created = true;
		}
	}

	return retval;

}

// returns 0 on success, 1 if the request was rejected
static int http_receive_init_mpart_parser (
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
	http_receive->request->multi_parts = dlist_init (http_multi_parts_return, NULL);

	int retval = (http_receive->mpart_parser && http_receive->request->multi_parts) ? 0 : 1;

	if (!retval) {
		http_receive->mpart_parser->data = http_receive;

		if (http_receive_uploads_directory_path (http_receive, dirname)) {
			retval = http_receive_uploads_directory_create (http_receive, dirname);
		}
	}

	return retval;

}

// answers "100 Continue" to a client that waits for it before sending the body
// returns 0 on success, 1 if the client is gone
static int http_receive_handle_expect_continue (HttpReceive *http_receive) {

	int retval = 0;

	const String *expect = http_receive->request->headers[HTTP_HEADER_EXPECT];
	if (expect && !strcasecmp (expect->str, expect_continue_value)) {
		const ssize_t sent = send (
			http_receive->cr->connection->socket->sock_fd,
			continue_response, continue_response_len, MSG_NOSIGNAL
		);

		if (sent != (ssize_t) continue_response_len) {
			http_receive_client_left (http_receive, (sent < 0) ? errno : EPIPE);
			retval = 1;
		}
	}

	return retval;

}

// prepares the multi-part parser of an authenticated request to a route
// returns 0 on success, 1 if the request was rejected
static int http_receive_handle_headers_multi_part (HttpReceive *http_receive) {

	int retval = 0;

	const String *content_type = http_receive->request->headers[HTTP_HEADER_CONTENT_TYPE];
	if (content_type && is_multipart (content_type->str)) {
		http_receive->is_multi_part = true;

		char boundary[HTTP_MULTI_PART_BOUNDARY_MAX_SIZE] = { 0 };
		if (!http_mpart_get_boundary (
			boundary, content_type->str, (unsigned int) content_type->len
		)) {
			http_receive->settings.on_body = http_receive_handle_mpart_body;

			if (http_receive_init_mpart_parser (http_receive, boundary)) {
				retval = http_receive->responded ? 1 : http_receive_reject (
					http_receive, server_error, HTTP_RECEIVE_STATUS_UPLOAD_ERROR
				);
			}
		}

		// a multi-part request without a boundary can not be parsed
		else {
			retval = http_receive_reject (
				http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
			);
		}
	}

	return retval;

}

// a request without a route is answered by the catch all handler,
// so its body is only checked against the limit, but it is not saved
static int http_receive_handle_body_discard (
	llhttp_t *parser, const char *at, size_t length
) {

	(void) at;

	return http_receive_parser_result (
		http_receive_check_body_size ((HttpReceive *) parser->data, length)
	);

}

// the request's id and the client's address,
// they are resolved with the headers, so the rejected requests also have them
static void http_receive_identify (HttpReceive *http_receive) {

	HttpCerver *http_cerver = http_receive->http_cerver;
	HttpRequest *request = http_receive->request;

	if (http_cerver->request_id) http_request_resolve_id (request);

	http_proxy_resolve_client_ip (
		http_cerver,
		http_receive->cr->connection ? http_receive->cr->connection->ip : NULL,
		request, request->client_ip
	);

}

// a body bigger than the limit is rejected before receiving it
// (the parser only has a content length when the request has a Content-Length)
static bool http_receive_body_too_large (
	const HttpReceive *http_receive, const llhttp_t *parser
) {

	return http_receive->max_body_size
		&& (parser->flags & F_CONTENT_LENGTH)
		&& (parser->content_length > http_receive->max_body_size);

}

static bool http_receive_handle_static_authenticate (
	HttpReceive *http_receive, HttpRequest *request
);

// the url and the headers are complete (each header was saved with its value):
// the route is selected and the request is authenticated before its body is received,
// so the files of a rejected request are never saved
// HTTP_RECEIVE_PARSER_STOP stops the parser (1 would only skip the body)
static int http_receive_handle_headers_completed (llhttp_t *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;
	HttpCerver *http_cerver = http_receive->http_cerver;
	HttpRequest *request = http_receive->request;

	http_receive->receive_status = HTTP_RECEIVE_STATUS_HEADERS;

	// the headers are complete, what follows is the body
	http_receive->stage = HTTP_STAGE_BODY;

	const uint8_t method = llhttp_get_method (parser);
	request->method = (RequestMethod) method;

	http_receive_identify (http_receive);

	// a method that the parser accepts but cerver does not know (RFC 9110 section 9.1)
	if (!http_request_method_is_valid (request->method)) {
		(void) http_receive_reject (
			http_receive, not_implemented_error, HTTP_RECEIVE_STATUS_REJECTED
		);

		return HTTP_RECEIVE_PARSER_STOP;
	}

	#ifdef HTTP_DEBUG
	cerver_log (
		LOG_TYPE_DEBUG, LOG_TYPE_HTTP,
		"%s %s",
		http_request_method_str (request->method),
		request->url->str
	);
	#endif

	http_receive->type = HTTP_RECEIVE_TYPE_ROUTE;
	http_receive_select (http_receive);

	const HttpEndpoint *endpoint = http_receive->endpoint;

	// an endpoint can have its own body limit
	http_receive->max_body_size = (endpoint && endpoint->resolved.max_body_size) ?
		endpoint->resolved.max_body_size : http_cerver->max_body_size;

	if (http_receive_body_too_large (http_receive, parser)) {
		(void) http_receive_reject (
			http_receive, payload_too_large_error, HTTP_RECEIVE_STATUS_LIMIT
		);

		return HTTP_RECEIVE_PARSER_STOP;
	}

	if (endpoint) {
		// the authentication handler can use the request's endpoint
		if (
			!endpoint->resolved.authentication_with_body
			&& !http_receive_handle_authenticate (http_cerver, http_receive, &endpoint->resolved, request)
		) {
			return HTTP_RECEIVE_PARSER_STOP;
		}

		if (http_receive_handle_headers_web_socket (http_receive, parser)) {
			return HTTP_RECEIVE_PARSER_STOP;
		}

		if (http_receive_handle_headers_multi_part (http_receive)) {
			return HTTP_RECEIVE_PARSER_STOP;
		}
	}

	// a request that no endpoint handles never saves its body
	else {
		if (http_receive_is_static_file (http_receive, request)) {
			http_receive->type = HTTP_RECEIVE_TYPE_FILE;
			http_receive->head = (request->method == REQUEST_METHOD_HEAD);

			if (!http_receive_handle_static_authenticate (http_receive, request)) return HTTP_RECEIVE_PARSER_STOP;
		}

		http_receive->settings.on_body = http_receive_handle_body_discard;
	}

	// a client that is gone stops the parser, without a response
	return http_receive_parser_result (http_receive_handle_expect_continue (http_receive));

}

// sets in real_filename (PATH_MAX bytes) the real path of the url inside the static path
// returns false if the path does not fit or if it is outside the static path ("..", symbolic links)
static bool http_static_path_resolve (
	const HttpStaticPath *static_path, const char *url, char *real_filename
) {

	char filename[HTTP_CERVER_STATIC_FILE_SIZE] = { 0 };
	char real_static_path[PATH_MAX] = { 0 };

	// a path that does not fit is never truncated
	const int written = snprintf (
		filename, HTTP_CERVER_STATIC_FILE_SIZE,
		"%s%s", static_path->path->str, url
	);

	if ((written < 0) || (written >= HTTP_CERVER_STATIC_FILE_SIZE)) return false;

	if (!realpath (filename, real_filename)) return false;
	if (!realpath (static_path->path->str, real_static_path)) return false;

	const size_t real_static_path_len = strlen (real_static_path);

	return !strncmp (real_filename, real_static_path, real_static_path_len)
		&& (real_filename[real_static_path_len] == '/');

}

// the first static path that has the file of the url, with its real path and its status,
// only regular files are served
// returns NULL if none has it
static const HttpStaticPath *http_static_path_find (
	const HttpCerver *http_cerver, const char *url,
	char *real_filename, struct stat *filestatus
) {

	const HttpStaticPath *found = NULL;

	for (ListElement *le = dlist_start (http_cerver->static_paths); le && !found; le = le->next) {
		const HttpStaticPath *static_path = (const HttpStaticPath *) le->data;

		(void) memset (filestatus, 0, sizeof (struct stat));
		if (
			http_static_path_resolve (static_path, url, real_filename)
			&& !stat (real_filename, filestatus) && S_ISREG (filestatus->st_mode)
		) {
			found = static_path;
		}
	}

	return found;

}

// the first static path that has authentication, NULL if every one is public
static const HttpStaticPath *http_static_path_first_protected (const HttpCerver *http_cerver) {

	const HttpStaticPath *found = NULL;

	for (ListElement *le = dlist_start (http_cerver->static_paths); le && !found; le = le->next) {
		const HttpStaticPath *static_path = (const HttpStaticPath *) le->data;
		if (static_path->auth_type != HTTP_ROUTE_AUTH_TYPE_NONE) found = static_path;
	}

	return found;

}

// the request of a file is authenticated with the static path that has it,
// or with the first protected one when none has it,
// so a file that does not exist is answered like a protected one
// returns true if the request continues, a failed authentication was already answered
static bool http_receive_handle_static_authenticate (
	HttpReceive *http_receive, HttpRequest *request
) {

	bool authenticated = true;

	char real_filename[PATH_MAX] = { 0 };
	struct stat filestatus = CERVER_ZERO_INITIALIZER;

	const HttpStaticPath *static_path = http_static_path_find (
		http_receive->http_cerver, request->url->str, real_filename, &filestatus
	);

	if (!static_path) static_path = http_static_path_first_protected (http_receive->http_cerver);

	if (static_path && (static_path->auth_type != HTTP_ROUTE_AUTH_TYPE_NONE)) {
		HttpRouteSettings settings;
		(void) memset (&settings, 0, sizeof (HttpRouteSettings));
		settings.auth_type = static_path->auth_type;
		settings.decode_data = static_path->decode_data;
		settings.delete_decoded_data = static_path->delete_decoded_data;
		settings.authentication_handler = static_path->authentication_handler;

		authenticated = http_receive_handle_authenticate (
			http_receive->http_cerver, http_receive, &settings, request
		);
	}

	return authenticated;

}

// the request was authenticated in on_headers_complete with its static path
static void http_receive_handle_serve_file (HttpReceive *http_receive) {

	char real_filename[PATH_MAX] = { 0 };
	struct stat filestatus = CERVER_ZERO_INITIALIZER;

	const bool found = http_static_path_find (
		http_receive->http_cerver, http_receive->request->url->str, real_filename, &filestatus
	) != NULL;

	if (found) {
		http_receive->served_file = http_receive->request->url->str;

		(void) http_response_send_file_internal (
			http_receive, HTTP_STATUS_OK,
			real_filename, &filestatus
		);
	}

	else {
		#ifdef HTTP_RECEIVE_DEBUG
		cerver_log_warning (
			"Unable to find file %s",
			http_receive->request->url->str
		);
		#endif

		http_receive_handle_catch_all (
			http_receive->http_cerver,
			http_receive, http_receive->request
		);
	}

}

// an endpoint whose authentication needs the body is authenticated now,
// any other endpoint was authenticated in on_headers_complete
static void http_receive_handle_route (HttpReceive *http_receive) {

	const HttpEndpoint *endpoint = http_receive->endpoint;

	if (
		endpoint->resolved.authentication_with_body
		&& !http_receive_handle_authenticate (
			http_receive->http_cerver, http_receive, &endpoint->resolved, http_receive->request
		)
	) {
		return;
	}

	http_receive_handle_match (http_receive, http_receive->request, endpoint);

}

// adds the CORS headers of a preflight request from a whitelisted origin
static void http_receive_handle_preflight (
	const HttpReceive *http_receive, HttpResponse *response
) {

	const HttpRequest *request = http_receive->request;

	if (
		request->headers[HTTP_HEADER_ACCESS_CONTROL_REQUEST_METHOD]
		&& !http_response_add_whitelist_cors_header_from_request (http_receive, response)
	) {
		(void) http_response_add_cors_allow_methods_header (response, http_receive->node->allow);

		const String *request_headers = request->headers[HTTP_HEADER_ACCESS_CONTROL_REQUEST_HEADERS];
		if (request_headers) {
			(void) http_response_add_header (
				response, HTTP_HEADER_ACCESS_CONTROL_ALLOW_HEADERS, request_headers->str
			);
		}
	}

}

// a path without an OPTIONS handler is answered with its Allow header
// and, when its endpoints enabled CORS, with the preflight headers
static void http_receive_handle_options (HttpReceive *http_receive) {

	HttpResponse *response = http_response_create (HTTP_STATUS_NO_CONTENT, NULL, 0);
	if (response) {
		(void) http_response_add_header (response, HTTP_HEADER_ALLOW, http_receive->node->allow);

		if (http_receive->node->cors) http_receive_handle_preflight (http_receive, response);

		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

}

// a path without a handler for the request's method,
// answered with the custom handler or with the default 405 response
static void http_receive_handle_method_not_allowed (HttpReceive *http_receive) {

	const HttpHandler handler = http_receive->http_cerver->tree->method_not_allowed_handler;
	if (handler) {
		handler (http_receive, http_receive->request);
		return;
	}

	HttpResponse *response = http_response_create (
		HTTP_STATUS_METHOD_NOT_ALLOWED,
		method_not_allowed_error->data, method_not_allowed_error->data_len
	);

	if (response) {
		(void) http_response_add_header (response, HTTP_HEADER_ALLOW, http_receive->node->allow);
		(void) http_response_add_content_type_header (response, HTTP_CONTENT_TYPE_JSON);
		(void) http_response_add_content_length_header (response, response->data_len);

		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

}

// a path with a trailing slash is redirected with 308 (that keeps the method and the body)
static void http_receive_handle_redirect (HttpReceive *http_receive) {

	const HttpRequest *request = http_receive->request;

	HttpResponse *response = http_response_create (HTTP_STATUS_PERMANENT_REDIRECT, NULL, 0);
	if (response) {
		(void) http_response_add_custom_header (
			response, HTTP_HEADER_LOCATION, "%.*s%s%s",
			(int) (request->url->len - 1), request->url->str,
			request->query ? query_separator : "",
			request->query ? request->query->str : ""
		);

		(void) http_response_add_content_length_header (response, 0);

		(void) http_response_send (response, http_receive);
		http_response_delete (response);
	}

}

// answers a request that matched a path of the routes tree, or the catch all handler
static void http_receive_handle_dispatch (HttpReceive *http_receive) {

	switch (http_receive->dispatch) {
		case HTTP_RECEIVE_DISPATCH_ENDPOINT:
			http_receive_handle_route (http_receive);
			break;

		case HTTP_RECEIVE_DISPATCH_OPTIONS:
			http_receive_handle_options (http_receive);
			break;

		case HTTP_RECEIVE_DISPATCH_METHOD_NOT_ALLOWED:
			http_receive_handle_method_not_allowed (http_receive);
			break;

		case HTTP_RECEIVE_DISPATCH_REDIRECT:
			http_receive_handle_redirect (http_receive);
			break;

		default:
			http_receive_handle_catch_all (
				http_receive->http_cerver,
				http_receive, http_receive->request
			);
			break;
	}

}

static const char *http_receive_url (const HttpReceive *http_receive) {

	const HttpRequest *request = http_receive->request;

	return (request && request->url) ? request->url->str : "";

}

// a handler that returned without sending a response is answered with 500,
// instead of closing the connection without one (a proxy answers it with 502)
static void http_receive_handle_unanswered (HttpReceive *http_receive) {

	if (!http_receive->responded) {
		cerver_log_with_date (
			LOG_TYPE_WARNING, LOG_TYPE_HTTP,
			"http_receive_handle () - the request was not answered - fd %d - %s %s",
			http_receive->cr->connection->socket->sock_fd,
			http_request_method_str (http_receive->request->method),
			http_receive_url (http_receive)
		);

		(void) http_response_send (server_error, http_receive);

		CERVER_STATS_ADD (http_receive->http_cerver->n_unanswered_requests, 1);
	}

}

// the endpoint and its authentication were resolved in on_headers_complete,
// now that the body is complete the request is handled
static int http_receive_handle_message_completed (llhttp_t *parser) {

	HttpReceive *http_receive = (HttpReceive *) parser->data;

	http_receive->receive_status = HTTP_RECEIVE_STATUS_COMPLETED;
	http_receive->stage = HTTP_STAGE_HANDLER;

	// what the client asked (HTTP/1.1 without close, HTTP/1.0 with keep-alive),
	// the connection is still closed after the response
	http_receive->keep_alive_requested = llhttp_should_keep_alive (parser);

	switch (http_receive->type) {
		case HTTP_RECEIVE_TYPE_FILE:
			http_receive_handle_serve_file (http_receive);
			break;

		default:
			http_receive_handle_dispatch (http_receive);
			break;
	}

	http_receive_handle_unanswered (http_receive);

	// the client left while its response was sent
	if (http_receive->response_failed) {
		http_receive->receive_status = HTTP_RECEIVE_STATUS_RESPONSE_FAILED;
	}

	// only one request is handled by connection,
	// the parser pauses here, so a pipelined request in the same buffer is never parsed
	// (a callback can not call llhttp_pause (), it returns HPE_PAUSED)
	return HPE_PAUSED;

}

// reads the data that the client already sent, up to HTTP_RECEIVE_DRAIN_SIZE bytes
// waiting tells if recv () waits for more data, with the socket's timeout
static void http_receive_drain (const int sock_fd, const bool waiting) {

	char buffer[HTTP_RECEIVE_DRAIN_BUFFER_SIZE];
	const int flags = waiting ? 0 : MSG_DONTWAIT;

	size_t drained = 0;
	while (drained < HTTP_RECEIVE_DRAIN_SIZE) {
		const ssize_t received = recv (sock_fd, buffer, sizeof (buffer), flags);
		if (received <= 0) break;

		drained += (size_t) received;
	}

}

// reads, without waiting, the data that the client already sent
// up to HTTP_RECEIVE_DRAIN_SIZE bytes
// returns 0 on success, 1 on error
u8 http_receive_drain_available (const int sock_fd) {

	u8 retval = 1;

	if (sock_fd > -1) {
		http_receive_drain (sock_fd, false);
		retval = 0;
	}

	return retval;

}

// closes the connection after its response was sent
// the data that the client already sent is read first,
// because a close () with unread data answers with a RST that can destroy the response
// a rejected request waits a bit for the rest of its body
static void http_receive_close (
	HttpReceive *http_receive, const bool rejected
) {

	Connection *connection = http_receive->cr->connection;
	const int sock_fd = connection->socket->sock_fd;

	if (!http_receive->keep_alive && (sock_fd > -1)) {
		(void) shutdown (sock_fd, SHUT_WR);

		if (rejected) {
			(void) sock_set_timeout (sock_fd, HTTP_RECEIVE_DRAIN_TIMEOUT);
		}

		http_receive_drain (sock_fd, rejected);

		connection_end (connection);
	}

}

// a request that the parser could not parse is answered with 400,
// unless a callback already answered it (a limit, a failed authentication, ...)
// or it stopped the parser because the client is gone
static void http_receive_handle_parser_error (
	HttpReceive *http_receive, const llhttp_errno_t parser_errno,
	const size_t n_parsed, const ssize_t received
) {

	if (!http_receive->responded && (http_receive->receive_status != HTTP_RECEIVE_STATUS_CLOSED)) {
		const char *reason = llhttp_get_error_reason (&http_receive->parser);

		cerver_log_with_date (
			LOG_TYPE_ERROR, LOG_TYPE_HTTP,
			"http_receive_handle () - llhttp_execute () failed - n parsed %zu / received %zd"
			" - fd %d - %s: %s - status %s",
			n_parsed, received,
			http_receive->cr->connection->socket->sock_fd,
			llhttp_errno_name (parser_errno),
			reason ? reason : http_receive_parser_no_reason,
			http_receive_status_str (http_receive->receive_status)
		);

		(void) http_receive_reject (
			http_receive, bad_request_error, HTTP_RECEIVE_STATUS_PARSER_ERROR
		);
	}

	http_receive_close (http_receive, true);

}

// returns true while the request's headers have not been completed
// (HTTP_RECEIVE_STATUS_HEADERS is set when they are complete)
bool http_receive_is_receiving_headers (const HttpReceive *http_receive) {

	return (http_receive->receive_status == HTTP_RECEIVE_STATUS_NONE);

}

// a request that did not arrive in time is answered with 408
// if the client did not send anything, the connection is only closed
void http_receive_handle_timeout (
	HttpReceive *http_receive, const bool received
) {

	if (received && !http_receive->responded) {
		(void) pthread_mutex_lock (http_receive->http_cerver->mutex);
		http_receive->http_cerver->n_timeout_requests += 1;
		(void) pthread_mutex_unlock (http_receive->http_cerver->mutex);

		(void) http_receive_reject (
			http_receive, request_timeout_error, HTTP_RECEIVE_STATUS_TIMEOUT
		);
	}

	else if (!http_receive->responded) {
		http_receive->receive_status = HTTP_RECEIVE_STATUS_TIMEOUT;
	}

	http_receive_close (http_receive, false);

}

// the bytes of the buffer that the parser consumed,
// after an error or a pause llhttp tells where it stopped
static size_t http_receive_parsed_bytes (
	const HttpReceive *http_receive, const llhttp_errno_t parser_errno,
	const char *buffer, const size_t length
) {

	size_t parsed = length;

	const char *position = llhttp_get_error_pos (&http_receive->parser);
	if (
		(parser_errno != HPE_OK) && position
		&& (position >= buffer) && (position <= (buffer + length))
	) {
		parsed = (size_t) (position - buffer);
	}

	return parsed;

}

static void http_receive_handle (
	HttpReceive *http_receive,
	ssize_t rc, char *packet_buffer
) {

	// the first bytes start the request line
	if (http_receive->stage == HTTP_STAGE_QUEUED) {
		http_receive->stage = HTTP_STAGE_REQUEST_LINE;
	}

	const llhttp_errno_t parser_errno = llhttp_execute (
		&http_receive->parser, packet_buffer, (size_t) rc
	);

	const size_t n_parsed = http_receive_parsed_bytes (
		http_receive, parser_errno, packet_buffer, (size_t) rc
	);

	switch (parser_errno) {
		// all the data was parsed, the request is not complete yet
		case HPE_OK: break;

		// the request was completed and handled,
		// the bytes after it (a pipelined request) were not parsed
		case HPE_PAUSED:
		case HPE_PAUSED_UPGRADE:
			http_receive->unparsed = (size_t) rc - n_parsed;

			// the connection belongs to its WebSocket now, with the bytes that followed the request
			if (http_receive->upgraded) http_receive_web_socket_pending (http_receive, packet_buffer + n_parsed);
			else http_receive_close (http_receive, false);
			break;

		default:
			http_receive_handle_parser_error (http_receive, parser_errno, n_parsed, rc);
			break;
	}

}

#pragma endregion
