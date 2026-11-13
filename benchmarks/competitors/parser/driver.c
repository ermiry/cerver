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

typedef struct ParserCase {

	const char *name;
	const char *request;
	size_t size;

	// the headers that every implementation has to find
	int headers;

} ParserCase;

static const ParserCase parser_cases[] = {
	{ "small", parser_small, sizeof (parser_small) - 1, 1 },
	{ "browser", parser_browser, sizeof (parser_browser) - 1, 10 },
	{ "post", parser_post, sizeof (parser_post) - 1, 4 }
};

static bool parser_work (void *data) {

	const ParserCase *parser_case = (const ParserCase *) data;

	return (parser_backend_parse (parser_case->request, parser_case->size) == parser_case->headers);

}

static bool parser_measure (const ParserCase *parser_case) {

	const int headers = parser_backend_parse (parser_case->request, parser_case->size);
	bool succeeded = (headers == parser_case->headers);
	if (!succeeded) {
		(void) fprintf (
			stderr, "%s found %d headers in the %s request, %d expected\n",
			parser_backend_name, headers, parser_case->name, parser_case->headers
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

	const size_t count = sizeof (parser_cases) / sizeof (ParserCase);
	for (size_t i = 0; succeeded && (i < count); i++) {
		succeeded = parser_measure (&parser_cases[i]);
	}

	return succeeded ? 0 : 1;

}
