#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/string.h"
#include "cerver/types/types.h"

#include "cerver/collections/dlist.h"

#include "cerver/http/core/http.h"
#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"
#include "cerver/http/requests/request.h"
#include "cerver/http/routes/compatibility.h"
#include "cerver/http/routes/middleware.h"
#include "cerver/http/routes/route.h"
#include "cerver/http/routes/tree.h"
#include "cerver/http/routes/validator.h"
#include "cerver/http/security/auth.h"
#include "cerver/http/websockets/internal.h"

#include "cerver/utils/log.h"

static const char *http_tree_root_path = { "/" };
static const char *http_tree_empty_path = { "" };

// a level of the printed routes, the indentation of the first routes API print
static const char *http_tree_indent = { "    " };
static const char *http_tree_values_separator = { ", " };

// the depth of the tree that is printed with indentation
#define HTTP_TREE_PRINT_INDENT_SIZE			64

// the methods or the per method values of a printed route
#define HTTP_TREE_PRINT_VALUES_SIZE			256

// the slices of a segment of a path
typedef struct HttpTreeSegment {

	HttpRouteNodeType type;

	// the text of a static segment
	const char *text;
	size_t text_len;

	// the name of a param or a wildcard
	const char *name;
	size_t name_len;

	// the validator's name of a param
	const char *validator;
	size_t validator_len;

} HttpTreeSegment;

static void http_route_settings_init (HttpRouteSettings *settings);

static void http_route_settings_end (HttpRouteSettings *settings);

const char *http_trailing_slash_to_string (
	const HttpTrailingSlash trailing_slash
) {

	switch (trailing_slash) {
		#define XX(num, name, string, description) case HTTP_TRAILING_SLASH_##name: return #string;
		HTTP_TRAILING_SLASH_MAP(XX)
		#undef XX

		default: break;
	}

	return http_trailing_slash_to_string (HTTP_TRAILING_SLASH_NONE);

}

const char *http_trailing_slash_description (
	const HttpTrailingSlash trailing_slash
) {

	switch (trailing_slash) {
		#define XX(num, name, string, description) case HTTP_TRAILING_SLASH_##name: return #description;
		HTTP_TRAILING_SLASH_MAP(XX)
		#undef XX

		default: break;
	}

	return http_trailing_slash_description (HTTP_TRAILING_SLASH_NONE);

}

#pragma region settings

static void http_route_settings_init (HttpRouteSettings *settings) {

	settings->set = 0;

	settings->modifier = HTTP_ROUTE_MODIFIER_NONE;

	settings->auth_type = HTTP_ROUTE_AUTH_TYPE_NONE;

	settings->decode_data = NULL;
	settings->delete_decoded_data = NULL;

	settings->authentication_handler = NULL;

	settings->authentication_with_body = false;

	settings->custom_data = NULL;
	settings->delete_custom_data = NULL;

	settings->max_body_size = 0;

	settings->cors = false;

	settings->etag = false;

	settings->compression = false;

	http_middlewares_init (&settings->middlewares);

	http_web_socket_settings_init (&settings->web_socket);

}

// deletes the custom data that the settings own
static void http_route_settings_end (HttpRouteSettings *settings) {

	if (
		(settings->set & HTTP_ROUTE_SETTING_CUSTOM_DATA)
		&& settings->custom_data && settings->delete_custom_data
	) {
		settings->delete_custom_data (settings->custom_data);
	}

	settings->custom_data = NULL;

	http_middlewares_end (&settings->middlewares);

	http_web_socket_settings_end (&settings->web_socket);

}

// takes from source the settings that were not set in resolved
static void http_route_settings_inherit (
	HttpRouteSettings *resolved, const HttpRouteSettings *source
) {

	const unsigned int missing = source->set & ~resolved->set;

	if (missing & HTTP_ROUTE_SETTING_MODIFIER) resolved->modifier = source->modifier;

	if (missing & HTTP_ROUTE_SETTING_AUTH_TYPE) resolved->auth_type = source->auth_type;

	if (missing & HTTP_ROUTE_SETTING_DECODE_DATA) {
		resolved->decode_data = source->decode_data;
		resolved->delete_decoded_data = source->delete_decoded_data;
	}

	if (missing & HTTP_ROUTE_SETTING_AUTHENTICATION_HANDLER) {
		resolved->authentication_handler = source->authentication_handler;
	}

	if (missing & HTTP_ROUTE_SETTING_AUTHENTICATION_WITH_BODY) {
		resolved->authentication_with_body = source->authentication_with_body;
	}

	if (missing & HTTP_ROUTE_SETTING_CUSTOM_DATA) resolved->custom_data = source->custom_data;

	if (missing & HTTP_ROUTE_SETTING_MAX_BODY_SIZE) resolved->max_body_size = source->max_body_size;

	if (missing & HTTP_ROUTE_SETTING_CORS) resolved->cors = source->cors;

	if (missing & HTTP_ROUTE_SETTING_ETAG) resolved->etag = source->etag;

	if (missing & HTTP_ROUTE_SETTING_COMPRESSION) resolved->compression = source->compression;

	resolved->set |= missing;

	http_web_socket_settings_inherit (&resolved->web_socket, &source->web_socket);

}

#pragma endregion

#pragma region endpoint

static HttpEndpoint *http_endpoint_new (void) {

	HttpEndpoint *endpoint = (HttpEndpoint *) malloc (sizeof (HttpEndpoint));
	if (endpoint) {
		endpoint->path = NULL;

		endpoint->method = (RequestMethod) REQUEST_METHOD_UNDEFINED;
		endpoint->handler = NULL;

		endpoint->group = NULL;
		endpoint->route = NULL;

		http_route_settings_init (&endpoint->settings);
		http_route_settings_init (&endpoint->resolved);

		endpoint->n_params = 0;
		endpoint->params_names = NULL;

		endpoint->stats = NULL;
		endpoint->file_stats = NULL;
		endpoint->owns_stats = false;
	}

	return endpoint;

}

static void http_endpoint_params_names_delete (HttpEndpoint *endpoint) {

	if (endpoint->params_names) {
		for (unsigned int idx = 0; idx < endpoint->n_params; idx++) {
			free (endpoint->params_names[idx]);
		}

		free (endpoint->params_names);
		endpoint->params_names = NULL;
	}

	endpoint->n_params = 0;

}

void http_endpoint_delete (void *endpoint_ptr) {

	if (endpoint_ptr) {
		HttpEndpoint *endpoint = (HttpEndpoint *) endpoint_ptr;

		str_delete (endpoint->path);

		http_route_settings_end (&endpoint->settings);

		// the resolved settings only keep references to the custom data
		http_middlewares_end (&endpoint->resolved.middlewares);

		http_endpoint_params_names_delete (endpoint);

		if (endpoint->owns_stats) {
			http_route_stats_delete (endpoint->stats);
			http_route_file_stats_delete (endpoint->file_stats);
		}

		free (endpoint);
	}

}

// creates an endpoint with its complete path
HttpEndpoint *http_endpoint_create (
	const char *path, const RequestMethod method, const HttpHandler handler
) {

	HttpEndpoint *endpoint = http_endpoint_new ();
	if (endpoint) {
		endpoint->path = str_new (path);
		endpoint->method = method;
		endpoint->handler = handler;
	}

	return endpoint;

}

const String *http_endpoint_get_path (
	const HttpEndpoint *endpoint
) {

	return endpoint ? endpoint->path : NULL;

}

RequestMethod http_endpoint_get_method (
	const HttpEndpoint *endpoint
) {

	return endpoint ? endpoint->method : (RequestMethod) REQUEST_METHOD_UNDEFINED;

}

void http_endpoint_set_modifier (
	HttpEndpoint *endpoint, const HttpRouteModifier modifier
) {

	if (endpoint) {
		endpoint->settings.modifier = modifier;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_MODIFIER;
	}

}

