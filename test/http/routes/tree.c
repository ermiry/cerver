#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/collections/dlist.h>

#include <cerver/http/core/admin.h>
#include <cerver/http/core/context.h>
#include "cerver/http/core/handler.h"
#include <cerver/http/core/http.h>
#include <cerver/http/core/routes.h>
#include "cerver/http/requests/internal.h"
#include <cerver/http/requests/request.h>
#include <cerver/http/responses/compression.h>
#include <cerver/http/routes/middleware.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>
#include <cerver/http/routes/validator.h>
#include <cerver/http/websockets/frame.h>
#include <cerver/http/websockets/settings.h>

#include "../../test.h"

#define TEST_TREE_PATH_SIZE					512

// the limits of the uploads and the compression that a group, a subgroup and an endpoint set
#define TEST_TREE_GROUP_MAX_FILE_SIZE				4096
#define TEST_TREE_GROUP_MAX_PARTS					8
#define TEST_TREE_GROUP_COMPRESSION_MIN_SIZE		128
#define TEST_TREE_GROUP_COMPRESSION_LEVEL			3
#define TEST_TREE_SUBGROUP_MAX_PARTS				2
#define TEST_TREE_ENDPOINT_MAX_FILE_SIZE			512
#define TEST_TREE_ENDPOINT_COMPRESSION_MIN_SIZE		16
#define TEST_TREE_ENDPOINT_COMPRESSION_LEVEL		HTTP_COMPRESSION_MAX_LEVEL

// a path of TEST_TREE_PATH_SIZE with its first slash
#define TEST_TREE_URL_SIZE					(TEST_TREE_PATH_SIZE * 2)
#define TEST_TREE_LONG_SEGMENTS				20
#define TEST_TREE_MANY_PARAMS				(HTTP_TREE_DEFAULT_MAX_PARAMS + 1)
#define TEST_TREE_MORE_PARAMS				(HTTP_TREE_DEFAULT_MAX_PARAMS << 1)

#define TEST_TREE_SEQUENCE_SIZE				16

#define TEST_TREE_FUZZ_ROUTES				64
#define TEST_TREE_FUZZ_URLS					(1 << 14)
#define TEST_TREE_FUZZ_SEGMENTS				4
#define TEST_TREE_FUZZ_SEED					0x5eed

static const char *test_tree_fuzz_alphabet[] = { "a", "b", "users", "items", "1", "me" };
static const size_t test_tree_fuzz_alphabet_size = 6;

// the settings of the WebSocket endpoint of the routes json
#define TEST_TREE_PROTOCOLS					2
#define TEST_TREE_MAX_MESSAGE				1024
#define TEST_TREE_PING_INTERVAL				500
#define TEST_TREE_PONG_TIMEOUT				250

static const char * const test_tree_protocols[TEST_TREE_PROTOCOLS] = { "chat", "the.second" };

static const char *test_tree_objectid = { "5f9b1c2d3e4f5a6b7c8d9e0f" };

// the order in which the middlewares ran
static char test_tree_sequence[TEST_TREE_SEQUENCE_SIZE] = { 0 };
static size_t test_tree_sequence_len = 0;

static void test_tree_handler (CerverHttpContext *context) {

	(void) context;

}

static void test_tree_web_socket_message (
	struct _HttpWebSocket *web_socket,
	HttpWebSocketOpcode type, const void *data, size_t size
) {

	(void) web_socket;
	(void) type;
	(void) data;
	(void) size;

}

static unsigned int test_tree_authentication (CerverHttpContext *context) {

	(void) context;

	return 0;

}

static void test_tree_sequence_add (const char step) {

	if (test_tree_sequence_len < (TEST_TREE_SEQUENCE_SIZE - 1)) {
		test_tree_sequence[test_tree_sequence_len] = step;
		test_tree_sequence_len += 1;
		test_tree_sequence[test_tree_sequence_len] = '\0';
	}

}

static void test_tree_sequence_reset (void) {

	(void) memset (test_tree_sequence, 0, TEST_TREE_SEQUENCE_SIZE);
	test_tree_sequence_len = 0;

}

#define TEST_TREE_BEFORE(name, step, result)											\
	static CerverHttpMiddlewareResult name (CerverHttpContext *context) {				\
		(void) context;																	\
		test_tree_sequence_add (step);													\
		return result;																	\
	}

#define TEST_TREE_AFTER(name, step)														\
	static void name (CerverHttpContext *context) {										\
		(void) context;																	\
		test_tree_sequence_add (step);													\
	}

TEST_TREE_BEFORE (test_tree_before_cerver, 'c', HTTP_MIDDLEWARE_RESULT_CONTINUE)
TEST_TREE_BEFORE (test_tree_before_group, 'g', HTTP_MIDDLEWARE_RESULT_CONTINUE)
TEST_TREE_BEFORE (test_tree_before_subgroup, 's', HTTP_MIDDLEWARE_RESULT_CONTINUE)
TEST_TREE_BEFORE (test_tree_before_endpoint, 'e', HTTP_MIDDLEWARE_RESULT_CONTINUE)
TEST_TREE_BEFORE (test_tree_before_stop, 'x', HTTP_MIDDLEWARE_RESULT_STOP)

