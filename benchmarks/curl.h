#ifndef _CERVER_BENCH_CURL_H_
#define _CERVER_BENCH_CURL_H_

#include <stddef.h>

#include <curl/curl.h>

typedef size_t (*curl_write_data_cb)(
	void *contents, size_t size, size_t nmemb, void *storage
);

// performs a simple curl request to the specified address
// creates an destroy a local CURL structure
// returns 0 on success, 1 on any error
extern unsigned int curl_simple_full (
	const char *address
);

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern unsigned int curl_simple (
	CURL *curl, const char *address
);

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern unsigned int curl_simple_handle_data (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer
);

#endif