void http_endpoint_set_auth (
	HttpEndpoint *endpoint, const HttpRouteAuthType auth_type
) {

	if (endpoint) {
		endpoint->settings.auth_type = auth_type;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_AUTH_TYPE;
	}

}

void http_endpoint_set_decode_data (
	HttpEndpoint *endpoint,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
) {

	if (endpoint) {
		endpoint->settings.decode_data = decode_data;
		endpoint->settings.delete_decoded_data = delete_decoded_data;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_DECODE_DATA;
	}

}

void http_endpoint_set_decode_data_into_json (
	HttpEndpoint *endpoint
) {

	http_endpoint_set_decode_data (endpoint, http_decode_data_into_json, free);

}

void http_endpoint_set_authentication_handler (
	HttpEndpoint *endpoint, const HttpAuthenticationHandler authentication_handler
) {

	if (endpoint) {
		endpoint->settings.authentication_handler = authentication_handler;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_AUTHENTICATION_HANDLER;
	}

}

void http_endpoint_set_authentication_with_body (
	HttpEndpoint *endpoint, const bool authentication_with_body
) {

	if (endpoint) {
		endpoint->settings.authentication_with_body = authentication_with_body;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_AUTHENTICATION_WITH_BODY;
	}

}

// the endpoint owns the data, it is deleted with delete_custom_data
void http_endpoint_set_custom_data (
	HttpEndpoint *endpoint, void *custom_data, void (*delete_custom_data)(void *)
) {

	if (endpoint) {
		endpoint->settings.custom_data = custom_data;
		endpoint->settings.delete_custom_data = delete_custom_data;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_CUSTOM_DATA;
	}

}

// the custom data of the endpoint, or the one that it inherits from its groups
const void *http_endpoint_get_custom_data (
	const HttpEndpoint *endpoint
) {

	const void *custom_data = NULL;

	if (endpoint) {
		custom_data = (endpoint->settings.set & HTTP_ROUTE_SETTING_CUSTOM_DATA) ?
			endpoint->settings.custom_data : endpoint->resolved.custom_data;
	}

	return custom_data;

}

void http_endpoint_set_max_body_size (
	HttpEndpoint *endpoint, const size_t max_body_size
) {

	if (endpoint) {
		endpoint->settings.max_body_size = max_body_size;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_MAX_BODY_SIZE;
	}

}

void http_endpoint_set_cors (
	HttpEndpoint *endpoint, const bool cors
) {

	if (endpoint) {
		endpoint->settings.cors = cors;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_CORS;
	}

}

void http_endpoint_set_etag (
	HttpEndpoint *endpoint, const bool etag
) {

	if (endpoint) {
		endpoint->settings.etag = etag;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_ETAG;
	}

}

void http_endpoint_set_compression (
	HttpEndpoint *endpoint, const bool compression
) {

	if (endpoint) {
		endpoint->settings.compression = compression;
		endpoint->settings.set |= HTTP_ROUTE_SETTING_COMPRESSION;
	}

}

u8 http_endpoint_add_before (
	HttpEndpoint *endpoint, const HttpBeforeMiddleware middleware
) {

	return endpoint ? http_middlewares_add_before (&endpoint->settings.middlewares, middleware) : 1;

}

u8 http_endpoint_add_after (
	HttpEndpoint *endpoint, const HttpAfterMiddleware middleware
) {

	return endpoint ? http_middlewares_add_after (&endpoint->settings.middlewares, middleware) : 1;

}

#pragma endregion

#pragma region websocket

