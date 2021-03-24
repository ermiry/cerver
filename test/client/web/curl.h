#ifndef _CERVER_TESTS_WEB_CURL_H_
#define _CERVER_TESTS_WEB_CURL_H_

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

// works like curl_simple () but adds a custom auth header
// returns 0 on success, 1 on any error
extern unsigned int curl_simple_with_auth (
	CURL *curl, const char *address,
	const char *authorization
);

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern unsigned int curl_simple_handle_data (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer
);

// performs a POST with application/x-www-form-urlencoded data
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern unsigned int curl_simple_post (
	CURL *curl, const char *address,
	const char *data, const size_t datalen
);

// works like curl_simple_post ()
// but sets a custom Authorization header
extern unsigned int curl_simple_post_with_auth (
	CURL *curl, const char *address,
	const char *data, const size_t datalen,
	const char *authorization
);

// works as curl_simple_post ()
// but has the option to handle the result data
extern unsigned int curl_simple_post_handle_data (
	CURL *curl, const char *address,
	const char *data, const size_t datalen,
	curl_write_data_cb write_cb, char *buffer
);

// performs a multi-part request with just one value
// returns 0 on success, 1 on error
extern unsigned int curl_post_form_value (
	CURL *curl, const char *address,
	const char *key, const char *value
);

// uploads a file to the requested route performing a multi-part request
// returns 0 on success, 1 on error
extern unsigned int curl_upload_file (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename
);

// uploads a file to the requested route performing a multi-part request
// and also adds another value to the request
// returns 0 on success, 1 on error
extern unsigned int curl_upload_file_with_extra_value (
	CURL *curl, const char *address,
	const char *filename,
	const char *key, const char *value
);

#endif