TEST_TREE_AFTER (test_tree_after_cerver, 'C')
TEST_TREE_AFTER (test_tree_after_group, 'G')
TEST_TREE_AFTER (test_tree_after_endpoint, 'E')

static HttpCerver *test_tree_cerver (void) {

	HttpCerver *http_cerver = cerver_http_create (NULL);
	test_check_ptr (http_cerver);
	test_check_ptr (http_cerver->tree);

	return http_cerver;

}

// returns the endpoint of the method in the node that matched the path, NULL if none
static const HttpEndpoint *test_tree_match (
	const HttpCerver *http_cerver, const CerverHttpMethod method, const char *path,
	HttpRequestParam *params, unsigned int *n_params
) {

	HttpRouteMatch match = { NULL, 0 };

	const bool found = http_tree_match (
		http_cerver->tree, path, strlen (path), params, &match
	);

	if (n_params) *n_params = match.n_params;

	return found ? match.node->endpoints[method] : NULL;

}

static bool test_tree_param_is (
	const HttpRequestParam *param, const char *expected
) {

	return (param->len == strlen (expected)) && !strncmp (param->value, expected, param->len);

}

static void test_tree_expect_path (
	const HttpCerver *http_cerver, const CerverHttpMethod method,
	const char *url, const char *expected_path
) {

	HttpRequestParam params[TEST_TREE_MORE_PARAMS];
	const HttpEndpoint *endpoint = test_tree_match (http_cerver, method, url, params, NULL);

	if (expected_path) {
		test_check_ptr (endpoint);
		test_check_str_eq (endpoint->path->str, expected_path, NULL);
	}

	else {
		test_check_null_ptr (endpoint);
	}

}