void http_endpoint_set_web_socket (
	HttpEndpoint *endpoint, const HttpWebSocketHandlers *handlers
) {

	if (endpoint && handlers) {
		http_endpoint_set_modifier (endpoint, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
		http_web_socket_settings_set_handlers (&endpoint->settings.web_socket, handlers);
	}

}

void http_endpoint_set_web_socket_max_message (
	HttpEndpoint *endpoint, const size_t max_message
) {

	if (endpoint) http_web_socket_settings_set_max_message (&endpoint->settings.web_socket, max_message);

}

u8 http_endpoint_set_web_socket_protocols (
	HttpEndpoint *endpoint, const char * const *protocols, const unsigned int n_protocols
) {

	return endpoint ?
		http_web_socket_settings_set_protocols (&endpoint->settings.web_socket, protocols, n_protocols) : 1;

}

void http_endpoint_set_web_socket_compression (
	HttpEndpoint *endpoint, const bool compression
) {

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = compression;

	http_endpoint_set_web_socket_compression_options (endpoint, &options);

}

void http_endpoint_set_web_socket_compression_options (
	HttpEndpoint *endpoint, const HttpWebSocketDeflateOptions *options
) {

	if (endpoint) http_web_socket_settings_set_compression (&endpoint->settings.web_socket, options);

}

void http_endpoint_set_web_socket_origin_check (
	HttpEndpoint *endpoint, const bool origin_check
) {

	if (endpoint) http_web_socket_settings_set_origin_check (&endpoint->settings.web_socket, origin_check);

}

void http_endpoint_set_web_socket_ping_milliseconds (
	HttpEndpoint *endpoint, const unsigned int ping_interval, const unsigned int pong_timeout
) {

	if (endpoint) http_web_socket_settings_set_ping_milliseconds (&endpoint->settings.web_socket, ping_interval, pong_timeout);

}

void http_endpoint_set_web_socket_close_timeout_milliseconds (
	HttpEndpoint *endpoint, const unsigned int close_timeout
) {

	if (endpoint) http_web_socket_settings_set_close_timeout_milliseconds (&endpoint->settings.web_socket, close_timeout);

}

void http_endpoint_set_web_socket_send_timeout_milliseconds (
	HttpEndpoint *endpoint, const unsigned int send_timeout
) {

	if (endpoint) http_web_socket_settings_set_send_timeout_milliseconds (&endpoint->settings.web_socket, send_timeout);

}

#pragma endregion

#pragma region paths

// joins a path relative to a prefix, eg. "/api" and "users" is "/api/users"
String *http_tree_path_join (
	const char *prefix, const char *path
) {

	const char *relative = path ? path : http_tree_empty_path;
	if (*relative == HTTP_TREE_PATH_SEPARATOR) relative += 1;

	const bool root = !strcmp (prefix, http_tree_root_path);

	String *joined = NULL;
	if (!*relative) joined = str_new (prefix);
	else if (root) joined = str_create ("/%s", relative);
	else joined = str_create ("%s/%s", prefix, relative);

	return joined;

}

static bool http_tree_name_is_valid (const char *name, const size_t name_len) {

	bool valid = true;

	for (size_t idx = 0; idx < name_len; idx++) {
		if (!isalnum ((unsigned char) name[idx]) && (name[idx] != '_')) {
			valid = false;
			break;
		}
	}

	return valid;

}

// parses ":name" or ":name<validator>"
// returns 0 on success, 1 on error
static u8 http_tree_segment_parse_param (
	HttpTreeSegment *segment, const char *text, const size_t text_len,
	char *error, const size_t error_size
) {

	const char *name = text + 1;
	const char *start = (const char *) memchr (name, HTTP_TREE_VALIDATOR_START, text_len - 1);

	segment->type = HTTP_ROUTE_NODE_TYPE_PARAM;
	segment->name = name;
	segment->name_len = start ? (size_t) (start - name) : text_len - 1;

	if (!segment->name_len || !http_tree_name_is_valid (segment->name, segment->name_len)) {
		(void) snprintf (error, error_size, "the param \"%.*s\" needs a name of letters, digits or '_'", (int) text_len, text);
		return 1;
	}

	if (start) {
		segment->validator = start + 1;
		segment->validator_len = (size_t) ((text + text_len - 1) - segment->validator);

		if (
			(text[text_len - 1] != HTTP_TREE_VALIDATOR_END)
			|| !segment->validator_len
			|| (segment->validator_len >= HTTP_PARAM_VALIDATOR_NAME_SIZE)
			|| !http_tree_name_is_valid (segment->validator, segment->validator_len)
		) {
			(void) snprintf (error, error_size, "the param \"%.*s\" has an invalid validator", (int) text_len, text);
			return 1;
		}
	}

	return 0;

}

// parses a segment of a path
// returns 0 on success, 1 on error
static u8 http_tree_segment_parse (
	HttpTreeSegment *segment, const char *text, const size_t text_len, const bool last,
	char *error, const size_t error_size
) {

	(void) memset (segment, 0, sizeof (HttpTreeSegment));

	segment->text = text;
	segment->text_len = text_len;

	if (!text_len) {
		(void) snprintf (error, error_size, "a path can not have empty segments (\"//\" or a trailing \"/\")");
		return 1;
	}

	if (text[0] == HTTP_TREE_PARAM_PREFIX) {
		return http_tree_segment_parse_param (segment, text, text_len, error, error_size);
	}

	if (text[0] == HTTP_TREE_WILDCARD_PREFIX) {
		segment->type = HTTP_ROUTE_NODE_TYPE_WILDCARD;
		segment->name = text + 1;
		segment->name_len = text_len - 1;

		if (!last) {
			(void) snprintf (error, error_size, "the wildcard \"%.*s\" must be the last segment", (int) text_len, text);
			return 1;
		}

		if (!http_tree_name_is_valid (segment->name, segment->name_len)) {
			(void) snprintf (error, error_size, "the wildcard \"%.*s\" has an invalid name", (int) text_len, text);
			return 1;
		}

		return 0;
	}

	segment->type = HTTP_ROUTE_NODE_TYPE_STATIC;

	return 0;

}

// iterates the segments of a path without its first '/'
// returns 0 on success, 1 on error
typedef u8 (*HttpTreeSegmentCallback)(const HttpTreeSegment *segment, void *data);

static u8 http_tree_path_segments (
	const char *path, HttpTreeSegmentCallback callback, void *data,
	char *error, const size_t error_size
) {

	const char *relative = (*path == HTTP_TREE_PATH_SEPARATOR) ? path + 1 : path;
	const size_t relative_len = strlen (relative);

	u8 retval = 0;
	HttpTreeSegment segment = { HTTP_ROUTE_NODE_TYPE_NONE, NULL, 0, NULL, 0, NULL, 0 };

	size_t position = 0;
	while (!retval && (position < relative_len)) {
		const char *start = relative + position;
		const char *end = (const char *) memchr (start, HTTP_TREE_PATH_SEPARATOR, relative_len - position);
		const size_t text_len = end ? (size_t) (end - start) : relative_len - position;
		const bool last = !end;

		retval = http_tree_segment_parse (&segment, start, text_len, last, error, error_size);

		// a trailing '/' is an empty last segment
		if (!retval && end && ((size_t) (end - relative) + 1 == relative_len)) {
			(void) snprintf (error, error_size, "a path can not end with \"/\"");
			retval = 1;
		}

		if (!retval && callback) retval = callback (&segment, data);

		position += text_len + 1;
	}

	return retval;

}

// checks the syntax of a path relative to a group
// returns 0 if it is valid, 1 if it is not (the reason is written in error)
u8 http_tree_path_validate (
	const char *path, char *error, const size_t error_size
) {

	u8 retval = 1;

	if (path) {
		// only the root itself can be a single '/'
		const char *relative = (*path == HTTP_TREE_PATH_SEPARATOR) ? path + 1 : path;
		retval = (*relative == HTTP_TREE_PATH_SEPARATOR) ?
			(u8) (snprintf (error, error_size, "a path can not start with \"//\"") > 0) :
			http_tree_path_segments (path, NULL, NULL, error, error_size);
	}

	else {
		(void) snprintf (error, error_size, "a path is required");
	}

	return retval;

}

#pragma endregion

#pragma region group

static HttpGroup *http_group_new (void) {

	HttpGroup *group = (HttpGroup *) malloc (sizeof (HttpGroup));
	if (group) {
		group->prefix = NULL;

		group->parent = NULL;
		group->tree = NULL;

		http_route_settings_init (&group->settings);

		group->groups = dlist_init (http_group_delete, NULL);
		group->endpoints = dlist_init (http_endpoint_delete, NULL);
	}

	return group;

}

void http_group_delete (void *group_ptr) {

	if (group_ptr) {
		HttpGroup *group = (HttpGroup *) group_ptr;

		str_delete (group->prefix);

		dlist_delete (group->groups);
		dlist_delete (group->endpoints);

		http_route_settings_end (&group->settings);

		free (group);
	}

}

// validates the prefix and creates the group under parent_prefix
static HttpGroup *http_group_create_internal (
	HttpTree *tree, HttpGroup *parent, const char *parent_prefix, const char *prefix
) {

	HttpGroup *group = NULL;

	// a group without prefix is the same path as its parent
	const char *relative = prefix ? prefix : http_tree_empty_path;

	char error[HTTP_TREE_ERROR_SIZE] = { 0 };
	if (http_tree_path_validate (relative, error, HTTP_TREE_ERROR_SIZE)) {
		http_tree_error (tree, "group \"%s\" - %s", relative, error);
	}

	else {
		group = http_group_new ();
		if (group) {
			group->prefix = http_tree_path_join (parent_prefix, relative);
			group->parent = parent;
			group->tree = tree;
		}
	}

	return group;

}

// creates a top level group in the cerver, the prefix can be "" or "/" for the root
HttpGroup *http_cerver_group_create (
	HttpCerver *http_cerver, const char *prefix
) {

	HttpGroup *group = NULL;

	if (http_cerver && http_cerver->tree) {
		group = http_group_create_internal (
			http_cerver->tree, NULL, http_tree_root_path, prefix
		);

		if (group) (void) dlist_insert_at_end_unsafe (http_cerver->tree->groups, group);
	}

	return group;

}

// creates a group inside another one
HttpGroup *http_group_create (
	HttpGroup *parent, const char *prefix
) {

	HttpGroup *group = NULL;

	if (parent) {
		group = http_group_create_internal (
			parent->tree, parent, parent->prefix->str, prefix
		);

		if (group) (void) dlist_insert_at_end_unsafe (parent->groups, group);
	}

	return group;

}

const String *http_group_get_prefix (
	const HttpGroup *group
) {

	return group ? group->prefix : NULL;

}

// adds an endpoint to the group, the path is relative to the group's prefix,
// only a WebSocket endpoint can be without a handler
static HttpEndpoint *http_group_endpoint_add (
	HttpGroup *group,
	const RequestMethod method, const char *path, const HttpHandler handler,
	const bool handler_required
) {

	HttpEndpoint *endpoint = NULL;

	if (group) {
		char error[HTTP_TREE_ERROR_SIZE] = { 0 };

		if (!http_request_method_is_valid (method)) {
			http_tree_error (group->tree, "route \"%s\" - the method %d is not valid", path ? path : "", (int) method);
		}

		else if (handler_required && !handler) {
			http_tree_error (group->tree, "route %s \"%s\" - a handler is required", http_request_method_str (method), path ? path : "");
		}

		else if (http_tree_path_validate (path, error, HTTP_TREE_ERROR_SIZE)) {
			http_tree_error (group->tree, "route %s \"%s\" - %s", http_request_method_str (method), path ? path : "", error);
		}

		else {
			String *complete = http_tree_path_join (group->prefix->str, path);
			if (complete) {
				endpoint = http_endpoint_create (complete->str, method, handler);
				str_delete (complete);
			}

			if (endpoint) {
				endpoint->group = group;
				endpoint->owns_stats = true;
				endpoint->stats = http_route_stats_new ();
				endpoint->file_stats = http_route_file_stats_create ();

				(void) dlist_insert_at_end_unsafe (group->endpoints, endpoint);
			}
		}
	}

	return endpoint;

}

// adds an endpoint to the group, the path is relative to the group's prefix
HttpEndpoint *http_group_route_add (
	HttpGroup *group,
	const RequestMethod method, const char *path, const HttpHandler handler
) {

	return http_group_endpoint_add (group, method, path, handler, true);

}

HttpEndpoint *http_group_root_add (
	HttpGroup *group,
	const RequestMethod method, const HttpHandler handler
) {

	return http_group_route_add (group, method, http_tree_empty_path, handler);

}

HttpEndpoint *http_group_get (
	HttpGroup *group, const char *path, const HttpHandler handler
) {

	return http_group_route_add (group, REQUEST_METHOD_GET, path, handler);

}

HttpEndpoint *http_group_post (
	HttpGroup *group, const char *path, const HttpHandler handler
) {

	return http_group_route_add (group, REQUEST_METHOD_POST, path, handler);

}

HttpEndpoint *http_group_put (
	HttpGroup *group, const char *path, const HttpHandler handler
) {

	return http_group_route_add (group, REQUEST_METHOD_PUT, path, handler);

}

HttpEndpoint *http_group_patch (
	HttpGroup *group, const char *path, const HttpHandler handler
) {

	return http_group_route_add (group, REQUEST_METHOD_PATCH, path, handler);

}

HttpEndpoint *http_group_delete_route (
	HttpGroup *group, const char *path, const HttpHandler handler
) {

	return http_group_route_add (group, REQUEST_METHOD_DELETE, path, handler);

}

void http_group_set_modifier (
	HttpGroup *group, const HttpRouteModifier modifier
) {

	if (group) {
		group->settings.modifier = modifier;
		group->settings.set |= HTTP_ROUTE_SETTING_MODIFIER;
	}

}

void http_group_set_auth (
	HttpGroup *group, const HttpRouteAuthType auth_type
) {

	if (group) {
		group->settings.auth_type = auth_type;
		group->settings.set |= HTTP_ROUTE_SETTING_AUTH_TYPE;
	}

}

void http_group_set_decode_data (
	HttpGroup *group,
	void *(*decode_data)(void *), void (*delete_decoded_data)(void *)
) {

	if (group) {
		group->settings.decode_data = decode_data;
		group->settings.delete_decoded_data = delete_decoded_data;
		group->settings.set |= HTTP_ROUTE_SETTING_DECODE_DATA;
	}

}

void http_group_set_decode_data_into_json (
	HttpGroup *group
) {

	http_group_set_decode_data (group, http_decode_data_into_json, free);

}

void http_group_set_authentication_handler (
	HttpGroup *group, const HttpAuthenticationHandler authentication_handler
) {

	if (group) {
		group->settings.authentication_handler = authentication_handler;
		group->settings.set |= HTTP_ROUTE_SETTING_AUTHENTICATION_HANDLER;
	}

}

void http_group_set_authentication_with_body (
	HttpGroup *group, const bool authentication_with_body
) {

	if (group) {
		group->settings.authentication_with_body = authentication_with_body;
		group->settings.set |= HTTP_ROUTE_SETTING_AUTHENTICATION_WITH_BODY;
	}

}

// the group owns the data, it is deleted with delete_custom_data
void http_group_set_custom_data (
	HttpGroup *group, void *custom_data, void (*delete_custom_data)(void *)
) {

	if (group) {
		group->settings.custom_data = custom_data;
		group->settings.delete_custom_data = delete_custom_data;
		group->settings.set |= HTTP_ROUTE_SETTING_CUSTOM_DATA;
	}

}

void http_group_set_max_body_size (
	HttpGroup *group, const size_t max_body_size
) {

	if (group) {
		group->settings.max_body_size = max_body_size;
		group->settings.set |= HTTP_ROUTE_SETTING_MAX_BODY_SIZE;
	}

}

void http_group_set_cors (
	HttpGroup *group, const bool cors
) {

	if (group) {
		group->settings.cors = cors;
		group->settings.set |= HTTP_ROUTE_SETTING_CORS;
	}

}

void http_group_set_etag (
	HttpGroup *group, const bool etag
) {

	if (group) {
		group->settings.etag = etag;
		group->settings.set |= HTTP_ROUTE_SETTING_ETAG;
	}

}

void http_group_set_compression (
	HttpGroup *group, const bool compression
) {

	if (group) {
		group->settings.compression = compression;
		group->settings.set |= HTTP_ROUTE_SETTING_COMPRESSION;
	}

}

u8 http_group_add_before (
	HttpGroup *group, const HttpBeforeMiddleware middleware
) {

	return group ? http_middlewares_add_before (&group->settings.middlewares, middleware) : 1;

}

u8 http_group_add_after (
	HttpGroup *group, const HttpAfterMiddleware middleware
) {

	return group ? http_middlewares_add_after (&group->settings.middlewares, middleware) : 1;

}

HttpEndpoint *http_group_web_socket_add (
	HttpGroup *group, const char *path, const HttpWebSocketHandlers *handlers
) {

	HttpEndpoint *endpoint = http_group_endpoint_add (group, REQUEST_METHOD_GET, path, NULL, false);

	// without handlers it inherits the ones of its groups
	http_endpoint_set_modifier (endpoint, HTTP_ROUTE_MODIFIER_WEB_SOCKET);
	if (endpoint && handlers) http_web_socket_settings_set_handlers (&endpoint->settings.web_socket, handlers);

	return endpoint;

}

void http_group_set_web_socket (
	HttpGroup *group, const HttpWebSocketHandlers *handlers
) {

	if (group && handlers) http_web_socket_settings_set_handlers (&group->settings.web_socket, handlers);

}

void http_group_set_web_socket_max_message (
	HttpGroup *group, const size_t max_message
) {

	if (group) http_web_socket_settings_set_max_message (&group->settings.web_socket, max_message);

}

u8 http_group_set_web_socket_protocols (
	HttpGroup *group, const char * const *protocols, const unsigned int n_protocols
) {

	return group ?
		http_web_socket_settings_set_protocols (&group->settings.web_socket, protocols, n_protocols) : 1;

}

void http_group_set_web_socket_compression (
	HttpGroup *group, const bool compression
) {

	HttpWebSocketDeflateOptions options;
	http_web_socket_deflate_options_init (&options);
	options.enabled = compression;

	http_group_set_web_socket_compression_options (group, &options);

}

void http_group_set_web_socket_compression_options (
	HttpGroup *group, const HttpWebSocketDeflateOptions *options
) {

	if (group) http_web_socket_settings_set_compression (&group->settings.web_socket, options);

}

void http_group_set_web_socket_origin_check (
	HttpGroup *group, const bool origin_check
) {

	if (group) http_web_socket_settings_set_origin_check (&group->settings.web_socket, origin_check);

}

void http_group_set_web_socket_ping_milliseconds (
	HttpGroup *group, const unsigned int ping_interval, const unsigned int pong_timeout
) {

	if (group) http_web_socket_settings_set_ping_milliseconds (&group->settings.web_socket, ping_interval, pong_timeout);

}

void http_group_set_web_socket_close_timeout_milliseconds (
	HttpGroup *group, const unsigned int close_timeout
) {

	if (group) http_web_socket_settings_set_close_timeout_milliseconds (&group->settings.web_socket, close_timeout);

}

void http_group_set_web_socket_send_timeout_milliseconds (
	HttpGroup *group, const unsigned int send_timeout
) {

	if (group) http_web_socket_settings_set_send_timeout_milliseconds (&group->settings.web_socket, send_timeout);

}

#pragma endregion

#pragma region nodes

static HttpRouteNode *http_route_node_new (const HttpRouteNodeType type) {

	HttpRouteNode *node = (HttpRouteNode *) malloc (sizeof (HttpRouteNode));
	if (node) {
		(void) memset (node, 0, sizeof (HttpRouteNode));
		node->type = type;
	}

	return node;

}

static void http_route_node_delete (HttpRouteNode *node) {

	if (node) {
		for (size_t idx = 0; idx < node->n_statics; idx++) {
			http_route_node_delete (node->statics[idx]);
		}

		for (size_t idx = 0; idx < node->n_params; idx++) {
			http_route_node_delete (node->params[idx]);
		}

		http_route_node_delete (node->wildcard);

		free (node->statics);
		free (node->params);

		str_delete (node->segment);

		free (node);
	}

}

// adds a child to an array of children
// returns the child, NULL on error
static HttpRouteNode *http_route_node_children_add (
	HttpRouteNode ***children, size_t *n_children, HttpRouteNode *child
) {

	HttpRouteNode **added = (HttpRouteNode **) realloc (
		*children, (*n_children + 1) * sizeof (HttpRouteNode *)
	);

	if (added && child) {
		added[*n_children] = child;
		*children = added;
		*n_children += 1;
	}

	else {
		if (added) *children = added;
		http_route_node_delete (child);
		child = NULL;
	}

	return child;

}

static bool http_route_node_segment_equals (
	const String *segment, const char *text, const size_t text_len
) {

	return segment && (segment->len == text_len) && !strncmp (segment->str, text, text_len);

}

static HttpRouteNode *http_route_node_static_get (
	HttpRouteNode *node, const char *text, const size_t text_len
) {

	for (size_t idx = 0; idx < node->n_statics; idx++) {
		if (http_route_node_segment_equals (node->statics[idx]->segment, text, text_len)) {
			return node->statics[idx];
		}
	}

	HttpRouteNode *child = http_route_node_new (HTTP_ROUTE_NODE_TYPE_STATIC);
	if (child) child->segment = str_create_from_bytes (text, text_len);

	return http_route_node_children_add (&node->statics, &node->n_statics, child);

}

static HttpRouteNode *http_route_node_param_get (
	HttpRouteNode *node, const HttpParamValidator *validator
) {

	for (size_t idx = 0; idx < node->n_params; idx++) {
		if (node->params[idx]->validator == validator) {
			return node->params[idx];
		}
	}

	HttpRouteNode *child = http_route_node_new (HTTP_ROUTE_NODE_TYPE_PARAM);
	if (child) {
		child->validator = validator;
		if (validator) child->segment = str_new (validator->name);
	}

	return http_route_node_children_add (&node->params, &node->n_params, child);

}

static HttpRouteNode *http_route_node_wildcard_get (HttpRouteNode *node) {

	if (!node->wildcard) {
		node->wildcard = http_route_node_new (HTTP_ROUTE_NODE_TYPE_WILDCARD);
	}

	return node->wildcard;

}

static int http_route_node_static_comparator (const void *a, const void *b) {

	const HttpRouteNode *first = *(const HttpRouteNode **) a;
	const HttpRouteNode *second = *(const HttpRouteNode **) b;

	return strcmp (first->segment->str, second->segment->str);

}

// the params with a validator are tried before the one without it
static int http_route_node_param_comparator (const void *a, const void *b) {

	const HttpRouteNode *first = *(const HttpRouteNode **) a;
	const HttpRouteNode *second = *(const HttpRouteNode **) b;

	return (first->validator ? 0 : 1) - (second->validator ? 0 : 1);

}

// a marker for the methods that have an endpoint in the Allow header
static void http_route_node_allow_marker (
	const struct _HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

// the methods of the node with HEAD (when it has GET) and OPTIONS
static void http_route_node_allow_build (HttpRouteNode *node) {

	HttpHandler methods[REQUEST_METHOD_SIZE] = { 0 };

	for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
		if (node->endpoints[method]) {
			methods[method] = http_route_node_allow_marker;
			if (node->endpoints[method]->resolved.cors) node->cors = true;
		}
	}

	if (methods[REQUEST_METHOD_GET]) methods[REQUEST_METHOD_HEAD] = http_route_node_allow_marker;
	methods[REQUEST_METHOD_OPTIONS] = http_route_node_allow_marker;

	node->allow_len = http_route_methods_string (methods, node->allow, HTTP_TREE_ALLOW_SIZE);

}

// sorts the children of each node and builds their Allow headers
static void http_route_node_finish (HttpRouteNode *node) {

	if (node) {
		if (node->n_statics > 1) {
			qsort (node->statics, node->n_statics, sizeof (HttpRouteNode *), http_route_node_static_comparator);
		}

		if (node->n_params > 1) {
			qsort (node->params, node->n_params, sizeof (HttpRouteNode *), http_route_node_param_comparator);
		}

		if (node->n_endpoints) http_route_node_allow_build (node);

		for (size_t idx = 0; idx < node->n_statics; idx++) http_route_node_finish (node->statics[idx]);
		for (size_t idx = 0; idx < node->n_params; idx++) http_route_node_finish (node->params[idx]);
		http_route_node_finish (node->wildcard);
	}

}

#pragma endregion

#pragma region tree

HttpTree *http_tree_create (HttpCerver *http_cerver) {

	HttpTree *tree = (HttpTree *) malloc (sizeof (HttpTree));
	if (tree) {
		tree->http_cerver = http_cerver;

		tree->groups = dlist_init (http_group_delete, NULL);
		tree->compatibility_endpoints = dlist_init (http_endpoint_delete, NULL);
		tree->validators = dlist_init (http_param_validator_delete, NULL);

		tree->root = NULL;
		tree->compiled = false;
		tree->n_endpoints = 0;

		tree->max_params = HTTP_TREE_DEFAULT_MAX_PARAMS;
		tree->trailing_slash = HTTP_TREE_DEFAULT_TRAILING_SLASH;

		tree->method_not_allowed = false;
		tree->method_not_allowed_handler = NULL;

		http_middlewares_init (&tree->middlewares);

		tree->n_errors = 0;
		(void) memset (tree->error, 0, HTTP_TREE_ERROR_SIZE);
	}

	return tree;

}

void http_tree_delete (void *tree_ptr) {

	if (tree_ptr) {
		HttpTree *tree = (HttpTree *) tree_ptr;

		http_route_node_delete (tree->root);

		dlist_delete (tree->groups);
		dlist_delete (tree->compatibility_endpoints);
		dlist_delete (tree->validators);

		http_middlewares_end (&tree->middlewares);

		free (tree);
	}

}

// keeps the error of a registration, the first one is the one that is reported
void http_tree_error (
	HttpTree *tree, const char *format, ...
) {

	char message[HTTP_TREE_ERROR_SIZE] = { 0 };

	va_list args;
	va_start (args, format);
	(void) vsnprintf (message, HTTP_TREE_ERROR_SIZE, format, args);
	va_end (args);

	cerver_log_error ("[TREE] %s", message);

	if (tree) {
		if (!tree->n_errors) {
			(void) snprintf (tree->error, HTTP_TREE_ERROR_SIZE, "%s", message);
		}

		tree->n_errors += 1;
	}

}

// a warning of the first routes API, it is logged but it is not an error
void http_tree_warning (
	const char *format, ...
) {

	char message[HTTP_TREE_ERROR_SIZE] = { 0 };

	va_list args;
	va_start (args, format);
	(void) vsnprintf (message, HTTP_TREE_ERROR_SIZE, format, args);
	va_end (args);

	cerver_log_warning ("[TREE] %s", message);

}

static const HttpParamValidator *http_tree_validator_get (
	const HttpTree *tree, const char *name, const size_t name_len
) {

	const HttpParamValidator *validator = NULL;

	for (ListElement *le = dlist_start (tree->validators); le; le = le->next) {
		const HttpParamValidator *custom = (const HttpParamValidator *) le->data;
		if ((strlen (custom->name) == name_len) && !strncmp (custom->name, name, name_len)) {
			validator = custom;
			break;
		}
	}

	return validator ? validator : http_param_validator_builtin (name, name_len);

}

// the state of an endpoint while it is inserted in the tree
typedef struct HttpTreeInsert {

	HttpTree *tree;
	HttpEndpoint *endpoint;
	bool compatibility;

	HttpRouteNode *node;

	unsigned int n_params;
	char *names[HTTP_TREE_DEFAULT_MAX_PARAMS + 1];
	unsigned int n_names;
	char **extra_names;

	char error[HTTP_TREE_ERROR_SIZE];

} HttpTreeInsert;

// keeps the name of a param, NULL for a wildcard without name
static u8 http_tree_insert_name (
	HttpTreeInsert *insert, const char *name, const size_t name_len
) {

	char **names = (char **) realloc (
		insert->extra_names, (insert->n_names + 1) * sizeof (char *)
	);

	if (!names) return 1;

	insert->extra_names = names;
	insert->extra_names[insert->n_names] = name_len ? strndup (name, name_len) : NULL;
	insert->n_names += 1;

	return 0;

}

static u8 http_tree_insert_param (
	HttpTreeInsert *insert, const HttpTreeSegment *segment
) {

	const HttpParamValidator *validator = NULL;
	if (segment->validator) {
		validator = http_tree_validator_get (insert->tree, segment->validator, segment->validator_len);
		if (!validator) {
			(void) snprintf (
				insert->error, HTTP_TREE_ERROR_SIZE, "the validator \"%.*s\" is not registered",
				(int) segment->validator_len, segment->validator
			);

			return 1;
		}
	}

	insert->node = http_route_node_param_get (insert->node, validator);
	insert->n_params += 1;

	return http_tree_insert_name (insert, segment->name, segment->name_len);

}

static u8 http_tree_insert_segment (const HttpTreeSegment *segment, void *data) {

	HttpTreeInsert *insert = (HttpTreeInsert *) data;

	u8 retval = 0;

	switch (segment->type) {
		case HTTP_ROUTE_NODE_TYPE_STATIC:
			insert->node = http_route_node_static_get (insert->node, segment->text, segment->text_len);
			break;

		case HTTP_ROUTE_NODE_TYPE_PARAM:
			retval = http_tree_insert_param (insert, segment);
			break;

		case HTTP_ROUTE_NODE_TYPE_WILDCARD:
			insert->node = http_route_node_wildcard_get (insert->node);
			insert->n_params += 1;
			retval = http_tree_insert_name (insert, segment->name, segment->name_len);
			break;

		default:
			retval = 1;
			break;
	}

	if (!retval && !insert->node) {
		(void) snprintf (insert->error, HTTP_TREE_ERROR_SIZE, "failed to allocate a node of the tree");
		retval = 1;
	}

	return retval;

}

static void http_tree_insert_fail (HttpTreeInsert *insert) {

	const HttpEndpoint *endpoint = insert->endpoint;

	if (insert->compatibility) {
		http_tree_warning (
			"route %s %s - %s - it is not registered",
			http_request_method_str (endpoint->method), endpoint->path->str, insert->error
		);
	}

	else {
		http_tree_error (
			insert->tree, "route %s %s - %s",
			http_request_method_str (endpoint->method), endpoint->path->str, insert->error
		);
	}

	for (unsigned int idx = 0; idx < insert->n_names; idx++) free (insert->extra_names[idx]);
	free (insert->extra_names);

}

// adds a resolved endpoint to the routes tree
// returns 0 on success, 1 on error
u8 http_tree_insert (
	HttpTree *tree, HttpEndpoint *endpoint, const bool compatibility
) {

	HttpTreeInsert insert;
	(void) memset (&insert, 0, sizeof (HttpTreeInsert));
	insert.tree = tree;
	insert.endpoint = endpoint;
	insert.compatibility = compatibility;

	if (!tree->root) tree->root = http_route_node_new (HTTP_ROUTE_NODE_TYPE_STATIC);
	insert.node = tree->root;

	u8 retval = insert.node ? http_tree_path_segments (
		endpoint->path->str, http_tree_insert_segment, &insert,
		insert.error, HTTP_TREE_ERROR_SIZE
	) : 1;

	if (!retval && (insert.n_params > tree->max_params)) {
		(void) snprintf (
			insert.error, HTTP_TREE_ERROR_SIZE, "it has %u params, the max is %u",
			insert.n_params, tree->max_params
		);

		retval = 1;
	}

	if (!retval && insert.node->endpoints[endpoint->method]) {
		(void) snprintf (
			insert.error, HTTP_TREE_ERROR_SIZE, "it is already registered as %s",
			insert.node->endpoints[endpoint->method]->path->str
		);

		retval = 1;
	}

	if (retval) {
		http_tree_insert_fail (&insert);
	}

	else {
		insert.node->endpoints[endpoint->method] = endpoint;
		insert.node->n_endpoints += 1;

		http_endpoint_params_names_delete (endpoint);
		endpoint->n_params = insert.n_names;
		endpoint->params_names = insert.extra_names;

		tree->n_endpoints += 1;
	}

	return retval;

}

// the middlewares of the groups, from the outermost one
static u8 http_tree_resolve_groups_middlewares (
	HttpMiddlewares *middlewares, const HttpGroup *group
) {

	u8 errors = 0;

	if (group) {
		errors |= http_tree_resolve_groups_middlewares (middlewares, group->parent);
		errors |= http_middlewares_append (middlewares, &group->settings.middlewares);
	}

	return errors;

}

// a WebSocket endpoint needs its handlers, set in it or in one of its groups, and it is a GET
static u8 http_tree_resolve_web_socket (
	HttpTree *tree, const HttpEndpoint *endpoint
) {

	u8 errors = 0;

	if (endpoint->resolved.modifier == HTTP_ROUTE_MODIFIER_WEB_SOCKET) {
		if (!(endpoint->resolved.web_socket.set & HTTP_WEB_SOCKET_SETTING_HANDLERS)) {
			http_tree_error (
				tree, "websocket \"%s\" - it needs its handlers (http_endpoint_set_web_socket ())",
				endpoint->path->str
			);

			errors = 1;
		}

		else if (endpoint->method != REQUEST_METHOD_GET) {
			http_tree_error (
				tree, "websocket %s \"%s\" - a WebSocket endpoint is a GET",
				http_request_method_str (endpoint->method), endpoint->path->str
			);

			errors = 1;
		}
	}

	return errors;

}

// resolves the settings that the endpoint inherits from its groups
static u8 http_tree_resolve_endpoint (
	HttpTree *tree, HttpEndpoint *endpoint
) {

	http_middlewares_end (&endpoint->resolved.middlewares);

	HttpMiddlewares middlewares = endpoint->resolved.middlewares;
	endpoint->resolved = endpoint->settings;

	// the resolved settings never own the data
	endpoint->resolved.middlewares = middlewares;
	endpoint->resolved.delete_custom_data = NULL;

	for (const HttpGroup *group = endpoint->group; group; group = group->parent) {
		http_route_settings_inherit (&endpoint->resolved, &group->settings);
	}

	u8 errors = http_tree_resolve_web_socket (tree, endpoint);

	errors |= http_middlewares_append (&endpoint->resolved.middlewares, &tree->middlewares);
	errors |= http_tree_resolve_groups_middlewares (&endpoint->resolved.middlewares, endpoint->group);
	errors |= http_middlewares_append (&endpoint->resolved.middlewares, &endpoint->settings.middlewares);

	return errors;

}

static u8 http_tree_compile_group (HttpTree *tree, HttpGroup *group) {

	u8 errors = 0;

	for (ListElement *le = dlist_start (group->endpoints); le; le = le->next) {
		HttpEndpoint *endpoint = (HttpEndpoint *) le->data;

		errors |= http_tree_resolve_endpoint (tree, endpoint);
		errors |= http_tree_insert (tree, endpoint, false);
	}

	for (ListElement *le = dlist_start (group->groups); le; le = le->next) {
		errors |= http_tree_compile_group (tree, (HttpGroup *) le->data);
	}

	return errors;

}

// builds the routes tree with the groups and the routes of the first API
// returns 0 on success, 1 on error (the cerver does not start)
u8 http_tree_compile (HttpTree *tree) {

	u8 errors = 0;

	if (tree) {
		http_route_node_delete (tree->root);
		tree->root = http_route_node_new (HTTP_ROUTE_NODE_TYPE_STATIC);
		tree->n_endpoints = 0;

		for (ListElement *le = dlist_start (tree->groups); le; le = le->next) {
			errors |= http_tree_compile_group (tree, (HttpGroup *) le->data);
		}

		(void) http_compatibility_translate (tree, tree->http_cerver->routes);

		http_route_node_finish (tree->root);

		tree->compiled = true;

		// an error during the registration also stops the cerver
		if (tree->n_errors) {
			cerver_log_error (
				"[TREE] %zu routes errors, the first one: %s",
				tree->n_errors, tree->error
			);

			errors = 1;
		}
	}

	return errors;

}

#pragma endregion

#pragma region match

static int http_tree_segment_compare (
	const String *segment, const char *text, const size_t text_len
) {

	const size_t length = (segment->len < text_len) ? segment->len : text_len;

	int result = strncmp (segment->str, text, length);
	if (!result) result = (segment->len < text_len) ? -1 : ((segment->len > text_len) ? 1 : 0);

	return result;

}

static const HttpRouteNode *http_tree_static_find (
	const HttpRouteNode *node, const char *text, const size_t text_len
) {

	const HttpRouteNode *found = NULL;

	size_t low = 0;
	size_t high = node->n_statics;
	while (low < high) {
		const size_t middle = low + ((high - low) / 2);
		const int result = http_tree_segment_compare (node->statics[middle]->segment, text, text_len);

		if (!result) {
			found = node->statics[middle];
			break;
		}

		if (result < 0) low = middle + 1;
		else high = middle;
	}

	return found;

}

// the state of a match, the params are written in order and removed when backtracking
typedef struct HttpTreeMatch {

	HttpRequestParam *params;
	unsigned int n_params;
	unsigned int max_params;

} HttpTreeMatch;

static const HttpRouteNode *http_tree_match_node (
	const HttpRouteNode *node, const char *rest, const size_t rest_len,
	HttpTreeMatch *match
);

// the node matched the segment, the rest of the path continues in its children
static const HttpRouteNode *http_tree_match_next (
	const HttpRouteNode *child, const char *next, const size_t next_len,
	const bool last, HttpTreeMatch *match
) {

	const HttpRouteNode *found = NULL;

	if (last) {
		if (child->n_endpoints) found = child;
	}

	// a '/' followed by nothing is an empty segment
	else if (next_len) {
		found = http_tree_match_node (child, next, next_len, match);
	}

	return found;

}

static bool http_tree_match_param_push (
	HttpTreeMatch *match, const char *value, const size_t length
) {

	bool pushed = false;

	if (match->n_params < match->max_params) {
		match->params[match->n_params].value = value;
		match->params[match->n_params].len = length;
		match->params[match->n_params].decoded = NULL;
		match->n_params += 1;

		pushed = true;
	}

	return pushed;

}

static const HttpRouteNode *http_tree_match_params (
	const HttpRouteNode *node,
	const char *segment, const size_t segment_len,
	const char *next, const size_t next_len, const bool last,
	HttpTreeMatch *match
) {

	const HttpRouteNode *found = NULL;

	for (size_t idx = 0; !found && (idx < node->n_params); idx++) {
		const HttpRouteNode *param = node->params[idx];
		if (param->validator && !param->validator->validate (segment, segment_len)) continue;

		if (http_tree_match_param_push (match, segment, segment_len)) {
			found = http_tree_match_next (param, next, next_len, last, match);
			if (!found) match->n_params -= 1;
		}
	}

	return found;

}

static const HttpRouteNode *http_tree_match_wildcard (
	const HttpRouteNode *node, const char *rest, const size_t rest_len,
	HttpTreeMatch *match
) {

	const HttpRouteNode *found = NULL;

	const HttpRouteNode *wildcard = node->wildcard;
	if (wildcard && wildcard->n_endpoints && http_tree_match_param_push (match, rest, rest_len)) {
		found = wildcard;
	}

	return found;

}

// matches the rest of a path (after a '/') with the children of the node:
// the static child first, then the params and at last the wildcard
static const HttpRouteNode *http_tree_match_node (
	const HttpRouteNode *node, const char *rest, const size_t rest_len,
	HttpTreeMatch *match
) {

	const char *end = (const char *) memchr (rest, HTTP_TREE_PATH_SEPARATOR, rest_len);
	const size_t segment_len = end ? (size_t) (end - rest) : rest_len;
	const bool last = !end;

	const char *next = end ? end + 1 : NULL;
	const size_t next_len = end ? rest_len - segment_len - 1 : 0;

	// an empty segment ("//") never matches
	if (!segment_len) return NULL;

	const HttpRouteNode *found = NULL;

	const HttpRouteNode *child = http_tree_static_find (node, rest, segment_len);
	if (child) found = http_tree_match_next (child, next, next_len, last, match);

	if (!found) {
		found = http_tree_match_params (node, rest, segment_len, next, next_len, last, match);
	}

	if (!found) {
		found = http_tree_match_wildcard (node, rest, rest_len, match);
	}

	return found;

}

// matches a path with the tree, the params are written in params (max_params + 1 slots)
// returns true if a node with endpoints matched the path
bool http_tree_match (
	const HttpTree *tree,
	const char *path, const size_t path_len,
	HttpRequestParam *params, HttpRouteMatch *match
) {

	match->node = NULL;
	match->n_params = 0;

	if (tree && tree->root && path && path_len && (*path == HTTP_TREE_PATH_SEPARATOR)) {
		HttpTreeMatch state = { params, 0, tree->max_params };

		// the path "/" is the root itself
		if (path_len == 1) {
			if (tree->root->n_endpoints) match->node = tree->root;
		}

		else {
			match->node = http_tree_match_node (tree->root, path + 1, path_len - 1, &state);
		}

		match->n_params = match->node ? state.n_params : 0;
	}

	return match->node != NULL;

}

#pragma endregion

#pragma region print

// every route is printed like the first routes API did: a block with its path, methods,
// modifier and auth, and the closest routes below it with an endpoint as its children,
// the path segments without an endpoint add no level

static size_t http_tree_print_count_children (const HttpRouteNode *node);

// a child without an endpoint is replaced by the closest routes below it that have one
static size_t http_tree_print_count_node (const HttpRouteNode *node) {

	return node->n_endpoints ? 1 : http_tree_print_count_children (node);

}

static size_t http_tree_print_count_children (const HttpRouteNode *node) {

	size_t count = 0;

	for (size_t idx = 0; idx < node->n_statics; idx++) {
		count += http_tree_print_count_node (node->statics[idx]);
	}

	for (size_t idx = 0; idx < node->n_params; idx++) {
		count += http_tree_print_count_node (node->params[idx]);
	}

	if (node->wildcard) count += http_tree_print_count_node (node->wildcard);

	return count;

}

static const HttpEndpoint *http_tree_print_first_endpoint (const HttpRouteNode *node) {

	const HttpEndpoint *first = NULL;
	for (unsigned int method = 0; (method < REQUEST_METHOD_SIZE) && !first; method++) {
		first = node->endpoints[method];
	}

	return first;

}

static void http_tree_print_append (
	char *values, size_t *length, const char *value
) {

	const int written = snprintf (
		values + *length, HTTP_TREE_PRINT_VALUES_SIZE - *length, "%s%s",
		*length ? http_tree_values_separator : http_tree_empty_path, value
	);

	if (written > 0) {
		*length += (size_t) written;
		if (*length >= HTTP_TREE_PRINT_VALUES_SIZE) *length = HTTP_TREE_PRINT_VALUES_SIZE - 1;
	}

}

static void http_tree_print_methods (const HttpRouteNode *node, const char *indent) {

	char methods[HTTP_TREE_PRINT_VALUES_SIZE] = { 0 };
	size_t length = 0;

	for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
		if (node->endpoints[method]) {
			http_tree_print_append (methods, &length, http_request_method_str (node->endpoints[method]->method));
		}
	}

	cerver_log_msg ("%sMethods: %s", indent, methods);

}

