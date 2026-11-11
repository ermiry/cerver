#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/collections/dlist.h>

#include <cerver/http/http.h>
#include <cerver/http/middleware.h>
#include <cerver/http/request.h>
#include <cerver/http/route.h>
#include <cerver/http/tree.h>
#include <cerver/http/validator.h>

#include <cerver/http/json/json.h>
#include <cerver/http/json/value.h>

#include "../test.h"

#define TEST_TREE_PATH_SIZE					512

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

static const char *test_tree_objectid = { "5f9b1c2d3e4f5a6b7c8d9e0f" };

// the order in which the middlewares ran
static char test_tree_sequence[TEST_TREE_SEQUENCE_SIZE] = { 0 };
static size_t test_tree_sequence_len = 0;

static void test_tree_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

static unsigned int test_tree_authentication (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

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
	static HttpMiddlewareResult name (													\
		const HttpReceive *http_receive, const HttpRequest *request						\
	) {																					\
		(void) http_receive;															\
		(void) request;																	\
		test_tree_sequence_add (step);													\
		return result;																	\
	}

#define TEST_TREE_AFTER(name, step)														\
	static void name (																	\
		const HttpReceive *http_receive, const HttpRequest *request						\
	) {																					\
		(void) http_receive;															\
		(void) request;																	\
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

	HttpCerver *http_cerver = http_cerver_create (NULL);
	test_check_ptr (http_cerver);
	test_check_ptr (http_cerver->tree);

	return http_cerver;

}

// returns the endpoint of the method in the node that matched the path, NULL if none
static const HttpEndpoint *test_tree_match (
	const HttpCerver *http_cerver, const RequestMethod method, const char *path,
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
	const HttpCerver *http_cerver, const RequestMethod method,
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

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	test_check_ptr (http_group_get (api, "users/*rest", test_tree_handler));
	test_check_ptr (http_group_get (api, "users/:id", test_tree_handler));
	test_check_ptr (http_group_get (api, "users/me", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

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

	http_cerver_delete (http_cerver);

}

// a static segment that does not lead to an endpoint goes back to the param
static void test_tree_backtracking (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *root = http_cerver_group_create (http_cerver, "/");
	test_check_ptr (http_group_get (root, "a/static/view", test_tree_handler));
	test_check_ptr (http_group_get (root, "a/:id/edit", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/a/static/view", "/a/static/view");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/a/static/edit", "/a/:id/edit");

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	unsigned int n_params = 0;
	(void) test_tree_match (http_cerver, REQUEST_METHOD_GET, "/a/static/edit", params, &n_params);
	test_check_unsigned_eq (n_params, 1, NULL);
	test_check_true (test_tree_param_is (&params[0], "static"));

	http_cerver_delete (http_cerver);

}

// "//", a trailing "/" (the tree is strict, the policy is applied by the receive)
// and a prefix without a segment boundary never match
static void test_tree_boundaries (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	test_check_ptr (http_group_get (api, "users", test_tree_handler));
	test_check_ptr (http_group_get (api, "", test_tree_handler));

	HttpGroup *root = http_cerver_group_create (http_cerver, "");
	test_check_ptr (http_group_get (root, "", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

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

	http_cerver_delete (http_cerver);

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

	HttpGroup *root = http_cerver_group_create (http_cerver, NULL);
	test_check_ptr (root);
	test_check_ptr (http_group_get (root, path, test_tree_handler));

	// a top level param
	test_check_ptr (http_group_get (root, ":tenant/info", test_tree_handler));

	// nested groups with params
	HttpGroup *tenants = http_cerver_group_create (http_cerver, "tenants/:tenant");
	HttpGroup *projects = http_group_create (tenants, "projects/:project");
	test_check_ptr (http_group_get (projects, "blocks/:block/info", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

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

	http_cerver_delete (http_cerver);

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
	HttpGroup *root = http_cerver_group_create (http_cerver, "/");
	test_check_ptr (http_group_get (root, path, test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 1, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 1, NULL);
	test_check_ptr (strstr (http_cerver_routes_error (http_cerver), "params"));

	http_cerver_delete (http_cerver);

	http_cerver = test_tree_cerver ();
	http_cerver_set_max_route_params (http_cerver, TEST_TREE_MORE_PARAMS);

	root = http_cerver_group_create (http_cerver, "/");
	test_check_ptr (http_group_get (root, path, test_tree_handler));
	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

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

	http_cerver_delete (http_cerver);

}

static bool test_tree_validate_even (const char *value, const size_t length) {

	return length && (((value[length - 1] - '0') % 2) == 0);

}

// a param with a validator only matches the values that it accepts
static void test_tree_validators (void) {

	HttpCerver *http_cerver = test_tree_cerver ();
	test_check_unsigned_eq (http_cerver_register_param_validator (http_cerver, "even", test_tree_validate_even), 0, NULL);

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	test_check_ptr (http_group_get (api, "items/:name", test_tree_handler));
	test_check_ptr (http_group_get (api, "items/:id<objectid>", test_tree_handler));
	test_check_ptr (http_group_get (api, "numbers/:n<int>", test_tree_handler));
	test_check_ptr (http_group_get (api, "uuids/:id<uuid>", test_tree_handler));
	test_check_ptr (http_group_get (api, "even/:n<even>", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

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

	test_check_true (http_param_validate_objectid (test_tree_objectid, strlen (test_tree_objectid)));
	const bool invalid = http_param_validate_objectid ("5f9b1c2d3e4f5a6b7c8d9e0g", strlen (test_tree_objectid));
	test_check_false (invalid);

	http_cerver_delete (http_cerver);

}

// the names of the params belong to the endpoint, so a position can have many names
static void test_tree_params_names (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *engines = http_cerver_group_create (http_cerver, "api/engines");
	test_check_ptr (http_group_get (engines, ":id/info", test_tree_handler));
	test_check_ptr (http_group_post (engines, ":engine/inference", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *info = test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/engines/e1/info", params, NULL);
	const HttpEndpoint *inference = test_tree_match (http_cerver, REQUEST_METHOD_POST, "/api/engines/e1/inference", params, NULL);

	test_check_ptr (info);
	test_check_ptr (inference);
	test_check_str_eq (info->params_names[0], "id", NULL);
	test_check_str_eq (inference->params_names[0], "engine", NULL);

	http_cerver_delete (http_cerver);

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

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	test_check_null_ptr (http_group_get (api, "a//b", test_tree_handler));
	test_check_null_ptr (http_group_get (api, "users", NULL));
	test_check_null_ptr (http_group_route_add (api, (RequestMethod) REQUEST_METHOD_UNDEFINED, "users", test_tree_handler));
	test_check_null_ptr (http_cerver_group_create (http_cerver, "bad//group"));
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 4, NULL);

	// errors found when the tree is built
	test_check_ptr (http_group_get (api, "items/:id<unknown>", test_tree_handler));
	test_check_ptr (http_group_get (api, "users", test_tree_handler));
	test_check_ptr (http_group_get (api, "users", test_tree_handler));
	test_check_ptr (http_group_post (api, "users", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 1, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 6, NULL);
	test_check_ptr (strstr (http_cerver_routes_error (http_cerver), "a//b"));

	// the valid ones are still in the tree
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/users", "/api/users");

	http_cerver_delete (http_cerver);

	// a cerver without errors has no error message
	http_cerver = test_tree_cerver ();
	test_check_null_ptr (http_cerver_routes_error (http_cerver));
	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
	http_cerver_delete (http_cerver);

}

static void test_tree_delete_data (void *data) {

	free (data);

}

// the endpoints inherit the settings of the nearest group that set them
static void test_tree_inheritance (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	int *group_data = (int *) malloc (sizeof (int));
	int *endpoint_data = (int *) malloc (sizeof (int));

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	http_group_set_auth (api, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_group_set_authentication_handler (api, test_tree_authentication);
	http_group_set_custom_data (api, group_data, test_tree_delete_data);
	http_group_set_max_body_size (api, 1024);
	http_group_set_cors (api, true);

	HttpGroup *public_group = http_group_create (api, "public");
	http_group_set_auth (public_group, HTTP_ROUTE_AUTH_TYPE_NONE);
	http_group_set_modifier (public_group, HTTP_ROUTE_MODIFIER_MULTI_PART);

	HttpEndpoint *inherited = http_group_get (api, "users", test_tree_handler);
	HttpEndpoint *overridden = http_group_post (public_group, "upload", test_tree_handler);
	HttpEndpoint *own = http_group_get (public_group, "own", test_tree_handler);
	http_endpoint_set_custom_data (own, endpoint_data, test_tree_delete_data);
	http_endpoint_set_max_body_size (own, 2048);
	http_endpoint_set_authentication_with_body (own, true);

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	test_check_unsigned_eq (inherited->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);
	test_check_ptr_eq (inherited->resolved.authentication_handler, test_tree_authentication);
	test_check_ptr_eq (http_endpoint_get_custom_data (inherited), group_data);
	test_check_unsigned_eq (inherited->resolved.max_body_size, 1024, NULL);
	test_check_true (inherited->resolved.cors);

	test_check_unsigned_eq (overridden->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_NONE, NULL);
	test_check_unsigned_eq (overridden->resolved.modifier, HTTP_ROUTE_MODIFIER_MULTI_PART, NULL);
	test_check_ptr_eq (http_endpoint_get_custom_data (overridden), group_data);
	test_check_str_eq (overridden->path->str, "/api/public/upload", NULL);

	test_check_ptr_eq (http_endpoint_get_custom_data (own), endpoint_data);
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

	http_cerver_delete (http_cerver);

}

// the cerver's middlewares, then the groups' from the outermost and then the endpoint's,
// the after middlewares run in the opposite order
static void test_tree_middlewares (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	test_check_unsigned_eq (http_cerver_add_before (http_cerver, test_tree_before_cerver), 0, NULL);
	test_check_unsigned_eq (http_cerver_add_after (http_cerver, test_tree_after_cerver), 0, NULL);

	HttpGroup *api = http_cerver_group_create (http_cerver, "api");
	test_check_unsigned_eq (http_group_add_before (api, test_tree_before_group), 0, NULL);
	test_check_unsigned_eq (http_group_add_after (api, test_tree_after_group), 0, NULL);

	HttpGroup *users = http_group_create (api, "users");
	test_check_unsigned_eq (http_group_add_before (users, test_tree_before_subgroup), 0, NULL);

	HttpEndpoint *endpoint = http_group_get (users, "list", test_tree_handler);
	test_check_unsigned_eq (http_endpoint_add_before (endpoint, test_tree_before_endpoint), 0, NULL);
	test_check_unsigned_eq (http_endpoint_add_after (endpoint, test_tree_after_endpoint), 0, NULL);

	HttpEndpoint *stopped = http_group_get (users, "stopped", test_tree_handler);
	test_check_unsigned_eq (http_endpoint_add_before (stopped, test_tree_before_stop), 0, NULL);
	test_check_unsigned_eq (http_endpoint_add_before (stopped, test_tree_before_endpoint), 0, NULL);

	test_check_unsigned_eq (http_endpoint_add_before (NULL, test_tree_before_endpoint), 1, NULL);
	test_check_unsigned_eq (http_group_add_before (api, NULL), 1, NULL);

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	test_tree_sequence_reset ();
	test_check_true (http_middlewares_run_before (&endpoint->resolved.middlewares, NULL, NULL));
	http_middlewares_run_after (&endpoint->resolved.middlewares, NULL, NULL);
	test_check_str_eq (test_tree_sequence, "cgseEGC", NULL);

	// a middleware that stops the request skips the rest and the handler
	test_tree_sequence_reset ();
	test_check_false (http_middlewares_run_before (&stopped->resolved.middlewares, NULL, NULL));
	test_check_str_eq (test_tree_sequence, "cgsx", NULL);

	// compiling again does not repeat the middlewares
	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
	test_tree_sequence_reset ();
	(void) http_middlewares_run_before (&endpoint->resolved.middlewares, NULL, NULL);
	test_check_str_eq (test_tree_sequence, "cgse", NULL);

	test_check_str_eq (http_middleware_result_to_string (HTTP_MIDDLEWARE_RESULT_STOP), "Stop", NULL);
	test_check_str_eq (http_middleware_result_to_string (HTTP_MIDDLEWARE_RESULT_MAX), "None", NULL);

	http_cerver_delete (http_cerver);

}

static void test_tree_child_add (HttpRoute *parent, HttpRoute *child) {

	test_check_ptr (child);
	http_route_child_add (parent, child);

}

// the routes of the first API are translated to the tree: the routes that
// were shadowed by an earlier route are reachable, a duplicate is only a warning
static void test_tree_compatibility (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpRoute *payloads = http_route_create (REQUEST_METHOD_GET, "api/payloads", test_tree_handler);
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_GET, ":id/status", test_tree_handler));
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_GET, "debug/status", test_tree_handler));
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_PUT, ":id/status/update", test_tree_handler));
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_PUT, ":id/status/update", test_tree_handler));

	// the same path with two methods in two routes (controller-service)
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_GET, "containers/hosts", test_tree_handler));
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_POST, "containers/hosts", test_tree_handler));

	// a ':' inside a segment is not a param
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_POST, "v1:batch", test_tree_handler));

	// a grandchild added before its parent was attached
	HttpRoute *child = http_route_create (REQUEST_METHOD_GET, "nested", test_tree_handler);
	test_tree_child_add (child, http_route_create (REQUEST_METHOD_GET, "deep/:id", test_tree_handler));
	test_tree_child_add (payloads, child);

	// the methods of a route share its settings
	HttpRoute *shared = http_route_create (REQUEST_METHOD_GET, "shared", test_tree_handler);
	http_route_set_handler (shared, REQUEST_METHOD_DELETE, test_tree_handler);
	http_route_set_auth (shared, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_route_set_custom_data (shared, (void *) test_tree_objectid);
	test_tree_child_add (payloads, shared);

	// an invalid path is skipped with its children
	test_tree_child_add (payloads, http_route_create (REQUEST_METHOD_GET, "bad//path", test_tree_handler));

	http_cerver_route_register (http_cerver, payloads);

	HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "/", test_tree_handler);
	test_tree_child_add (main_route, http_route_create (REQUEST_METHOD_GET, "cerver/info", test_tree_handler));
	http_cerver_route_register (http_cerver, main_route);

	// the duplicate and the invalid path are only warnings
	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/payloads", "/api/payloads");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/payloads/debug/status", "/api/payloads/debug/status");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/payloads/p1/status", "/api/payloads/:id/status");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_PUT, "/api/payloads/p1/status/update", "/api/payloads/:id/status/update");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/payloads/containers/hosts", "/api/payloads/containers/hosts");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/payloads/containers/hosts", "/api/payloads/containers/hosts");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/payloads/v1:batch", "/api/payloads/v1:batch");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/payloads/v1:other", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/payloads/nested/deep/d1", "/api/payloads/nested/deep/:id");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/payloads/bad//path", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/", "/");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/cerver/info", "/cerver/info");

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *get = test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/payloads/shared", params, NULL);
	const HttpEndpoint *removed = test_tree_match (http_cerver, REQUEST_METHOD_DELETE, "/api/payloads/shared", params, NULL);
	test_check_ptr (get);
	test_check_ptr (removed);
	test_check_ptr_eq (get->route, shared);
	test_check_ptr_eq (removed->route, shared);
	test_check_unsigned_eq (removed->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);
	test_check_ptr_eq (http_endpoint_get_custom_data (removed), test_tree_objectid);

	// the stats stay in the route, so the admin routes still read them
	test_check_ptr_eq (get->stats, shared->stats[REQUEST_METHOD_GET]);

	// the complete path of the grandchild is resolved when the tree is built
	test_check_str_eq (((HttpRoute *) dlist_start (child->children)->data)->route->str, "/api/payloads/nested/deep/:id", NULL);

	http_cerver_delete (http_cerver);

}

