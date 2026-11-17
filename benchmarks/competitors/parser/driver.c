#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "../measure.h"
#include "backend.h"

static const char *parser_module = { "parser" };

// a request with only its line and its host
static const char parser_small[] =
	"GET / HTTP/1.1\r\n"
	"Host: localhost\r\n"
	"\r\n";

// the request of a page from a browser
static const char parser_browser[] =
	"GET /dashboard/reports?range=week&page=2 HTTP/1.1\r\n"
	"Host: cerver.test\r\n"
	"User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0 Safari/537.36\r\n"
	"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,image/webp,*/*;q=0.8\r\n"
	"Accept-Language: es-MX,es;q=0.9,en-US;q=0.8,en;q=0.7\r\n"
	"Accept-Encoding: gzip, deflate, br, zstd\r\n"
	"Cookie: session=6f1d2c3b4a5968778695a4b3c2d1e0f0; theme=dark; locale=es-MX\r\n"
	"Referer: https://cerver.test/dashboard\r\n"
	"Connection: keep-alive\r\n"
	"Cache-Control: max-age=0\r\n"
	"Upgrade-Insecure-Requests: 1\r\n"
	"\r\n";

// a request of an API with a token and a JSON body
static const char parser_post[] =
	"POST /api/users HTTP/1.1\r\n"
	"Host: api.cerver.test\r\n"
	"Authorization: Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJ1c2VyLTEyMyIsImV4cCI6MjAwMDAwMDAwMH0."
		"c2lnbmF0dXJlLW9mLXRoZS10b2tlbi1vZi10aGUtdXNlcg\r\n"
	"Content-Type: application/json\r\n"
	"Content-Length: 130\r\n"
	"\r\n"
	"{\"name\":\"user-123\",\"email\":\"user123@cerver.test\",\"roles\":[\"read\",\"write\"],"
	"\"address\":{\"city\":\"city-1\",\"zip\":\"01234\"},\"active\":true}";

// a cookie of a session with its value of 64 bytes
#define PARSER_COOKIE(number) "cookie" #number "=6f1d2c3b4a5968778695a4b3c2d1e0f06f1d2c3b4a5968778695a4b3c2d1e0f0; "

// the request of a page with the cookies of many services: one value of 1.8 KB
static const char parser_cookies[] =
	"GET /account/settings HTTP/1.1\r\n"
	"Host: cerver.test\r\n"
	"User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/140.0 Safari/537.36\r\n"
	"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8\r\n"
	"Cookie: "
		PARSER_COOKIE (01) PARSER_COOKIE (02) PARSER_COOKIE (03) PARSER_COOKIE (04) PARSER_COOKIE (05)
		PARSER_COOKIE (06) PARSER_COOKIE (07) PARSER_COOKIE (08) PARSER_COOKIE (09) PARSER_COOKIE (10)
		PARSER_COOKIE (11) PARSER_COOKIE (12) PARSER_COOKIE (13) PARSER_COOKIE (14) PARSER_COOKIE (15)
		PARSER_COOKIE (16) PARSER_COOKIE (17) PARSER_COOKIE (18) PARSER_COOKIE (19) PARSER_COOKIE (20)
		PARSER_COOKIE (21) PARSER_COOKIE (22) PARSER_COOKIE (23) PARSER_COOKIE (24) "theme=dark\r\n"
	"Connection: keep-alive\r\n"
	"\r\n";

// a chunk of 64 bytes, its size in hexadecimal
#define PARSER_CHUNK "40\r\n" "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef" "\r\n"

// a request of an API with a body of 8 chunks
static const char parser_chunked[] =
	"POST /api/uploads HTTP/1.1\r\n"
	"Host: api.cerver.test\r\n"
	"Content-Type: application/octet-stream\r\n"
	"Transfer-Encoding: chunked\r\n"
	"\r\n"
	PARSER_CHUNK PARSER_CHUNK PARSER_CHUNK PARSER_CHUNK PARSER_CHUNK PARSER_CHUNK PARSER_CHUNK PARSER_CHUNK
	"0\r\n"
	"\r\n";

// the requests of a connection with pipelining: the small one 16 times, one after another
#define PARSER_PIPELINE_REQUESTS			16
#define PARSER_PIPELINE_SIZE				(PARSER_PIPELINE_REQUESTS * (sizeof (parser_small) - 1))

static char parser_pipeline[PARSER_PIPELINE_SIZE];

static void parser_pipeline_fill (void) {

	for (size_t i = 0; i < PARSER_PIPELINE_REQUESTS; i++) {
		(void) memcpy (parser_pipeline + (i * (sizeof (parser_small) - 1)), parser_small, sizeof (parser_small) - 1);
	}

}

typedef struct ParserCase {

	const char *name;
	const char *request;
	size_t size;

	// the requests of the text one after another, or a single request
	bool pipeline;

	// the headers of a single request, or the requests of a pipeline, that every implementation has to find
	int expected;

} ParserCase;

static const ParserCase parser_cases[] = {
	{ "small", parser_small, sizeof (parser_small) - 1, false, 1 },
	{ "browser", parser_browser, sizeof (parser_browser) - 1, false, 10 },
	{ "post", parser_post, sizeof (parser_post) - 1, false, 4 },
	{ "cookies", parser_cookies, sizeof (parser_cookies) - 1, false, 5 },
	{ "chunked", parser_chunked, sizeof (parser_chunked) - 1, false, 3 },
	{ "pipeline", parser_pipeline, PARSER_PIPELINE_SIZE, true, PARSER_PIPELINE_REQUESTS }
};

static int parser_case_parse (const ParserCase *parser_case) {

	return parser_case->pipeline ?
		parser_backend_pipeline (parser_case->request, parser_case->size) :
		parser_backend_parse (parser_case->request, parser_case->size);

}

static bool parser_work (void *data) {

	const ParserCase *parser_case = (const ParserCase *) data;

	return (parser_case_parse (parser_case) == parser_case->expected);

}

static bool parser_measure (const ParserCase *parser_case) {

	const int found = parser_case_parse (parser_case);
	bool succeeded = (found == parser_case->expected);
	if (!succeeded) {
		(void) fprintf (
			stderr, "%s found %d in the %s case, %d expected\n",
			parser_backend_name, found, parser_case->name, parser_case->expected
		);
	}

	MeasureResult result;
	if (succeeded) {
		succeeded = measure_run (parser_work, (void *) parser_case, parser_case->size, &result);
		if (succeeded) measure_print (parser_module, parser_case->name, parser_backend_name, "parse", &result);
	}

	return succeeded;

}

int main (void) {

	bool succeeded = true;

	parser_pipeline_fill ();

	const size_t count = sizeof (parser_cases) / sizeof (ParserCase);
	for (size_t i = 0; succeeded && (i < count); i++) {
		succeeded = parser_measure (&parser_cases[i]);
	}

	return succeeded ? 0 : 1;

}