// the auth type or the modifier of the endpoints of a route,
// one value when every method agrees, or "METHOD value" for each one
static void http_tree_print_value (
	const HttpRouteNode *node, const char *indent, const char *name,
	const char *(*value)(const HttpEndpoint *endpoint)
) {

	const char *first = value (http_tree_print_first_endpoint (node));

	bool same = true;
	for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
		if (node->endpoints[method] && strcmp (value (node->endpoints[method]), first)) same = false;
	}

	char values[HTTP_TREE_PRINT_VALUES_SIZE] = { 0 };
	size_t length = 0;

	for (unsigned int method = 0; (method < REQUEST_METHOD_SIZE) && !same; method++) {
		if (!node->endpoints[method]) continue;

		char method_value[HTTP_TREE_PRINT_VALUES_SIZE] = { 0 };
		(void) snprintf (
			method_value, HTTP_TREE_PRINT_VALUES_SIZE, "%s %s",
			http_request_method_str (node->endpoints[method]->method), value (node->endpoints[method])
		);

		http_tree_print_append (values, &length, method_value);
	}

	cerver_log_msg ("%s%s: %s", indent, name, same ? first : values);

}

static const char *http_tree_print_modifier (const HttpEndpoint *endpoint) {

	return http_route_modifier_to_string (endpoint->resolved.modifier);

}

