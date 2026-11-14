#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/arena.h"
#include "cerver/collections/pool.h"

#include "cerver/version.h"

#include "cerver/connection/connection.h"
#include "cerver/files/files.h"
#include "cerver/handler/errors.h"
#include "cerver/handler/handler.h"
#include "cerver/network/network.h"
#include "cerver/packets/packets.h"
#include "cerver/system/timer.h"

#include "cerver/crypto/memory.h"

#include "cerver/http/core/custom.h"
#include "cerver/http/core/http.h"
#include "cerver/http/core/internal.h"
#include "cerver/http/core/recycle.h"
#include "cerver/http/core/settings.h"
#include "cerver/http/headers/fields.h"
#include "cerver/http/json/json.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"
#include "cerver/http/parser/api.h"
#include "cerver/http/parser/helpers.h"
#include "cerver/http/requests/method.h"
#include "cerver/http/requests/multipart.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/requests/url.h"
#include "cerver/http/responses/compression.h"
#include "cerver/http/responses/response.h"
#include "cerver/http/responses/stream.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/security/proxy.h"
#include "cerver/http/websockets/deflate.h"
#include "cerver/http/websockets/handshake.h"
#include "cerver/http/websockets/internal.h"
#include "cerver/http/websockets/registry.h"
#include "cerver/http/websockets/websocket.h"

#include "cerver/threads/worker.h"

#include "cerver/utils/base64.h"
#include "cerver/utils/log.h"
#include "cerver/utils/utils.h"

HttpResponse *oki_doki = NULL;
HttpResponse *bad_request_error = NULL;
HttpResponse *bad_user_error = NULL;
HttpResponse *bad_auth_error = NULL;
HttpResponse *not_found_error = NULL;
HttpResponse *server_error = NULL;

HttpResponse *catch_all = NULL;

HttpResponse *payload_too_large_error = NULL;
HttpResponse *uri_too_long_error = NULL;
HttpResponse *header_fields_too_large_error = NULL;
HttpResponse *request_timeout_error = NULL;
HttpResponse *method_not_allowed_error = NULL;
HttpResponse *not_implemented_error = NULL;
HttpResponse *http_version_not_supported_error = NULL;

// the rejections of an upgrade to a WebSocket endpoint
HttpResponse *upgrade_required_error = NULL;
HttpResponse *forbidden_error = NULL;
HttpResponse *web_socket_unavailable_error = NULL;

static const char *upgrade_required_json = { "{\"error\": \"Upgrade to WebSocket required!\"}" };
static const char *web_socket_unavailable_json = { "{\"error\": \"Too many WebSocket connections!\"}" };

// the HTTP cervers of the process that hold a reference to the shared state
static DoubleList *http_cervers = NULL;
static pthread_mutex_t http_cervers_mutex = PTHREAD_MUTEX_INITIALIZER;

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
		http_cerver->max_custom_header_size = HTTP_CERVER_DEFAULT_MAX_CUSTOM_HEADER_SIZE;

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

	http_version_not_supported_error = http_response_create_json_key_value (
		HTTP_STATUS_HTTP_VERSION_NOT_SUPPORTED, "error", "HTTP version not supported!"
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
		&& http_version_not_supported_error
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

		cerver_crypto_cleanse (material, material_length);
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
	http_response_delete (http_version_not_supported_error);
	http_version_not_supported_error = NULL;

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
void http_cerver_init_admin_routes (
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