// static > param > wildcard, no matter the order of registration
static void test_tree_priority (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");
	test_check_ptr (cerver_http_group_get (api, "users/*rest", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "users/:id", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "users/me", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/users/me", "/api/users/me");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/users/42", "/api/users/:id");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/users/42/x/y", "/api/users/*rest");

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	unsigned int n_params = 0;
	(void) test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/users/42", params, &n_params);
	test_check_unsigned_eq (n_params, 1, NULL);
	test_check_true (test_tree_param_is (&params[0], "42"));

	(void) test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/users/42/x/y", params, &n_params);
	test_check_unsigned_eq (n_params, 1, NULL);
	test_check_true (test_tree_param_is (&params[0], "42/x/y"));

	// another method in the same path is a different endpoint
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/users/me", NULL);

	cerver_http_delete (http_cerver);

}

// a static segment that does not lead to an endpoint goes back to the param
static void test_tree_backtracking (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *root = cerver_http_group_create (http_cerver, "/");
	test_check_ptr (cerver_http_group_get (root, "a/static/view", test_tree_handler));
	test_check_ptr (cerver_http_group_get (root, "a/:id/edit", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/a/static/view", "/a/static/view");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/a/static/edit", "/a/:id/edit");

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	unsigned int n_params = 0;
	(void) test_tree_match (http_cerver, REQUEST_METHOD_GET, "/a/static/edit", params, &n_params);
	test_check_unsigned_eq (n_params, 1, NULL);
	test_check_true (test_tree_param_is (&params[0], "static"));

	cerver_http_delete (http_cerver);

}

// "//", a trailing "/" (the tree is strict, the policy is applied by the receive)
// and a prefix without a segment boundary never match
static void test_tree_boundaries (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");
	test_check_ptr (cerver_http_group_get (api, "users", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "", test_tree_handler));

	HttpGroup *root = cerver_http_group_create (http_cerver, "");
	test_check_ptr (cerver_http_group_get (root, "", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/users", "/api/users");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api", "/api");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/", "/");

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api//users", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "//api/users", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/users/", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/apiv2/users", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/x/api/users", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/user", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/usersx", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "api/users", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "", NULL);

	cerver_http_delete (http_cerver);

}

static void test_tree_build_path (
	char *path, const size_t path_size, const char *segment, const unsigned int n_segments
) {

	size_t written = 0;
	for (unsigned int idx = 0; idx < n_segments; idx++) {
		written += (size_t) snprintf (
			path + written, path_size - written, "%s%s%u", idx ? "/" : "", segment, idx
		);
	}

}

// any number of segments, params in any level and grandchildren
static void test_tree_depth (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	char path[TEST_TREE_PATH_SIZE] = { 0 };
	test_tree_build_path (path, TEST_TREE_PATH_SIZE, "s", TEST_TREE_LONG_SEGMENTS);

	HttpGroup *root = cerver_http_group_create (http_cerver, NULL);
	test_check_ptr (root);
	test_check_ptr (cerver_http_group_get (root, path, test_tree_handler));

	// a top level param
	test_check_ptr (cerver_http_group_get (root, ":tenant/info", test_tree_handler));

	// nested groups with params
	HttpGroup *tenants = cerver_http_group_create (http_cerver, "tenants/:tenant");
	HttpGroup *projects = cerver_http_group_subgroup_add (tenants, "projects/:project");
	test_check_ptr (cerver_http_group_get (projects, "blocks/:block/info", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	char url[TEST_TREE_URL_SIZE] = { 0 };
	(void) snprintf (url, TEST_TREE_URL_SIZE, "/%s", path);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, url, url);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/acme/info", "/:tenant/info");

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	unsigned int n_params = 0;
	const HttpEndpoint *endpoint = test_tree_match (
		http_cerver, REQUEST_METHOD_GET, "/tenants/t1/projects/p2/blocks/b3/info", params, &n_params
	);

	test_check_ptr (endpoint);
	test_check_str_eq (endpoint->path->str, "/tenants/:tenant/projects/:project/blocks/:block/info", NULL);
	test_check_unsigned_eq (n_params, 3, NULL);
	test_check_true (test_tree_param_is (&params[0], "t1"));
	test_check_true (test_tree_param_is (&params[1], "p2"));
	test_check_true (test_tree_param_is (&params[2], "b3"));
	test_check_unsigned_eq (endpoint->n_params, 3, NULL);
	test_check_str_eq (endpoint->params_names[0], "tenant", NULL);
	test_check_str_eq (endpoint->params_names[1], "project", NULL);
	test_check_str_eq (endpoint->params_names[2], "block", NULL);

	cerver_http_delete (http_cerver);

}

static void test_tree_build_params_path (
	char *path, const size_t path_size, const unsigned int n_params
) {

	size_t written = 0;
	for (unsigned int idx = 0; idx < n_params; idx++) {
		written += (size_t) snprintf (
			path + written, path_size - written, "%s:p%u", idx ? "/" : "", idx
		);
	}

}

// a route with more params than the max is an error, unless the max is raised
static void test_tree_max_params (void) {

	char path[TEST_TREE_PATH_SIZE] = { 0 };
	test_tree_build_params_path (path, TEST_TREE_PATH_SIZE, TEST_TREE_MANY_PARAMS);

	HttpCerver *http_cerver = test_tree_cerver ();
	HttpGroup *root = cerver_http_group_create (http_cerver, "/");
	test_check_ptr (cerver_http_group_get (root, path, test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 1, NULL);
	test_check_unsigned_eq (cerver_http_routes_errors_count (http_cerver), 1, NULL);
	test_check_ptr (strstr (cerver_http_routes_error (http_cerver), "params"));

	cerver_http_delete (http_cerver);

	http_cerver = test_tree_cerver ();
	cerver_http_set_max_route_params (http_cerver, TEST_TREE_MORE_PARAMS);

	root = cerver_http_group_create (http_cerver, "/");
	test_check_ptr (cerver_http_group_get (root, path, test_tree_handler));
	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	char url[TEST_TREE_PATH_SIZE] = { 0 };
	size_t written = 0;
	for (unsigned int idx = 0; idx < TEST_TREE_MANY_PARAMS; idx++) {
		written += (size_t) snprintf (url + written, TEST_TREE_PATH_SIZE - written, "/v%u", idx);
	}

	HttpRequestParam params[TEST_TREE_MORE_PARAMS];
	unsigned int n_params = 0;
	test_check_ptr (test_tree_match (http_cerver, REQUEST_METHOD_GET, url, params, &n_params));
	test_check_unsigned_eq (n_params, TEST_TREE_MANY_PARAMS, NULL);
	test_check_true (test_tree_param_is (&params[TEST_TREE_MANY_PARAMS - 1], "v16"));

	cerver_http_delete (http_cerver);

}

static bool test_tree_validate_even (const char *value, const size_t length) {

	return length && (((value[length - 1] - '0') % 2) == 0);

}

// a param with a validator only matches the values that it accepts
static void test_tree_validators (void) {

	HttpCerver *http_cerver = test_tree_cerver ();
	test_check_unsigned_eq (cerver_http_register_param_validator (http_cerver, "even", test_tree_validate_even), 0, NULL);

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");
	test_check_ptr (cerver_http_group_get (api, "items/:name", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "items/:id<objectid>", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "numbers/:n<int>", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "uuids/:id<uuid>", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "even/:n<even>", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	char url[TEST_TREE_PATH_SIZE] = { 0 };
	(void) snprintf (url, TEST_TREE_PATH_SIZE, "/api/items/%s", test_tree_objectid);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, url, "/api/items/:id<objectid>");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/items/abc", "/api/items/:name");

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/numbers/-42", "/api/numbers/:n<int>");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/numbers/4x", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/numbers/-", NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/uuids/123e4567-e89b-12d3-a456-426614174000", "/api/uuids/:id<uuid>");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/uuids/123e4567e89b12d3a456426614174000", NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/even/12", "/api/even/:n<even>");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/even/13", NULL);

	test_check_true (cerver_http_param_validate_objectid (test_tree_objectid, strlen (test_tree_objectid)));
	const bool invalid = cerver_http_param_validate_objectid ("5f9b1c2d3e4f5a6b7c8d9e0g", strlen (test_tree_objectid));
	test_check_false (invalid);

	cerver_http_delete (http_cerver);

}

// the names of the params belong to the endpoint, so a position can have many names
static void test_tree_params_names (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *engines = cerver_http_group_create (http_cerver, "api/engines");
	test_check_ptr (cerver_http_group_get (engines, ":id/info", test_tree_handler));
	test_check_ptr (cerver_http_group_post (engines, ":engine/inference", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *info = test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/engines/e1/info", params, NULL);
	const HttpEndpoint *inference = test_tree_match (http_cerver, REQUEST_METHOD_POST, "/api/engines/e1/inference", params, NULL);

	test_check_ptr (info);
	test_check_ptr (inference);
	test_check_str_eq (info->params_names[0], "id", NULL);
	test_check_str_eq (inference->params_names[0], "engine", NULL);

	cerver_http_delete (http_cerver);

}

static void test_tree_expect_invalid (const char *path) {

	char error[HTTP_TREE_ERROR_SIZE] = { 0 };
	test_check_unsigned_eq (http_tree_path_validate (path, error, HTTP_TREE_ERROR_SIZE), 1, NULL);

	const bool has_reason = (strlen (error) > 0);
	test_check_true (has_reason);

}

// every kind of registration error stops the cerver
static void test_tree_registration_errors (void) {

	char error[HTTP_TREE_ERROR_SIZE] = { 0 };
	test_check_unsigned_eq (http_tree_path_validate ("", error, HTTP_TREE_ERROR_SIZE), 0, NULL);
	test_check_unsigned_eq (http_tree_path_validate ("/", error, HTTP_TREE_ERROR_SIZE), 0, NULL);
	test_check_unsigned_eq (http_tree_path_validate ("users/:id<objectid>/v1:batch/*rest", error, HTTP_TREE_ERROR_SIZE), 0, NULL);
	test_check_unsigned_eq (http_tree_path_validate ("files/*", error, HTTP_TREE_ERROR_SIZE), 0, NULL);

	test_tree_expect_invalid (NULL);
	test_tree_expect_invalid ("a//b");
	test_tree_expect_invalid ("a/");
	test_tree_expect_invalid ("//a");
	test_tree_expect_invalid (":");
	test_tree_expect_invalid (":<int>");
	test_tree_expect_invalid (":id<int");
	test_tree_expect_invalid (":id<>");
	test_tree_expect_invalid (":i-d");
	test_tree_expect_invalid ("*rest/more");
	test_tree_expect_invalid ("*re-st");

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");
	test_check_null_ptr (cerver_http_group_get (api, "a//b", test_tree_handler));
	test_check_null_ptr (cerver_http_group_get (api, "users", NULL));
	test_check_null_ptr (cerver_http_group_route_add (api, (CerverHttpMethod) REQUEST_METHOD_UNDEFINED, "users", test_tree_handler));
	test_check_null_ptr (cerver_http_group_create (http_cerver, "bad//group"));
	test_check_unsigned_eq (cerver_http_routes_errors_count (http_cerver), 4, NULL);

	// errors found when the tree is built
	test_check_ptr (cerver_http_group_get (api, "items/:id<unknown>", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "users", test_tree_handler));
	test_check_ptr (cerver_http_group_get (api, "users", test_tree_handler));
	test_check_ptr (cerver_http_group_post (api, "users", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 1, NULL);
	test_check_unsigned_eq (cerver_http_routes_errors_count (http_cerver), 6, NULL);
	test_check_ptr (strstr (cerver_http_routes_error (http_cerver), "a//b"));

	// the valid ones are still in the tree
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/users", "/api/users");

	cerver_http_delete (http_cerver);

	// a cerver without errors has no error message
	http_cerver = test_tree_cerver ();
	test_check_null_ptr (cerver_http_routes_error (http_cerver));
	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);
	cerver_http_delete (http_cerver);

}

static void test_tree_delete_data (void *data) {

	free (data);

}

// the endpoints inherit the settings of the nearest group that set them
static void test_tree_inheritance (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	int *group_data = (int *) malloc (sizeof (int));
	int *endpoint_data = (int *) malloc (sizeof (int));

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");
	cerver_http_group_set_auth (api, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_group_set_authentication_handler (api, test_tree_authentication);
	cerver_http_group_set_custom_data (api, group_data, test_tree_delete_data);
	cerver_http_group_set_max_body_size (api, 1024);
	cerver_http_group_set_cors (api, true);

	// the limits of the uploads and the compression of a group, since the 3.0b-13
	cerver_http_group_set_max_file_size (api, TEST_TREE_GROUP_MAX_FILE_SIZE);
	cerver_http_group_set_max_multipart_parts (api, TEST_TREE_GROUP_MAX_PARTS);
	cerver_http_group_set_compression_min_size (api, TEST_TREE_GROUP_COMPRESSION_MIN_SIZE);
	test_check_unsigned_eq (cerver_http_group_set_compression_level (api, TEST_TREE_GROUP_COMPRESSION_LEVEL), 0, NULL);

	// a level that zlib does not have is not set
	test_check_unsigned_eq (cerver_http_group_set_compression_level (api, HTTP_COMPRESSION_MAX_LEVEL + 1), 1, NULL);
	test_check_unsigned_eq (cerver_http_group_set_compression_level (NULL, TEST_TREE_GROUP_COMPRESSION_LEVEL), 1, NULL);

	HttpGroup *public_group = cerver_http_group_subgroup_add (api, "public");
	cerver_http_group_set_auth (public_group, HTTP_ROUTE_AUTH_TYPE_NONE);
	cerver_http_group_set_modifier (public_group, HTTP_ROUTE_MODIFIER_MULTI_PART);
	cerver_http_group_set_max_multipart_parts (public_group, TEST_TREE_SUBGROUP_MAX_PARTS);

	HttpEndpoint *inherited = cerver_http_group_get (api, "users", test_tree_handler);
	HttpEndpoint *overridden = cerver_http_group_post (public_group, "upload", test_tree_handler);
	HttpEndpoint *own = cerver_http_group_get (public_group, "own", test_tree_handler);
	cerver_http_endpoint_set_custom_data (own, endpoint_data, test_tree_delete_data);
	cerver_http_endpoint_set_max_body_size (own, 2048);
	cerver_http_endpoint_set_authentication_with_body (own, true);
	cerver_http_endpoint_set_max_file_size (own, TEST_TREE_ENDPOINT_MAX_FILE_SIZE);
	cerver_http_endpoint_set_compression_min_size (own, TEST_TREE_ENDPOINT_COMPRESSION_MIN_SIZE);
	test_check_unsigned_eq (
		cerver_http_endpoint_set_compression_level (own, TEST_TREE_ENDPOINT_COMPRESSION_LEVEL), 0, NULL
	);
	test_check_unsigned_eq (cerver_http_endpoint_set_compression_level (own, -1), 1, NULL);

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	// an endpoint takes each limit from the nearest group that set it, or keeps its own
	test_check_unsigned_eq (inherited->resolved.max_file_size, TEST_TREE_GROUP_MAX_FILE_SIZE, NULL);
	test_check_unsigned_eq (inherited->resolved.max_multipart_parts, TEST_TREE_GROUP_MAX_PARTS, NULL);
	test_check_unsigned_eq (inherited->resolved.compression_min_size, TEST_TREE_GROUP_COMPRESSION_MIN_SIZE, NULL);
	test_check_int_eq (inherited->resolved.compression_level, TEST_TREE_GROUP_COMPRESSION_LEVEL, NULL);

	test_check_unsigned_eq (overridden->resolved.max_file_size, TEST_TREE_GROUP_MAX_FILE_SIZE, NULL);
	test_check_unsigned_eq (overridden->resolved.max_multipart_parts, TEST_TREE_SUBGROUP_MAX_PARTS, NULL);

	test_check_unsigned_eq (own->resolved.max_file_size, TEST_TREE_ENDPOINT_MAX_FILE_SIZE, NULL);
	test_check_unsigned_eq (own->resolved.max_multipart_parts, TEST_TREE_SUBGROUP_MAX_PARTS, NULL);
	test_check_unsigned_eq (own->resolved.compression_min_size, TEST_TREE_ENDPOINT_COMPRESSION_MIN_SIZE, NULL);
	test_check_int_eq (own->resolved.compression_level, TEST_TREE_ENDPOINT_COMPRESSION_LEVEL, NULL);

	test_check_unsigned_eq (inherited->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);
	test_check_ptr_eq (inherited->resolved.authentication_handler, test_tree_authentication);
	test_check_ptr_eq (cerver_http_endpoint_get_custom_data (inherited), group_data);
	test_check_unsigned_eq (inherited->resolved.max_body_size, 1024, NULL);
	test_check_true (inherited->resolved.cors);

	test_check_unsigned_eq (overridden->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);
	test_check_unsigned_eq (overridden->resolved.modifier, HTTP_ROUTE_MODIFIER_MULTI_PART, NULL);
	test_check_ptr_eq (cerver_http_endpoint_get_custom_data (overridden), group_data);
	test_check_str_eq (overridden->path->str, "/api/public/upload", NULL);

	test_check_ptr_eq (cerver_http_endpoint_get_custom_data (own), endpoint_data);
	test_check_unsigned_eq (own->resolved.max_body_size, 2048, NULL);
	test_check_true (own->resolved.authentication_with_body);

	// the resolved settings never delete the data, the group and the endpoint do
	test_check_null_ptr (inherited->resolved.delete_custom_data);

	// the Allow header of a path, with HEAD and OPTIONS
	const HttpRouteNode *node = NULL;
	HttpRouteMatch match = { NULL, 0 };
	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	test_check_true (http_tree_match (http_cerver->tree, "/api/users", strlen ("/api/users"), params, &match));
	node = match.node;
	test_check_str_eq (node->allow, "GET, HEAD, OPTIONS", NULL);
	test_check_true (node->cors);

	test_check_true (http_tree_match (http_cerver->tree, "/api/public/upload", strlen ("/api/public/upload"), params, &match));
	test_check_str_eq (match.node->allow, "POST, OPTIONS", NULL);

	cerver_http_delete (http_cerver);

}

// the cerver's middlewares, then the groups' from the outermost and then the endpoint's,
// the after middlewares run in the opposite order
static void test_tree_middlewares (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	test_check_unsigned_eq (cerver_http_add_before (http_cerver, test_tree_before_cerver), 0, NULL);
	test_check_unsigned_eq (cerver_http_add_after (http_cerver, test_tree_after_cerver), 0, NULL);

	HttpGroup *api = cerver_http_group_create (http_cerver, "api");
	test_check_unsigned_eq (cerver_http_group_add_before (api, test_tree_before_group), 0, NULL);
	test_check_unsigned_eq (cerver_http_group_add_after (api, test_tree_after_group), 0, NULL);

	HttpGroup *users = cerver_http_group_subgroup_add (api, "users");
	test_check_unsigned_eq (cerver_http_group_add_before (users, test_tree_before_subgroup), 0, NULL);

	HttpEndpoint *endpoint = cerver_http_group_get (users, "list", test_tree_handler);
	test_check_unsigned_eq (cerver_http_endpoint_add_before (endpoint, test_tree_before_endpoint), 0, NULL);
	test_check_unsigned_eq (cerver_http_endpoint_add_after (endpoint, test_tree_after_endpoint), 0, NULL);

	HttpEndpoint *stopped = cerver_http_group_get (users, "stopped", test_tree_handler);
	test_check_unsigned_eq (cerver_http_endpoint_add_before (stopped, test_tree_before_stop), 0, NULL);
	test_check_unsigned_eq (cerver_http_endpoint_add_before (stopped, test_tree_before_endpoint), 0, NULL);

	test_check_unsigned_eq (cerver_http_endpoint_add_before (NULL, test_tree_before_endpoint), 1, NULL);
	test_check_unsigned_eq (cerver_http_group_add_before (api, NULL), 1, NULL);

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	test_tree_sequence_reset ();
	test_check_true (http_middlewares_run_before (&endpoint->resolved.middlewares, NULL));
	http_middlewares_run_after (&endpoint->resolved.middlewares, NULL);
	test_check_str_eq (test_tree_sequence, "cgseEGC", NULL);

	// a middleware that stops the request skips the rest and the handler
	test_tree_sequence_reset ();
	test_check_false (http_middlewares_run_before (&stopped->resolved.middlewares, NULL));
	test_check_str_eq (test_tree_sequence, "cgsx", NULL);

	// compiling again does not repeat the middlewares
	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);
	test_tree_sequence_reset ();
	(void) http_middlewares_run_before (&endpoint->resolved.middlewares, NULL);
	test_check_str_eq (test_tree_sequence, "cgse", NULL);

	test_check_str_eq (cerver_http_middleware_result_to_string (HTTP_MIDDLEWARE_RESULT_STOP), "Stop", NULL);
	test_check_str_eq (cerver_http_middleware_result_to_string (HTTP_MIDDLEWARE_RESULT_MAX), "None", NULL);

	cerver_http_delete (http_cerver);

}

// the endpoint of the path of the group itself, of a top level group and of a subgroup
static void test_tree_group_root (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = cerver_http_group_create (http_cerver, "api/service");
	test_check_ptr (cerver_http_group_root_add (api, REQUEST_METHOD_GET, test_tree_handler));

	HttpGroup *items = cerver_http_group_subgroup_add (api, "items");
	test_check_ptr (cerver_http_group_root_add (items, REQUEST_METHOD_GET, test_tree_handler));
	test_check_ptr (cerver_http_group_root_add (items, REQUEST_METHOD_POST, test_tree_handler));
	test_check_ptr (cerver_http_group_get (items, ":id/info", test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service", "/api/service");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/items", "/api/service/items");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/service/items", "/api/service/items");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/items/i1/info", "/api/service/items/:id/info");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/items/", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/item", NULL);

	cerver_http_delete (http_cerver);

	// the same root twice and a root without handler are registration errors
	http_cerver = test_tree_cerver ();

	items = cerver_http_group_create (http_cerver, "items");
	test_check_ptr (cerver_http_group_root_add (items, REQUEST_METHOD_GET, test_tree_handler));
	test_check_ptr (cerver_http_group_root_add (items, REQUEST_METHOD_GET, test_tree_handler));
	test_check_null_ptr (cerver_http_group_root_add (items, REQUEST_METHOD_POST, NULL));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 1, NULL);
	test_check_unsigned_eq (cerver_http_routes_errors_count (http_cerver), 2, NULL);

	cerver_http_delete (http_cerver);

}

static void test_tree_expect_auth (
	const HttpCerver *http_cerver, const CerverHttpMethod method,
	const char *url, const CerverHttpRouteAuthType auth_type
) {

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *endpoint = test_tree_match (http_cerver, method, url, params, NULL);

	test_check_ptr (endpoint);
	test_check_unsigned_eq (endpoint->resolved.auth_type, auth_type, NULL);

}

// the admin routes are the endpoints of a top level group of their own, only once:
// in the root by default or in the path that the cerver set, never in a group of the application
static void test_tree_admin_groups (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = cerver_http_group_create (http_cerver, "api/service");
	cerver_http_group_set_auth (api, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_group_set_authentication_handler (api, test_tree_authentication);
	test_check_ptr (cerver_http_group_get (api, "items", test_tree_handler));

	HttpGroup *other = cerver_http_group_create (http_cerver, "other");
	test_check_ptr (cerver_http_group_get (other, "items", test_tree_handler));

	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_enable_admin_head_handlers (http_cerver, true);

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (cerver_http_routes_errors_count (http_cerver), 0, NULL);

	// 2 endpoints and the 5 admin routes with their HEAD
	test_check_unsigned_eq (http_tree_get_n_endpoints (http_cerver->tree), 12, NULL);

	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/cerver/metrics", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_HEAD, "/cerver/metrics", HTTP_ROUTE_AUTH_TYPE_NONE);

	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/cerver/info", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/cerver/stats", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_HEAD, "/cerver/stats", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/cerver/stats/filesystems", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/cerver/stats/workers", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/items", HTTP_ROUTE_AUTH_TYPE_CUSTOM);

	// the groups of the application do not have them
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/other/cerver/stats", NULL);

	cerver_http_delete (http_cerver);

	// the admin routes in the path that the cerver set, the one of a group of the application:
	// they authenticate with their own handler and custom data, not with the ones of that group
	http_cerver = test_tree_cerver ();

	api = cerver_http_group_create (http_cerver, "api/service");
	cerver_http_group_set_custom_data (api, (void *) test_tree_sequence, NULL);
	test_check_ptr (cerver_http_group_get (api, "items", test_tree_handler));

	cerver_http_enable_admin_routes (http_cerver, true);
	cerver_http_admin_set_path (http_cerver, "api/service");
	cerver_http_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_admin_routes_set_authentication_handler (http_cerver, test_tree_authentication);
	cerver_http_admin_routes_set_custom_data (http_cerver, (void *) test_tree_objectid);

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *stats = test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats", params, NULL);
	test_check_ptr (stats);
	test_check_unsigned_eq (stats->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);
	test_check_ptr_eq (stats->resolved.authentication_handler, test_tree_authentication);
	test_check_ptr_eq (cerver_http_endpoint_get_custom_data (stats), test_tree_objectid);

	cerver_http_delete (http_cerver);

}

// the compiled endpoints as json, byte by byte: their auth, their modifier,
// the settings that a group gives and the ones that an endpoint replaces, and the ones of a WebSocket
static void test_tree_json (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	test_check_null_ptr (cerver_http_routes_generate_json (http_cerver));

	HttpGroup *api = cerver_http_group_create (http_cerver, "api/service");
	cerver_http_group_set_auth (api, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	cerver_http_group_set_authentication_handler (api, test_tree_authentication);
	cerver_http_group_set_etag (api, true);
	cerver_http_group_set_compression (api, true);
	test_check_ptr (cerver_http_group_root_add (api, REQUEST_METHOD_GET, test_tree_handler));

	HttpEndpoint *upload = cerver_http_group_post (api, "upload", test_tree_handler);
	test_check_ptr (upload);
	cerver_http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);
	cerver_http_endpoint_set_compression (upload, false);
	cerver_http_endpoint_set_cors (upload, true);
	cerver_http_endpoint_set_etag (upload, false);

	HttpEndpoint *internal = cerver_http_group_post (api, "internal", test_tree_handler);
	test_check_ptr (internal);
	cerver_http_endpoint_set_auth (internal, HTTP_ROUTE_AUTH_TYPE_NONE);

	HttpWebSocketHandlers handlers;
	(void) memset (&handlers, 0, sizeof (HttpWebSocketHandlers));
	handlers.on_message = test_tree_web_socket_message;

	HttpEndpoint *socket = cerver_http_group_web_socket_add (api, "socket", &handlers);
	test_check_ptr (socket);
	test_check_unsigned_eq (
		cerver_http_endpoint_set_web_socket_protocols (socket, test_tree_protocols, TEST_TREE_PROTOCOLS), 0, NULL
	);
	cerver_http_endpoint_set_web_socket_compression (socket, false);
	cerver_http_endpoint_set_web_socket_origin_check (socket, true);
	cerver_http_endpoint_set_web_socket_max_message (socket, TEST_TREE_MAX_MESSAGE);
	cerver_http_endpoint_set_web_socket_ping_milliseconds (socket, TEST_TREE_PING_INTERVAL, TEST_TREE_PONG_TIMEOUT);

	HttpGroup *main_group = cerver_http_group_create (http_cerver, "/api/main");
	test_check_ptr (cerver_http_group_root_add (main_group, REQUEST_METHOD_GET, test_tree_handler));

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);

	char *json_string = cerver_http_routes_generate_json (http_cerver);
	test_check_ptr (json_string);

	// the endpoints are in the order of the tree, the settings of the group are inherited
	// and an endpoint can replace them
	test_check_str_eq (
		json_string,
		"{\"n_endpoints\": 5, \"endpoints\": ["
		"{\"method\": \"GET\", \"path\": \"/api/main\", \"auth\": \"None\", \"modifier\": \"None\", "
		"\"cors\": false, \"etag\": false, \"compression\": false}, "
		"{\"method\": \"GET\", \"path\": \"/api/service\", \"auth\": \"Custom\", \"modifier\": \"None\", "
		"\"cors\": false, \"etag\": true, \"compression\": true}, "
		"{\"method\": \"POST\", \"path\": \"/api/service/internal\", \"auth\": \"None\", \"modifier\": \"None\", "
		"\"cors\": false, \"etag\": true, \"compression\": true}, "
		"{\"method\": \"GET\", \"path\": \"/api/service/socket\", \"auth\": \"Custom\", \"modifier\": \"WebSocket\", "
		"\"cors\": false, \"etag\": true, \"compression\": true, "
		"\"websocket\": {\"protocols\": [\"chat\", \"the.second\"], \"compression\": false, "
		"\"origin_check\": true, \"max_message\": 1024, \"ping_interval\": 500}}, "
		"{\"method\": \"POST\", \"path\": \"/api/service/upload\", \"auth\": \"Custom\", \"modifier\": \"Multi-Part\", "
		"\"cors\": true, \"etag\": false, \"compression\": false}"
		"]}",
		NULL
	);

	cerver_http_routes_json_delete (json_string);

	cerver_http_delete (http_cerver);

}

static bool test_tree_fuzz_is_registered (
	char routes[TEST_TREE_FUZZ_ROUTES][TEST_TREE_PATH_SIZE], const size_t n_routes, const char *url
) {

	bool registered = false;

	for (size_t idx = 0; idx < n_routes; idx++) {
		if (!strcmp (routes[idx], url)) {
			registered = true;
			break;
		}
	}

	return registered;

}

static void test_tree_fuzz_path (char *path, const size_t path_size, unsigned int *seed) {

	const unsigned int n_segments = 1 + ((unsigned int) rand_r (seed) % TEST_TREE_FUZZ_SEGMENTS);

	size_t written = 0;
	for (unsigned int idx = 0; idx < n_segments; idx++) {
		written += (size_t) snprintf (
			path + written, path_size - written, "/%s",
			test_tree_fuzz_alphabet[(size_t) rand_r (seed) % test_tree_fuzz_alphabet_size]
		);
	}

}

// random static routes against random urls: the tree matches exactly the registered ones
static void test_tree_fuzz (void) {

	unsigned int seed = TEST_TREE_FUZZ_SEED;

	static char routes[TEST_TREE_FUZZ_ROUTES][TEST_TREE_PATH_SIZE];
	size_t n_routes = 0;

	HttpCerver *http_cerver = test_tree_cerver ();
	HttpGroup *root = cerver_http_group_create (http_cerver, "/");

	for (size_t idx = 0; idx < TEST_TREE_FUZZ_ROUTES; idx++) {
		char path[TEST_TREE_PATH_SIZE] = { 0 };
		test_tree_fuzz_path (path, TEST_TREE_PATH_SIZE, &seed);

		if (!test_tree_fuzz_is_registered (routes, n_routes, path)) {
			(void) snprintf (routes[n_routes], TEST_TREE_PATH_SIZE, "%s", path);
			n_routes += 1;

			test_check_ptr (cerver_http_group_get (root, path, test_tree_handler));
		}
	}

	test_check_unsigned_eq (cerver_http_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (http_tree_get_n_endpoints (http_cerver->tree), n_routes, NULL);

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	for (size_t idx = 0; idx < TEST_TREE_FUZZ_URLS; idx++) {
		char url[TEST_TREE_PATH_SIZE] = { 0 };
		test_tree_fuzz_path (url, TEST_TREE_PATH_SIZE, &seed);

		const HttpEndpoint *endpoint = test_tree_match (http_cerver, REQUEST_METHOD_GET, url, params, NULL);
		const bool registered = test_tree_fuzz_is_registered (routes, n_routes, url);

		const bool found = (endpoint != NULL);
		test_check_bool_eq (found, registered, NULL);
		if (endpoint) test_check_str_eq (endpoint->path->str, url, NULL);
	}

	cerver_http_delete (http_cerver);

}

static unsigned int test_tree_deleted_data = 0;

static void test_tree_count_delete_data (void *data) {

	test_tree_deleted_data += 1;
	free (data);

}

// a different custom data deletes the one that the endpoint or the group had, the same one is kept (it leaked)
static void test_tree_custom_data_replaced (void) {

	test_tree_deleted_data = 0;

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *group = cerver_http_group_create (http_cerver, "replaced");
	HttpEndpoint *endpoint = cerver_http_group_get (group, "data", test_tree_handler);

	int *first = (int *) malloc (sizeof (int));
	int *second = (int *) malloc (sizeof (int));
	cerver_http_endpoint_set_custom_data (endpoint, first, test_tree_count_delete_data);
	cerver_http_endpoint_set_custom_data (endpoint, first, test_tree_count_delete_data);
	test_check_unsigned_eq (test_tree_deleted_data, 0, "the same custom data was deleted");
	cerver_http_endpoint_set_custom_data (endpoint, second, test_tree_count_delete_data);
	test_check_unsigned_eq (test_tree_deleted_data, 1, "the replaced custom data of the endpoint was not deleted");

	int *group_first = (int *) malloc (sizeof (int));
	int *group_second = (int *) malloc (sizeof (int));
	cerver_http_group_set_custom_data (group, group_first, test_tree_count_delete_data);
	cerver_http_group_set_custom_data (group, group_second, test_tree_count_delete_data);
	test_check_unsigned_eq (test_tree_deleted_data, 2, "the replaced custom data of the group was not deleted");

	cerver_http_delete (http_cerver);
	test_check_unsigned_eq (test_tree_deleted_data, 4, NULL);

}

void http_tests_tree (void) {

	(void) printf ("Testing HTTP routes tree...\n");

	test_tree_priority ();
	test_tree_backtracking ();
	test_tree_boundaries ();
	test_tree_depth ();
	test_tree_max_params ();
	test_tree_validators ();
	test_tree_params_names ();
	test_tree_registration_errors ();
	test_tree_inheritance ();
	test_tree_middlewares ();
	test_tree_group_root ();
	test_tree_admin_groups ();
	test_tree_json ();
	test_tree_fuzz ();
	test_tree_custom_data_replaced ();

	(void) printf ("Done!\n");

}