static const char *http_tree_print_auth (const HttpEndpoint *endpoint) {

	return http_route_auth_type_to_string (endpoint->resolved.auth_type);

}

static void http_tree_print_node (
	const HttpRouteNode *node, char *indent, const size_t depth
);

// the children of a route are printed two levels deeper, below its "Children" line
static void http_tree_print_children (
	const HttpRouteNode *node, char *indent, const size_t depth
) {

	for (size_t idx = 0; idx < node->n_statics; idx++) {
		http_tree_print_node (node->statics[idx], indent, depth);
	}

	for (size_t idx = 0; idx < node->n_params; idx++) {
		http_tree_print_node (node->params[idx], indent, depth);
	}

	if (node->wildcard) http_tree_print_node (node->wildcard, indent, depth);

}

// the indentation of a level deeper than depth, capped at HTTP_TREE_PRINT_INDENT_SIZE
static size_t http_tree_print_deeper (char *indent, const size_t depth) {

	const size_t indent_len = strlen (http_tree_indent);
	const bool deeper = (depth + indent_len) < HTTP_TREE_PRINT_INDENT_SIZE;
	if (deeper) (void) memcpy (indent + depth, http_tree_indent, indent_len + 1);

	return deeper ? depth + indent_len : depth;

}

static void http_tree_print_route (
	const HttpRouteNode *node, char *indent, const size_t depth
) {

	cerver_log_msg ("%sRoute: %s", indent, http_tree_print_first_endpoint (node)->path->str);

	const size_t values_depth = http_tree_print_deeper (indent, depth);

	http_tree_print_methods (node, indent);
	http_tree_print_value (node, indent, "Modifier", http_tree_print_modifier);
	http_tree_print_value (node, indent, "Auth", http_tree_print_auth);

	const size_t n_children = http_tree_print_count_children (node);
	if (n_children) {
		cerver_log_msg ("%sChildren (%zu):", indent, n_children);

		http_tree_print_children (node, indent, http_tree_print_deeper (indent, values_depth));
	}

	indent[depth] = '\0';

}

