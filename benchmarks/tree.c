#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <time.h>

#include <cerver/types/string.h>

#include <cerver/http/http.h>
#include <cerver/http/request.h>
#include <cerver/http/route.h>
#include <cerver/http/tree.h>

#include <cerver/utils/utils.h>

// matches the urls of a routes table with the matcher of 2.0b-58 (a copy of it)
// and with the routes tree, and counts the time and the allocations of each match
// usage: tree <routes file> [rounds], the synthetic tables are in benchmarks/routes/

#define TREE_BENCH_LINE_SIZE				512
#define TREE_BENCH_MAX_ROUTES				(1 << 12)
#define TREE_BENCH_DEFAULT_ROUNDS			(1 << 10)
#define TREE_BENCH_METHOD_SIZE				16
#define TREE_BENCH_OLD_BUCKETS				16

#define TREE_BENCH_NANOSECONDS				1000000000.0

static const char *tree_bench_param_value = { "5f9b1c2d3e4f5a6b7c8d9e0f" };

static const char tree_bench_separator = '/';
static const char tree_bench_param_prefix = ':';
static const char tree_bench_old_param = '*';

#pragma region allocations

// every allocation of the process is counted, the ones inside libcerver too
extern void *__libc_malloc (size_t size);
extern void *__libc_calloc (size_t count, size_t size);
extern void *__libc_realloc (void *ptr, size_t size);

static size_t tree_bench_allocations = 0;

void *malloc (size_t size) {

	tree_bench_allocations += 1;
	return __libc_malloc (size);

}

void *calloc (size_t count, size_t size) {

	tree_bench_allocations += 1;
	return __libc_calloc (count, size);

}

void *realloc (void *ptr, size_t size) {

	tree_bench_allocations += 1;
	return __libc_realloc (ptr, size);

}

#pragma endregion

#pragma region routes

typedef struct TreeBenchRoute {

	RequestMethod method;
	char path[TREE_BENCH_LINE_SIZE];
	char url[TREE_BENCH_LINE_SIZE];

} TreeBenchRoute;

static TreeBenchRoute tree_bench_routes[TREE_BENCH_MAX_ROUTES];
static size_t tree_bench_n_routes = 0;

static RequestMethod tree_bench_method (const char *method) {

	RequestMethod found = (RequestMethod) REQUEST_METHOD_UNDEFINED;

	#define XX(num, name, string) if (!strcmp (method, #string)) found = REQUEST_METHOD_##name;
	REQUEST_METHOD_MAP(XX)
	#undef XX

	return found;

}

// a url for the route, each param is an ObjectId
static void tree_bench_url (const char *path, char *url) {

	size_t written = 0;
	const char *segment = path;
	while (segment && *segment) {
		const char *next = strchr (segment + 1, tree_bench_separator);
		const size_t length = next ? (size_t) (next - segment) : strlen (segment);

		if (segment[1] == tree_bench_param_prefix) {
			written += (size_t) snprintf (url + written, TREE_BENCH_LINE_SIZE - written, "/%s", tree_bench_param_value);
		}

		else {
			written += (size_t) snprintf (url + written, TREE_BENCH_LINE_SIZE - written, "%.*s", (int) length, segment);
		}

		segment = next;
	}

}

static size_t tree_bench_routes_load (const char *filename) {

	FILE *file = fopen (filename, "r");
	if (!file) return 0;

	char line[TREE_BENCH_LINE_SIZE] = { 0 };
	while (fgets (line, TREE_BENCH_LINE_SIZE, file) && (tree_bench_n_routes < TREE_BENCH_MAX_ROUTES)) {
		char method[TREE_BENCH_METHOD_SIZE] = { 0 };
		TreeBenchRoute *route = &tree_bench_routes[tree_bench_n_routes];

		if (sscanf (line, "%15s %511s", method, route->path) == 2) {
			route->method = tree_bench_method (method);
			tree_bench_url (route->path, route->url);
			tree_bench_n_routes += 1;
		}
	}

	(void) fclose (file);

	return tree_bench_n_routes;

}

#pragma endregion

#pragma region old

// the matcher of 2.0b-58: one top level route, its children grouped by their number of tokens,
// each request splits its url with an allocation per token and compares the routes in order
typedef struct TreeBenchOldBucket {

	size_t n_routes;
	char ***tokens;

} TreeBenchOldBucket;

static String *tree_bench_old_top = NULL;
static TreeBenchOldBucket tree_bench_old_buckets[TREE_BENCH_OLD_BUCKETS];

static String *tree_bench_top (const char *path) {

	const char *first = strchr (path + 1, tree_bench_separator);
	const char *second = first ? strchr (first + 1, tree_bench_separator) : NULL;

	return second ? str_create ("%.*s", (int) (second - path), path) : str_new (path);

}

static void tree_bench_old_add (const char *path) {

	if (strlen (path) <= tree_bench_old_top->len) return;

	size_t n_tokens = 0;
	char **tokens = c_string_split (path + tree_bench_old_top->len, tree_bench_separator, &n_tokens);
	if (!tokens || !n_tokens || (n_tokens > TREE_BENCH_OLD_BUCKETS)) return;

	for (size_t idx = 0; idx < n_tokens; idx++) {
		if (tokens[idx][0] == tree_bench_param_prefix) tokens[idx][0] = tree_bench_old_param;
	}

	TreeBenchOldBucket *bucket = &tree_bench_old_buckets[n_tokens - 1];
	bucket->tokens = (char ***) realloc (bucket->tokens, (bucket->n_routes + 1) * sizeof (char **));
	bucket->tokens[bucket->n_routes] = tokens;
	bucket->n_routes += 1;

}