// the endpoint of the path of the group itself, of a top level group and of a subgroup
static void test_tree_group_root (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = http_cerver_group_create (http_cerver, "api/service");
	test_check_ptr (http_group_root_add (api, REQUEST_METHOD_GET, test_tree_handler));

	HttpGroup *items = http_group_create (api, "items");
	test_check_ptr (http_group_root_add (items, REQUEST_METHOD_GET, test_tree_handler));
	test_check_ptr (http_group_root_add (items, REQUEST_METHOD_POST, test_tree_handler));
	test_check_ptr (http_group_get (items, ":id/info", test_tree_handler));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service", "/api/service");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/items", "/api/service/items");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_POST, "/api/service/items", "/api/service/items");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/items/i1/info", "/api/service/items/:id/info");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/items/", NULL);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/item", NULL);

	http_cerver_delete (http_cerver);

	// the same root twice and a root without handler are registration errors
	http_cerver = test_tree_cerver ();

	items = http_cerver_group_create (http_cerver, "items");
	test_check_ptr (http_group_root_add (items, REQUEST_METHOD_GET, test_tree_handler));
	test_check_ptr (http_group_root_add (items, REQUEST_METHOD_GET, test_tree_handler));
	test_check_null_ptr (http_group_root_add (items, REQUEST_METHOD_POST, NULL));

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 1, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 2, NULL);

	http_cerver_delete (http_cerver);

}