static void http_tree_print_node (
	const HttpRouteNode *node, char *indent, const size_t depth
) {

	if (node->n_endpoints) http_tree_print_route (node, indent, depth);

	else http_tree_print_children (node, indent, depth);

}

// prints the compiled routes tree
void http_tree_print (const HttpTree *tree) {

	if (tree && tree->root) {
		cerver_log_msg ("HTTP routes (%zu endpoints):", tree->n_endpoints);

		char indent[HTTP_TREE_PRINT_INDENT_SIZE + 1] = { 0 };

		http_tree_print_node (tree->root, indent, 0);
	}

}

// the number of endpoints in the compiled tree
size_t http_tree_get_n_endpoints (const HttpTree *tree) {

	return tree ? tree->n_endpoints : 0;

}

#pragma endregion

#pragma region json

// the WebSocket settings of an endpoint: its protocols, compression and limits
static json_t *http_tree_web_socket_json (const HttpEndpoint *endpoint) {

	const HttpWebSocketSettings *settings = &endpoint->resolved.web_socket;

	json_t *web_socket = json_object ();
	if (web_socket) {
		json_t *protocols = json_array ();
		for (unsigned int i = 0; protocols && (i < settings->n_protocols); i++) {
			(void) json_array_append_new (protocols, json_string (settings->protocols[i]));
		}

		(void) json_object_set_new (web_socket, "protocols", protocols);
		(void) json_object_set_new (web_socket, "compression", json_boolean (settings->compression.enabled));
		(void) json_object_set_new (web_socket, "origin_check", json_boolean (settings->origin_check));
		(void) json_object_set_new (web_socket, "max_message", json_integer ((json_int_t) settings->max_message));
		(void) json_object_set_new (web_socket, "ping_interval", json_integer ((json_int_t) settings->ping_interval));
	}

	return web_socket;

}