static bool tree_bench_old_compare (char **route, char **tokens, const size_t n_tokens) {

	bool matched = true;
	for (size_t idx = 0; matched && (idx < n_tokens); idx++) {
		if (route[idx][0] != tree_bench_old_param) matched = !strcmp (route[idx], tokens[idx]);
	}

	return matched;

}

static bool tree_bench_old_match (const char *url) {

	if (!strcmp (url, tree_bench_old_top->str)) return true;
	if (!strstr (url, tree_bench_old_top->str)) return false;

	size_t n_tokens = 0;
	char **tokens = c_string_split (url + tree_bench_old_top->len, tree_bench_separator, &n_tokens);

	bool matched = false;
	if (tokens && n_tokens && (n_tokens <= TREE_BENCH_OLD_BUCKETS)) {
		const TreeBenchOldBucket *bucket = &tree_bench_old_buckets[n_tokens - 1];
		for (size_t idx = 0; !matched && (idx < bucket->n_routes); idx++) {
			if (tree_bench_old_compare (bucket->tokens[idx], tokens, n_tokens)) {
				matched = true;

				// the params were copied in a string each
				for (size_t token = 0; token < n_tokens; token++) {
					if (bucket->tokens[idx][token][0] == tree_bench_old_param) str_delete (str_new (tokens[token]));
				}
			}
		}
	}

	for (size_t idx = 0; idx < n_tokens; idx++) free (tokens[idx]);
	free (tokens);

	return matched;

}

#pragma endregion

#pragma region measure

static double tree_bench_now (void) {

	struct timespec now = { 0, 0 };
	(void) clock_gettime (CLOCK_MONOTONIC, &now);

	return (double) now.tv_sec + ((double) now.tv_nsec / TREE_BENCH_NANOSECONDS);

}

typedef struct TreeBenchResult {

	size_t matches;
	size_t found;
	size_t allocations;
	double seconds;

} TreeBenchResult;

static void tree_bench_print (const char *name, const TreeBenchResult *result) {

	(void) printf (
		"%-6s %8.1f ns per match - %5.2f allocations per match - %zu of %zu found\n",
		name,
		(result->seconds * TREE_BENCH_NANOSECONDS) / (double) result->matches,
		(double) result->allocations / (double) result->matches,
		result->found / (result->matches / tree_bench_n_routes), tree_bench_n_routes
	);

}

static TreeBenchResult tree_bench_old (const size_t rounds) {

	TreeBenchResult result = { 0, 0, 0, 0 };

	const size_t allocations = tree_bench_allocations;
	const double start = tree_bench_now ();

	for (size_t round = 0; round < rounds; round++) {
		for (size_t idx = 0; idx < tree_bench_n_routes; idx++) {
			result.found += tree_bench_old_match (tree_bench_routes[idx].url) ? 1 : 0;
		}
	}

	result.seconds = tree_bench_now () - start;
	result.allocations = tree_bench_allocations - allocations;
	result.matches = rounds * tree_bench_n_routes;

	return result;

}

static TreeBenchResult tree_bench_tree (const HttpTree *tree, const size_t rounds) {

	TreeBenchResult result = { 0, 0, 0, 0 };

	HttpRequestParam params[HTTP_TREE_DEFAULT_MAX_PARAMS];
	HttpRouteMatch match = { NULL, 0 };

	const size_t allocations = tree_bench_allocations;
	const double start = tree_bench_now ();

	for (size_t round = 0; round < rounds; round++) {
		for (size_t idx = 0; idx < tree_bench_n_routes; idx++) {
			const char *url = tree_bench_routes[idx].url;
			result.found += http_tree_match (tree, url, strlen (url), params, &match) ? 1 : 0;
		}
	}

	result.seconds = tree_bench_now () - start;
	result.allocations = tree_bench_allocations - allocations;
	result.matches = rounds * tree_bench_n_routes;

	return result;

}

static void tree_bench_handler (
	const HttpReceive *http_receive, const HttpRequest *request
) {

	(void) http_receive;
	(void) request;

}

#pragma endregion

int main (int argc, char **argv) {

	if (argc < 2) {
		(void) fprintf (stderr, "usage: %s <routes file> [rounds]\n", argv[0]);
		return 1;
	}

	const size_t rounds = (argc > 2) ? (size_t) strtoul (argv[2], NULL, 10) : TREE_BENCH_DEFAULT_ROUNDS;

	if (!tree_bench_routes_load (argv[1])) {
		(void) fprintf (stderr, "no routes in %s\n", argv[1]);
		return 1;
	}

	// the top level route of the services is "/api/<service>", the first two segments
	tree_bench_old_top = tree_bench_top (tree_bench_routes[0].path);

	HttpCerver *http_cerver = http_cerver_create (NULL);
	HttpGroup *root = http_cerver_group_create (http_cerver, NULL);

	for (size_t idx = 0; idx < tree_bench_n_routes; idx++) {
		tree_bench_old_add (tree_bench_routes[idx].path);
		(void) http_group_route_add (root, tree_bench_routes[idx].method, tree_bench_routes[idx].path, tree_bench_handler);
	}

	(void) http_cerver_routes_compile (http_cerver);

	(void) printf ("%zu routes (%zu endpoints in the tree) - %zu rounds\n",
		tree_bench_n_routes, http_tree_get_n_endpoints (http_cerver->tree), rounds);

	const TreeBenchResult old = tree_bench_old (rounds);
	const TreeBenchResult tree = tree_bench_tree (http_cerver->tree, rounds);

	tree_bench_print ("old", &old);
	tree_bench_print ("tree", &tree);

	(void) printf ("speedup: %.1fx\n", old.seconds / tree.seconds);

	return 0;

}
