#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <assert.h>
#include <stdint.h>

#include <sys/time.h>

#include <cerver/http/http_parser.h>

/* 8 gb */
static const int64_t kBytes = 8LL << 30;

static const char data[] =
	"POST /joyent/http-parser HTTP/1.1\r\n"
	"Host: github.com\r\n"
	"DNT: 1\r\n"
	"Accept-Encoding: gzip, deflate, sdch\r\n"
	"Accept-Language: ru-RU,ru;q=0.8,en-US;q=0.6,en;q=0.4\r\n"
	"User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_10_1) "
		"AppleWebKit/537.36 (KHTML, like Gecko) "
		"Chrome/39.0.2171.65 Safari/537.36\r\n"
	"Accept: text/html,application/xhtml+xml,application/xml;q=0.9,"
		"image/webp,*/*;q=0.8\r\n"
	"Referer: https://github.com/joyent/http-parser\r\n"
	"Connection: keep-alive\r\n"
	"Transfer-Encoding: chunked\r\n"
	"Cache-Control: max-age=0\r\n\r\nb\r\nhello world\r\n0\r\n";

static const size_t data_len = sizeof(data) - 1;

static int on_info (http_parser *p) {

	return 0;
}


static int on_data (
	http_parser* p, const char *at, size_t length
) {

	return 0;

}

static http_parser_settings settings = {

	.on_message_begin = on_info,
	.on_headers_complete = on_info,
	.on_message_complete = on_info,
	.on_header_field = on_data,
	.on_header_value = on_data,
	.on_url = on_data,
	.on_status = on_data,
	.on_body = on_data

};

int bench (int iter_count, int silent) {

	struct http_parser parser = { 0 };
	int err = 0;
	struct timeval start = { 0 };
	struct timeval end = { 0 };

	if (!silent) {
		err = gettimeofday (&start, NULL);
		assert (err == 0);
	}

	(void) fprintf (stderr, "req_len=%d\n", (int) data_len);
	size_t parsed = 0;
	register int i = 0;
	for (i = 0; i < iter_count; i++) {
		http_parser_init (&parser, HTTP_REQUEST);

		parsed = http_parser_execute (&parser, &settings, data, data_len);
		assert (parsed == data_len);
	}

	if (!silent) {
		double elapsed = 0.0;
		double bw = 0.0;
		double total = 0.0;

		err = gettimeofday (&end, NULL);
		assert (err == 0);

		(void) fprintf (stdout, "Benchmark result:\n");

		elapsed = (double) (end.tv_sec - start.tv_sec) +
			(end.tv_usec - start.tv_usec) * 1e-6f;

		total = (double) iter_count * data_len;
		bw = (double) total / elapsed;

		(void) fprintf (
			stdout,
			"%.2f mb | %.2f mb/s | %.2f req/sec | %.2f s\n",
			(double) total / (1024 * 1024),
			bw / (1024 * 1024),
			(double) iter_count / elapsed,
			elapsed
		);

		(void) fflush (stdout);
	}

	return 0;
}

int main (int argc, char **argv) {

	int64_t iterations = kBytes / (int64_t) data_len;

	if (argc == 2 && strcmp (argv[1], "infinite") == 0) {
		for (;;) bench (iterations, 1);
		return 0;
	}

	return bench (iterations, 0);

}