static json_t *http_tree_endpoint_json (const HttpEndpoint *endpoint) {

	json_t *endpoint_object = json_object ();
	if (endpoint_object) {
		(void) json_object_set_new (
			endpoint_object, "method", json_string (http_request_method_str (endpoint->method))
		);

		(void) json_object_set_new (
			endpoint_object, "path", json_string (endpoint->path->str)
		);

		(void) json_object_set_new (
			endpoint_object, "auth",
			json_string (http_route_auth_type_to_string (endpoint->resolved.auth_type))
		);

		(void) json_object_set_new (
			endpoint_object, "modifier",
			json_string (http_route_modifier_to_string (endpoint->resolved.modifier))
		);

		(void) json_object_set_new (
			endpoint_object, "compatibility", json_boolean (endpoint->route != NULL)
		);

		(void) json_object_set_new (endpoint_object, "cors", json_boolean (endpoint->resolved.cors));
		(void) json_object_set_new (endpoint_object, "etag", json_boolean (endpoint->resolved.etag));
		(void) json_object_set_new (
			endpoint_object, "compression", json_boolean (endpoint->resolved.compression)
		);

		if (endpoint->resolved.modifier == HTTP_ROUTE_MODIFIER_WEB_SOCKET) {
			(void) json_object_set_new (endpoint_object, "websocket", http_tree_web_socket_json (endpoint));
		}
	}

	return endpoint_object;

}