static void test_tree_expect_auth (
	const HttpCerver *http_cerver, const RequestMethod method,
	const char *url, const HttpRouteAuthType auth_type
) {

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *endpoint = test_tree_match (http_cerver, method, url, params, NULL);

	test_check_ptr (endpoint);
	test_check_unsigned_eq (endpoint->resolved.auth_type, auth_type, NULL);

}

// a cerver that only has groups gets the admin routes in its first top level group,
// with the auth of the admin routes and not the one of the group, only once
static void test_tree_admin_groups (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	HttpGroup *api = http_cerver_group_create (http_cerver, "api/service");
	http_group_set_auth (api, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_group_set_authentication_handler (api, test_tree_authentication);
	test_check_ptr (http_group_get (api, "items", test_tree_handler));

	HttpGroup *other = http_cerver_group_create (http_cerver, "other");
	test_check_ptr (http_group_get (other, "items", test_tree_handler));

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_head_handlers (http_cerver, true);

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (http_cerver_routes_errors_count (http_cerver), 0, NULL);

	// 2 endpoints and the 5 admin routes with their HEAD
	test_check_unsigned_eq (http_tree_get_n_endpoints (http_cerver->tree), 12, NULL);

	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/metrics", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_HEAD, "/api/service/cerver/metrics", HTTP_ROUTE_AUTH_TYPE_NONE);

	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/info", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_HEAD, "/api/service/cerver/stats", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats/filesystems", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats/workers", HTTP_ROUTE_AUTH_TYPE_NONE);
	test_tree_expect_auth (http_cerver, REQUEST_METHOD_GET, "/api/service/items", HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/other/cerver/stats", NULL);

	http_cerver_delete (http_cerver);

	// the admin routes authenticate with their own handler and custom data
	http_cerver = test_tree_cerver ();

	api = http_cerver_group_create (http_cerver, "api/service");
	http_group_set_custom_data (api, (void *) test_tree_sequence, NULL);
	test_check_ptr (http_group_get (api, "items", test_tree_handler));

	http_cerver_enable_admin_routes (http_cerver, true);
	http_cerver_enable_admin_routes_authentication (http_cerver, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_cerver_admin_routes_set_authentication_handler (http_cerver, test_tree_authentication);
	http_cerver_admin_routes_set_custom_data (http_cerver, (void *) test_tree_objectid);

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	const HttpEndpoint *stats = test_tree_match (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats", params, NULL);
	test_check_ptr (stats);
	test_check_unsigned_eq (stats->resolved.auth_type, HTTP_ROUTE_AUTH_TYPE_CUSTOM, NULL);
	test_check_ptr_eq (stats->resolved.authentication_handler, test_tree_authentication);
	test_check_ptr_eq (http_endpoint_get_custom_data (stats), test_tree_objectid);

	http_cerver_delete (http_cerver);

	// with routes of the first API the admin routes are children of its main route
	http_cerver = test_tree_cerver ();

	HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "api/main", test_tree_handler);
	http_cerver_route_register (http_cerver, main_route);

	api = http_cerver_group_create (http_cerver, "api/service");
	test_check_ptr (http_group_get (api, "items", test_tree_handler));

	http_cerver_enable_admin_routes (http_cerver, true);

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/main/cerver/stats", "/api/main/cerver/stats");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/main/cerver/metrics", "/api/main/cerver/metrics");
	test_tree_expect_path (http_cerver, REQUEST_METHOD_GET, "/api/service/cerver/stats", NULL);
	test_check_unsigned_eq (dlist_size (main_route->children), 5, NULL);

	http_cerver_delete (http_cerver);

}

static const json_t *test_tree_json_endpoint (
	const json_t *endpoints, const char *method, const char *path
) {

	const json_t *found = NULL;

	for (size_t idx = 0; idx < json_array_size (endpoints); idx++) {
		const json_t *endpoint = json_array_get (endpoints, idx);
		if (
			!strcmp (json_string_value (json_object_get (endpoint, "method")), method)
			&& !strcmp (json_string_value (json_object_get (endpoint, "path")), path)
		) {
			found = endpoint;
			break;
		}
	}

	return found;

}

// the compiled endpoints as json, with their auth, modifier and if they come from the first API
static void test_tree_json (void) {

	HttpCerver *http_cerver = test_tree_cerver ();

	test_check_null_ptr (http_cerver_routes_generate_json (http_cerver));

	HttpGroup *api = http_cerver_group_create (http_cerver, "api/service");
	http_group_set_auth (api, HTTP_ROUTE_AUTH_TYPE_CUSTOM);
	http_group_set_authentication_handler (api, test_tree_authentication);
	http_group_set_etag (api, true);
	http_group_set_compression (api, true);
	test_check_ptr (http_group_root_add (api, REQUEST_METHOD_GET, test_tree_handler));

	HttpEndpoint *upload = http_group_post (api, "upload", test_tree_handler);
	test_check_ptr (upload);
	http_endpoint_set_modifier (upload, HTTP_ROUTE_MODIFIER_MULTI_PART);
	http_endpoint_set_compression (upload, false);
	http_endpoint_set_cors (upload, true);
	http_endpoint_set_etag (upload, false);

	HttpEndpoint *internal = http_group_post (api, "internal", test_tree_handler);
	test_check_ptr (internal);
	http_endpoint_set_auth (internal, HTTP_ROUTE_AUTH_TYPE_NONE);

	HttpRoute *main_route = http_route_create (REQUEST_METHOD_GET, "api/main", test_tree_handler);
	http_cerver_route_register (http_cerver, main_route);

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);

	char *json_string = http_cerver_routes_generate_json (http_cerver);
	test_check_ptr (json_string);

	json_error_t error = CERVER_ZERO_INITIALIZER;
	json_t *json = json_loads (json_string, 0, &error);
	test_check_ptr (json);

	const json_t *endpoints = json_object_get (json, "endpoints");
	test_check_unsigned_eq (json_integer_value (json_object_get (json, "n_endpoints")), 4, NULL);
	test_check_unsigned_eq (json_array_size (endpoints), 4, NULL);

	const json_t *root = test_tree_json_endpoint (endpoints, "GET", "/api/service");
	test_check_ptr (root);
	test_check_str_eq (json_string_value (json_object_get (root, "auth")), http_route_auth_type_to_string (HTTP_ROUTE_AUTH_TYPE_CUSTOM), NULL);
	test_check (json_is_false (json_object_get (root, "compatibility")), NULL);

	// the settings of the group are inherited, an endpoint can replace them
	test_check (json_is_true (json_object_get (root, "etag")), NULL);
	test_check (json_is_true (json_object_get (root, "compression")), NULL);
	test_check (json_is_false (json_object_get (root, "cors")), NULL);

	const json_t *upload_json = test_tree_json_endpoint (endpoints, "POST", "/api/service/upload");
	test_check_ptr (upload_json);
	test_check_str_eq (json_string_value (json_object_get (upload_json, "modifier")), http_route_modifier_to_string (HTTP_ROUTE_MODIFIER_MULTI_PART), NULL);
	test_check (json_is_false (json_object_get (upload_json, "etag")), NULL);
	test_check (json_is_false (json_object_get (upload_json, "compression")), NULL);
	test_check (json_is_true (json_object_get (upload_json, "cors")), NULL);

	const json_t *internal_json = test_tree_json_endpoint (endpoints, "POST", "/api/service/internal");
	test_check_ptr (internal_json);
	test_check_str_eq (json_string_value (json_object_get (internal_json, "auth")), http_route_auth_type_to_string (HTTP_ROUTE_AUTH_TYPE_NONE), NULL);

	const json_t *main_json = test_tree_json_endpoint (endpoints, "GET", "/api/main");
	test_check_ptr (main_json);
	test_check (json_is_true (json_object_get (main_json, "compatibility")), NULL);

	json_decref (json);
	http_cerver_routes_json_delete (json_string);

	http_cerver_delete (http_cerver);

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
	HttpGroup *root = http_cerver_group_create (http_cerver, "/");

	for (size_t idx = 0; idx < TEST_TREE_FUZZ_ROUTES; idx++) {
		char path[TEST_TREE_PATH_SIZE] = { 0 };
		test_tree_fuzz_path (path, TEST_TREE_PATH_SIZE, &seed);

		if (!test_tree_fuzz_is_registered (routes, n_routes, path)) {
			(void) snprintf (routes[n_routes], TEST_TREE_PATH_SIZE, "%s", path);
			n_routes += 1;

			test_check_ptr (http_group_get (root, path, test_tree_handler));
		}
	}

	test_check_unsigned_eq (http_cerver_routes_compile (http_cerver), 0, NULL);
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

	http_cerver_delete (http_cerver);

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
	test_tree_compatibility ();
	test_tree_group_root ();
	test_tree_admin_groups ();
	test_tree_json ();
	test_tree_fuzz ();

	(void) printf ("Done!\n");

}
