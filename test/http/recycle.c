#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include <pthread.h>

#include <cerver/version.h>

#include <cerver/http/headers.h>
#include <cerver/http/http.h>
#include <cerver/http/recycle.h>
#include <cerver/http/response.h>
#include <cerver/http/status.h>

#include "../test.h"

#include "http.h"

// the responses and the buffers of the bodies that the threads reuse:
// a reused one never keeps what its previous request left in it

#define TEST_RECYCLE_SMALL_SIZE				1
#define TEST_RECYCLE_CLASS_SIZE				((size_t) 1 << HTTP_RECYCLE_BUFFER_MIN_EXPONENT)
#define TEST_RECYCLE_LARGEST_SIZE			((size_t) 1 << HTTP_RECYCLE_BUFFER_MAX_EXPONENT)
// smaller than the smallest class: kept in it, the class would be written beyond it
#define TEST_RECYCLE_INEXACT_CAPACITY		200

#define TEST_RECYCLE_THREADS				4
#define TEST_RECYCLE_ROUNDS					256
#define TEST_RECYCLE_THREAD_BUFFER_SIZE		1000

#define TEST_RECYCLE_HEADER_VALUE			"x-recycle-test"
#define TEST_RECYCLE_JSON_KEY				"key"
#define TEST_RECYCLE_JSON_VALUE				"value"

// recycling is off in the builds with sanitizers
static const bool test_recycle_enabled = (HTTP_RECYCLE_LIMIT > 0);

static HttpCerver *test_recycle_cerver = NULL;

static void test_recycle_buffer_check (const size_t size, const size_t expected_capacity) {

	size_t capacity = 0;
	void *buffer = http_recycle_buffer_take (size, &capacity);

	test_check_ptr (buffer);
	test_check_unsigned_eq (capacity, expected_capacity, NULL);

	// the whole capacity can be written
	(void) memset (buffer, 0, capacity);

	http_recycle_buffer_give_back (buffer, capacity);

}

// every size takes the smallest class that fits it, a bigger one its exact size
static void test_recycle_buffer_classes (void) {

	test_recycle_buffer_check (TEST_RECYCLE_SMALL_SIZE, TEST_RECYCLE_CLASS_SIZE);
	test_recycle_buffer_check (TEST_RECYCLE_CLASS_SIZE, TEST_RECYCLE_CLASS_SIZE);
	test_recycle_buffer_check (TEST_RECYCLE_CLASS_SIZE + 1, TEST_RECYCLE_CLASS_SIZE << 1);
	test_recycle_buffer_check (TEST_RECYCLE_CLASS_SIZE << 1, TEST_RECYCLE_CLASS_SIZE << 1);
	test_recycle_buffer_check (TEST_RECYCLE_LARGEST_SIZE, TEST_RECYCLE_LARGEST_SIZE);
	test_recycle_buffer_check (TEST_RECYCLE_LARGEST_SIZE + 1, TEST_RECYCLE_LARGEST_SIZE + 1);

}

// a buffer of a class that is given back is the next one of its class
static void test_recycle_buffer_reused (void) {

	size_t capacity = 0;
	void *first = http_recycle_buffer_take (TEST_RECYCLE_CLASS_SIZE, &capacity);
	test_check_ptr (first);

	http_recycle_buffer_give_back (first, capacity);

	void *second = http_recycle_buffer_take (TEST_RECYCLE_SMALL_SIZE, &capacity);
	test_check_ptr (second);

	if (test_recycle_enabled) test_check_ptr_eq (first, second);

	http_recycle_buffer_give_back (second, capacity);

}

// a buffer whose capacity is not a class is released, not kept in a class it does not fill
static void test_recycle_buffer_inexact (void) {

	void *inexact = malloc (TEST_RECYCLE_INEXACT_CAPACITY);
	test_check_ptr (inexact);

	http_recycle_buffer_give_back (inexact, TEST_RECYCLE_INEXACT_CAPACITY);

	size_t capacity = 0;
	void *buffer = http_recycle_buffer_take (TEST_RECYCLE_INEXACT_CAPACITY, &capacity);
	test_check_ptr (buffer);
	test_check_unsigned_eq (capacity, TEST_RECYCLE_CLASS_SIZE, NULL);

	// the whole class can be written, valgrind reports it if it is the smaller buffer
	(void) memset (buffer, 0, capacity);

	http_recycle_buffer_give_back (buffer, capacity);

}

static void test_recycle_response_check_empty (const HttpResponse *response) {

	test_check_unsigned_eq (response->status, HTTP_STATUS_OK, NULL);
	test_check_unsigned_eq (response->n_headers, 0, NULL);

	for (unsigned int index = 0; index < HTTP_HEADERS_SIZE; index++) {
		test_check_int_eq (response->headers[index].len, 0, NULL);
		test_check_str_empty (response->headers[index].value);
	}

	test_check_null_ptr (response->header);
	test_check_unsigned_eq (response->header_len, 0, NULL);
	test_check_str_empty (response->header_buffer);

	test_check_null_ptr (response->data);
	test_check_unsigned_eq (response->data_len, 0, NULL);
	test_check_bool_eq (response->data_ref, false, NULL);

	test_check_null_ptr (response->res);
	test_check_unsigned_eq (response->res_len, 0, NULL);

	test_check_null_ptr (response->recycle_next);

}