static void http_tree_node_json (
	const HttpRouteNode *node, json_t *endpoints_array
) {

	for (unsigned int method = 0; method < REQUEST_METHOD_SIZE; method++) {
		const HttpEndpoint *endpoint = node->endpoints[method];
		if (endpoint) {
			(void) json_array_append_new (endpoints_array, http_tree_endpoint_json (endpoint));
		}
	}

	for (size_t idx = 0; idx < node->n_statics; idx++) {
		http_tree_node_json (node->statics[idx], endpoints_array);
	}

	for (size_t idx = 0; idx < node->n_params; idx++) {
		http_tree_node_json (node->params[idx], endpoints_array);
	}

	if (node->wildcard) http_tree_node_json (node->wildcard, endpoints_array);

}

// the compiled endpoints as a json string, NULL if the tree is not compiled
char *http_tree_generate_json (const HttpTree *tree) {

	char *json_string = NULL;

	if (tree && tree->root) {
		json_t *json = json_object ();
		if (json) {
			(void) json_object_set_new (
				json, "n_endpoints", json_integer ((json_int_t) tree->n_endpoints)
			);

			json_t *endpoints_array = json_array ();
			http_tree_node_json (tree->root, endpoints_array);

			(void) json_object_set_new (json, "endpoints", endpoints_array);

			json_string = json_dumps (json, 0);

			json_decref (json);
		}
	}

	return json_string;

}

#pragma endregion
