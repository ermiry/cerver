#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/http/core/handler.h>
#include <cerver/http/core/http.h>
#include <cerver/http/core/pool.h>
#include <cerver/http/core/routes.h>
#include <cerver/http/requests/request.h>
#include <cerver/http/routes/route.h>
#include <cerver/http/routes/tree.h>

// the registration of arbitrary paths and the match of arbitrary urls:
// the input is split in lines, the lines before the first empty one are paths
// to register (the first byte selects the method and the pool), and the rest are urls to match

#define TREE_ERROR_SIZE				(1 << 8)
#define TREE_METHODS				5
#define TREE_POOLS					4
#define TREE_POOL_THREADS			1

static const HttpRequestMethod tree_methods[TREE_METHODS] = {
	REQUEST_METHOD_GET, REQUEST_METHOD_POST, REQUEST_METHOD_PUT,
	REQUEST_METHOD_DELETE, REQUEST_METHOD_PATCH
};

static const char *tree_pool_name = { "slow" };

// no pool, the default one, the one that the cerver has and one that it does not have:
// the last one is an error of the tree when it is compiled
static const char *tree_pools[TREE_POOLS] = { NULL, "default", "slow", "missing" };

static void tree_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

// returns a copy of the line that ends at the next '\n', and moves the position after it
static char *tree_next_line (const uint8_t *data, const size_t size, size_t *position) {

	const size_t start = *position;

	size_t end = start;
	while ((end < size) && (data[end] != '\n')) end++;

	*position = (end < size) ? end + 1 : end;

	char *line = (char *) malloc ((end - start) + 1);
	if (line) {
		(void) memcpy (line, data + start, end - start);
		line[end - start] = '\0';
	}

	return line;

}

static void tree_register (HttpGroup *group, const char *line) {

	if (!line[0]) return;

	char error[TREE_ERROR_SIZE];
	(void) http_tree_path_validate (line + 1, error, TREE_ERROR_SIZE);

	const unsigned char selector = (unsigned char) line[0];

	const HttpRequestMethod method = tree_methods[selector % TREE_METHODS];
	HttpEndpoint *endpoint = http_group_route_add (group, method, line + 1, tree_handler);

	http_endpoint_set_pool (endpoint, tree_pools[(selector / TREE_METHODS) % TREE_POOLS]);

}

static void tree_match (const HttpCerver *http_cerver, const char *line) {

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	HttpRouteMatch match = { NULL, 0 };

	(void) http_tree_match (http_cerver->tree, line, strlen (line), params, &match);

}

int LLVMFuzzerTestOneInput (const uint8_t *data, size_t size) {

	HttpCerver *http_cerver = http_cerver_create (NULL);
	if (!http_cerver) return 0;

	(void) http_cerver_pool_create (http_cerver, tree_pool_name, TREE_POOL_THREADS);

	HttpGroup *group = http_cerver_group_create (http_cerver, "");

	size_t position = 0;
	bool registering = true;
	while (registering && (position < size)) {
		char *line = tree_next_line (data, size, &position);
		if (!line) break;

		registering = (line[0] != '\0');
		if (registering && group) tree_register (group, line);

		free (line);
	}

	// a registration error leaves the tree without endpoints, the urls are matched anyway
	(void) http_cerver_routes_compile (http_cerver);

	while (position < size) {
		char *line = tree_next_line (data, size, &position);
		if (!line) break;

		tree_match (http_cerver, line);

		free (line);
	}

	http_cerver_delete (http_cerver);

	return 0;

}