// a reused response has none of the headers, header or body of the previous one
static void test_recycle_response_reused (void) {

	HttpResponse *first = (HttpResponse *) http_response_new ();
	test_check_ptr (first);

	first->status = HTTP_STATUS_NOT_FOUND;
	test_check_unsigned_eq (http_response_add_header (first, HTTP_HEADER_ETAG, TEST_RECYCLE_HEADER_VALUE), 0, NULL);
	http_response_add_json_headers (first, strlen (TEST_RECYCLE_JSON_VALUE));
	first->data = strdup (TEST_RECYCLE_JSON_VALUE);
	first->data_len = strlen (TEST_RECYCLE_JSON_VALUE);
	test_check_unsigned_eq (http_response_compile (first), 0, NULL);

	http_response_delete (first);

	HttpResponse *second = (HttpResponse *) http_response_new ();
	test_check_ptr (second);

	if (test_recycle_enabled) test_check_ptr_eq (first, second);

	test_recycle_response_check_empty (second);

	http_response_delete (second);

}

// the header of a json response is written inside it,
// and a header that does not fit there is allocated
static void test_recycle_response_header_buffer (void) {

	HttpResponse *response = http_response_json_key_value (
		HTTP_STATUS_OK, TEST_RECYCLE_JSON_KEY, TEST_RECYCLE_JSON_VALUE
	);

	test_check_ptr (response);
	test_check_ptr_eq (response->header, (void *) response->header_buffer);
	test_check_unsigned_eq (response->header_len, strlen (response->header_buffer), NULL);
	test_check_true (strstr (response->header_buffer, "Content-Type: application/json\r\n") != NULL);
	test_check_true (strstr (response->header_buffer, "\r\n\r\n") != NULL);

	http_response_delete (response);

	HttpResponse *large = (HttpResponse *) http_response_new ();
	test_check_ptr (large);

	char value[HTTP_HEADER_VALUE_SIZE] = { 0 };
	(void) memset (value, 'v', HTTP_HEADER_VALUE_SIZE >> 1);

	test_check_unsigned_eq (http_response_add_header (large, HTTP_HEADER_ETAG, value), 0, NULL);
	test_check_unsigned_eq (http_response_add_header (large, HTTP_HEADER_LOCATION, value), 0, NULL);
	http_response_compile_header (large);

	test_check_ptr (large->header);
	test_check_ptr_ne (large->header, (void *) large->header_buffer);
	test_check_unsigned_eq (large->header_len, strlen ((const char *) large->header), NULL);
	test_check_unsigned_gt (large->header_len, HTTP_RESPONSE_HEADER_BUFFER_SIZE);

	// the allocated header is released, ASan and valgrind report it if it is not
	http_response_delete (large);

}

// threads that take and give back responses and buffers at the same time,
// and end with them in their caches
static void *test_recycle_thread (void *data) {

	(void) data;

	for (unsigned int round = 0; round < TEST_RECYCLE_ROUNDS; round++) {
		HttpResponse *response = (HttpResponse *) http_response_new ();
		size_t capacity = 0;
		void *buffer = http_recycle_buffer_take (TEST_RECYCLE_THREAD_BUFFER_SIZE, &capacity);

		if (response) {
			(void) http_response_add_header (response, HTTP_HEADER_ETAG, TEST_RECYCLE_HEADER_VALUE);
			http_response_delete (response);
		}

		if (buffer) {
			(void) memset (buffer, 0, capacity);
			http_recycle_buffer_give_back (buffer, capacity);
		}
	}

	return NULL;

}

static void test_recycle_threads (void) {

	pthread_t threads[TEST_RECYCLE_THREADS];

	for (unsigned int index = 0; index < TEST_RECYCLE_THREADS; index++) {
		test_check_int_eq (pthread_create (&threads[index], NULL, test_recycle_thread, NULL), 0, NULL);
	}

	for (unsigned int index = 0; index < TEST_RECYCLE_THREADS; index++) {
		test_check_int_eq (pthread_join (threads[index], NULL), 0, NULL);
	}

	// what the threads left in the shared lists is reused here
	HttpResponse *response = (HttpResponse *) http_response_new ();
	test_check_ptr (response);
	test_recycle_response_check_empty (response);
	http_response_delete (response);

}

void http_tests_recycle (void) {

	(void) printf ("Testing HTTP recycle...\n");

	test_recycle_cerver = http_cerver_new ();
	test_check_ptr (test_recycle_cerver);
	test_check_unsigned_eq (http_responses_init (test_recycle_cerver), 0, NULL);

	test_recycle_buffer_classes ();
	test_recycle_buffer_reused ();
	test_recycle_buffer_inexact ();

	test_recycle_response_reused ();
	test_recycle_response_header_buffer ();
	test_recycle_threads ();

	http_responses_end ();
	http_cerver_delete (test_recycle_cerver);
	test_recycle_cerver = NULL;

	(void) printf ("Done!\n");

}
