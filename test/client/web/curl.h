#ifndef _CERVER_TESTS_WEB_CURL_H_
#define _CERVER_TESTS_WEB_CURL_H_

#include <stddef.h>

#include <curl/curl.h>

#include <cerver/http/status.h>

typedef enum CurlResult {

	CURL_RESULT_NONE			= 0,
	CURL_RESULT_FAILED			= 1,
	CURL_RESULT_BAD_STATUS		= 2

} CurlResult;

typedef size_t (*curl_write_data_cb)(
	void *contents, size_t size, size_t nmemb, void *storage
);

// performs a simple curl request to the specified address
// creates an destroy a local CURL structure
// returns 0 on success, 1 on any error
extern unsigned int curl_simple_full (
	const char *address,
	const http_status expected_status
);

// works like curl_simple_full () but handles the data
// returns 0 on success, 1 on any error
extern unsigned int curl_full_handle_data (
	const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer
);

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern CurlResult curl_simple (
	CURL *curl, const char *address,
	const http_status expected_status
);

// works like curl_simple () but adds a custom auth header
// returns 0 on success, 1 on any error
extern CurlResult curl_simple_with_auth (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *authorization
);

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern CurlResult curl_simple_handle_data (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer
);

// performs a POST with application/x-www-form-urlencoded data
// uses an already created CURL structure
// returns 0 on success, 1 on any error
extern CurlResult curl_simple_post (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *data, const size_t datalen
);

// posts a JSON to the specified address
// uses an already created CURL structure
extern CurlResult curl_simple_post_json (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *json, const size_t json_len,
	curl_write_data_cb write_cb, char *buffer
);

// works like curl_simple_post ()
// but sets a custom Authorization header
extern CurlResult curl_simple_post_with_auth (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *data, const size_t datalen,
	const char *authorization
);

// works like curl_simple_post ()
// but has the option to handle the result data
extern CurlResult curl_simple_post_handle_data (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *data, const size_t datalen,
	curl_write_data_cb write_cb, char *buffer
);

// performs a multi-part request with just one value
// returns 0 on success, 1 on error
extern CurlResult curl_post_form_value (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *key, const char *value
);

// performs a multi-part request with two values
// returns 0 on success, 1 on error
extern CurlResult curl_post_two_form_values (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *first_key, const char *first_value,
	const char *second_key, const char *second_value
);

// uploads a file to the requested route performing a multi-part request
// returns 0 on success, 1 on error
extern CurlResult curl_upload_file (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename
);

// uploads two files in the same multi-part request
// returns 0 on success, 1 on error
extern CurlResult curl_upload_two_files (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename_one, const char *filename_two
);

// uploads a file to the requested route performing a multi-part request
// and also adds another value to the request
// returns 0 on success, 1 on error
extern CurlResult curl_upload_file_with_extra_value (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *filename,
	const char *key, const char *value
);

#endif