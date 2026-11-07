#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/collections/arena.h>

#include <cerver/http/compression.h>

#include "../test.h"

// the first bytes of a gzip member (RFC 1952)
#define TEST_COMPRESSION_GZIP_MAGIC_FIRST		0x1f
#define TEST_COMPRESSION_GZIP_MAGIC_SECOND		0x8b

#define TEST_COMPRESSION_BODY_SIZE				(1 << 14)

static bool test_accepts (const char *accept_encoding) {

	return http_compression_accepts_gzip (accept_encoding, strlen (accept_encoding));

}

static void test_compression_accepts_gzip (void) {

	test_check_true (test_accepts ("gzip"));
	test_check_true (test_accepts ("GZIP"));
	test_check_true (test_accepts ("x-gzip"));
	test_check_true (test_accepts ("gzip, deflate, br"));
	test_check_true (test_accepts ("br;q=1.0, gzip;q=0.8, *;q=0.1"));
	test_check_true (test_accepts (" deflate ,gzip ; q=0.5 "));
	test_check_true (test_accepts ("*"));
	test_check_true (test_accepts ("identity, *;q=0.5"));

	test_check_false (test_accepts (""));
	test_check_false (test_accepts ("identity"));
	test_check_false (test_accepts ("deflate, br"));
	test_check_false (test_accepts ("gzip;q=0"));
	test_check_false (test_accepts ("gzip;q=0.000"));
	test_check_false (test_accepts ("*;q=0"));
	test_check_false (test_accepts ("gzipped"));

	// a coding named explicitly takes precedence over "*"
	test_check_false (test_accepts ("gzip;q=0, *"));
	test_check_true (test_accepts ("*;q=0, gzip"));

	test_check_false (http_compression_accepts_gzip (NULL, 0));

}

static bool test_compressible (const char *content_type) {

	return http_compression_is_compressible (content_type, strlen (content_type));

}

static void test_compression_is_compressible (void) {

	test_check_true (test_compressible ("text/html"));
	test_check_true (test_compressible ("text/plain; charset=UTF-8"));
	test_check_true (test_compressible ("application/json"));
	test_check_true (test_compressible ("Application/JSON; charset=utf-8"));
	test_check_true (test_compressible ("application/javascript"));
	test_check_true (test_compressible ("application/xml"));
	test_check_true (test_compressible ("image/svg+xml"));
	test_check_true (test_compressible ("application/problem+json"));
	test_check_true (test_compressible ("application/atom+xml ; charset=utf-8"));

	test_check_false (test_compressible ("image/png"));
	test_check_false (test_compressible ("application/octet-stream"));
	test_check_false (test_compressible ("video/mp4"));
	test_check_false (test_compressible ("application/zip"));
	test_check_false (test_compressible (""));

	test_check_false (http_compression_is_compressible (NULL, 0));

}

static void test_compression_gzip (void) {

	char *body = (char *) malloc (TEST_COMPRESSION_BODY_SIZE);
	test_check_ptr (body);
	for (size_t i = 0; i < TEST_COMPRESSION_BODY_SIZE; i++) body[i] = (char) ('a' + (i % 8));

	Arena *arena = arena_create (0);
	test_check_ptr (arena);

	void *compressed = NULL;
	size_t compressed_length = 0;
	const bool done = http_compression_gzip (
		arena, body, TEST_COMPRESSION_BODY_SIZE, HTTP_COMPRESSION_DEFAULT_LEVEL,
		&compressed, &compressed_length
	);

	if (http_compression_available ()) {
		test_check_true (done);
		test_check_ptr (compressed);
		test_check (compressed_length < TEST_COMPRESSION_BODY_SIZE, "the body was not compressed");

		const unsigned char *bytes = (const unsigned char *) compressed;
		test_check_unsigned_eq (bytes[0], TEST_COMPRESSION_GZIP_MAGIC_FIRST, NULL);
		test_check_unsigned_eq (bytes[1], TEST_COMPRESSION_GZIP_MAGIC_SECOND, NULL);
	}

	// without zlib nothing is compressed
	else {
		test_check_false (done);
	}

	// nothing to compress
	test_check_false (http_compression_gzip (
		arena, body, 0, HTTP_COMPRESSION_DEFAULT_LEVEL, &compressed, &compressed_length
	));

	test_check_false (http_compression_gzip (
		NULL, body, TEST_COMPRESSION_BODY_SIZE, HTTP_COMPRESSION_DEFAULT_LEVEL, &compressed, &compressed_length
	));

	arena_delete (arena);
	free (body);

}

void http_tests_compression (void) {

	(void) printf ("Testing HTTP compression (%s zlib)...\n", http_compression_available () ? "with" : "without");

	test_compression_accepts_gzip ();

	test_compression_is_compressible ();

	test_compression_gzip ();

	(void) printf ("Done!\n");

}
