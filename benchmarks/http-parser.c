#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <assert.h>
#include <stdint.h>

#include <sys/time.h>

#include <cerver/http/parser/api.h>

// 8 gb
static const int64_t kBytes = 8LL << 30;

static const double bench_megabyte = 1024.0 * 1024.0;
static const double bench_microseconds = 1e-6;

static const char *bench_infinite = { "infinite" };

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

static const size_t data_len = sizeof (data) - 1;

static int on_info (llhttp_t *p) {

	(void) p;

	return 0;

}

static int on_data (
	llhttp_t *p, const char *at, size_t length
) {

	(void) p;
	(void) at;
	(void) length;

	return 0;

}

// the callbacks of the parser of cerver: the spans and their complete callbacks
static void bench_settings_init (llhttp_settings_t *settings) {

	llhttp_settings_init (settings);

	settings->on_message_begin = on_info;
	settings->on_url = on_data;
	settings->on_url_complete = on_info;
	settings->on_header_field = on_data;
	settings->on_header_value = on_data;
	settings->on_header_value_complete = on_info;
	settings->on_headers_complete = on_info;
	settings->on_body = on_data;
	settings->on_message_complete = on_info;

}

static int bench (int64_t iter_count, int silent) {

	llhttp_settings_t settings;
	bench_settings_init (&settings);

	llhttp_t parser;
	struct timeval start = { 0 };
	struct timeval end = { 0 };

	if (!silent) {
		const int err = gettimeofday (&start, NULL);
		assert (err == 0);
		(void) err;
	}

	(void) fprintf (stderr, "req_len=%d\n", (int) data_len);
	for (int64_t i = 0; i < iter_count; i++) {
		llhttp_init (&parser, HTTP_REQUEST, &settings);

		const llhttp_errno_t err = llhttp_execute (&parser, data, data_len);
		assert (err == HPE_OK);
		(void) err;
	}

	if (!silent) {
		const int err = gettimeofday (&end, NULL);
		assert (err == 0);
		(void) err;

		(void) fprintf (stdout, "Benchmark result:\n");

		const double elapsed = (double) (end.tv_sec - start.tv_sec)
			+ ((double) (end.tv_usec - start.tv_usec) * bench_microseconds);

		const double total = (double) iter_count * (double) data_len;
		const double bw = total / elapsed;

		(void) fprintf (
			stdout,
			"%.2f mb | %.2f mb/s | %.2f req/sec | %.2f s\n",
			total / bench_megabyte,
			bw / bench_megabyte,
			(double) iter_count / elapsed,
			elapsed
		);

		(void) fflush (stdout);
	}

	return 0;

}

int main (int argc, char **argv) {

	const int64_t iterations = kBytes / (int64_t) data_len;

	if ((argc == 2) && !strcmp (argv[1], bench_infinite)) {
		for (;;) (void) bench (iterations, 1);
	}

	return bench (iterations, 0);

}
