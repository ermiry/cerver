#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <curl/curl.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#include "../../address.h"

#define AUTHORIZATION_HEADER_SIZE		1024

#define CURL_REQUEST_TIMEOUT_SECONDS	32L
#define CURL_CONNECT_TIMEOUT_SECONDS	8L

// an IPv6 address has a ':', in a URL it goes between brackets
#define WEB_TEST_IPV6_SEPARATOR			':'

// writes the host (CERVER_TEST_ADDRESS, an IPv6 address between brackets),
// the port and the path (NULL for none) of the web cerver of a pair
void web_test_address (char *buffer, const char *path) {

	const char *host = test_address ();
	const char *suffix = path ? path : "";

	if (strchr (host, WEB_TEST_IPV6_SEPARATOR)) {
		(void) snprintf (buffer, WEB_TEST_ADDRESS_SIZE, "[%s]:%d%s", host, WEB_TEST_PORT, suffix);
	}

	else {
		(void) snprintf (buffer, WEB_TEST_ADDRESS_SIZE, "%s:%d%s", host, WEB_TEST_PORT, suffix);
	}

}

// performs the request with custom headers
// and removes them from the handle before releasing them
static CurlResult curl_perform_request_with_headers (
	CURL *curl, const http_status expected_status,
	struct curl_slist *headers
) {

	curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

	CurlResult result = curl_perform_request (curl, expected_status);

	curl_easy_setopt (curl, CURLOPT_HTTPHEADER, NULL);
	curl_slist_free_all (headers);

	return result;

}

CurlResult curl_perform_request (
	CURL *curl, const http_status expected_status
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_easy_setopt (curl, CURLOPT_TIMEOUT, CURL_REQUEST_TIMEOUT_SECONDS);
	curl_easy_setopt (curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT_SECONDS);

	CURLcode res = curl_easy_perform (curl);

	long http_code = 0;
	curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);

	if (res != CURLE_OK) {
		cerver_log_error (
			"curl_perform_request () failed: %s\n",
			curl_easy_strerror (res)
		);

		result = CURL_RESULT_FAILED;
	}

	else if ((long) expected_status != http_code) {
		cerver_log_error (
			"curl_perform_request () expected status %u but got %ld instead!",
			(unsigned int) expected_status, http_code
		);

		result = CURL_RESULT_BAD_STATUS;
	}

	return result;

}

// performs a simple curl request to the specified address
// creates an destroys a local CURL structure
// returns 0 on success, 1 on any error
unsigned int curl_simple_full (
	const char *address,
	const http_status expected_status
) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		curl_easy_setopt (curl, CURLOPT_URL, address);

		// perfrom the request
		retval = curl_perform_request (curl, expected_status);

		curl_easy_cleanup (curl);
	}

	return retval;

}

// works like curl_simple_full () but handles the data
// returns 0 on success, 1 on any error
unsigned int curl_full_handle_data (
	const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer
) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		curl_easy_setopt (curl, CURLOPT_URL, address);

		curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
		curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

		// perfrom the request
		retval = curl_perform_request (curl, expected_status);

		curl_easy_cleanup (curl);
	}

	return retval;

}

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
CurlResult curl_simple (
	CURL *curl, const char *address,
	const http_status expected_status
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	// perfrom the request
	result = curl_perform_request (curl, expected_status);

	return result;

}

// works like curl_simple () but adds a custom auth header
// returns 0 on success, 1 on any error
CurlResult curl_simple_with_auth (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *authorization
) {

	CurlResult result = CURL_RESULT_NONE;

	// add custom Authorization header
	struct curl_slist *headers = NULL;
	char auth_header[AUTHORIZATION_HEADER_SIZE] = { 0 };
	(void) snprintf (
		auth_header, AUTHORIZATION_HEADER_SIZE - 1,
		"Authorization: %s", authorization
	);

	headers = curl_slist_append (headers, auth_header);

	curl_easy_setopt (curl, CURLOPT_URL, address);
	curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "GET");

	// perfrom the request
	result = curl_perform_request_with_headers (curl, expected_status, headers);

	return result;

}

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
CurlResult curl_simple_handle_data (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	// perfrom the request
	result = curl_perform_request (curl, expected_status);

	return result;

}

// performs a POST with application/x-www-form-urlencoded data
// uses an already created CURL structure
// returns 0 on success, 1 on any error
CurlResult curl_simple_post (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *data, const size_t datalen
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	// the data is copied, because the handle is reused after the caller releases it
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, datalen);
	curl_easy_setopt (curl, CURLOPT_COPYPOSTFIELDS, data);

	// perfrom the request
	result = curl_perform_request (curl, expected_status);

	return result;

}

// posts a JSON to the specified address
// uses an already created CURL structure
CurlResult curl_simple_post_json (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *json, const size_t json_len,
	curl_write_data_cb write_cb, char *buffer
) {

	CurlResult result = CURL_RESULT_NONE;

	struct curl_slist *headers = NULL;
	headers = curl_slist_append (headers, "Accept: application/json");
	headers = curl_slist_append (headers, "Content-Type: application/json");

	curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "POST");
	// the json is copied, curl sends its length as the Content-Length
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, (long) json_len);
	curl_easy_setopt (curl, CURLOPT_COPYPOSTFIELDS, json);

	// set the destination address
	curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	// perfrom the request
	result = curl_perform_request_with_headers (curl, expected_status, headers);

	return result;

}

// works like curl_simple_post ()
// but sets a custom Authorization header
CurlResult curl_simple_post_with_auth (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *data, const size_t datalen,
	const char *authorization
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	// add custom Authorization header
	struct curl_slist *headers = NULL;
	char auth_header[AUTHORIZATION_HEADER_SIZE] = { 0 };
	(void) snprintf (
		auth_header, AUTHORIZATION_HEADER_SIZE - 1,
		"Authorization: %s", authorization
	);

	headers = curl_slist_append (headers, auth_header);

	// the data is copied, because the handle is reused after the caller releases it
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, datalen);
	curl_easy_setopt (curl, CURLOPT_COPYPOSTFIELDS, data);

	// perfrom the request
	result = curl_perform_request_with_headers (curl, expected_status, headers);

	return result;

}

// works like curl_simple_post ()
// but has the option to handle the result data
CurlResult curl_simple_post_handle_data (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *data, const size_t datalen,
	curl_write_data_cb write_cb, char *buffer
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	// the data is copied, because the handle is reused after the caller releases it
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, datalen);
	curl_easy_setopt (curl, CURLOPT_COPYPOSTFIELDS, data);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	// perfrom the request
	result = curl_perform_request (curl, expected_status);

	return result;

}

// performs a multi-part request with just one value
// returns 0 on success, 1 on error
CurlResult curl_post_form_value (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *key, const char *value
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_mimepart *field = NULL;

	curl_mime *form = curl_mime_init (curl);

	/* Fill in the filename field */
	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, key);
	(void) curl_mime_data (field, value, CURL_ZERO_TERMINATED);

	/* what URL that receives this POST */
	(void) curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	(void) curl_easy_setopt (curl, CURLOPT_MIMEPOST, form);

	result = curl_perform_request (curl, expected_status);

	curl_mime_free (form);

	return result;

}

// performs a multi-part request with two values
// returns 0 on success, 1 on error
CurlResult curl_post_two_form_values (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *first_key, const char *first_value,
	const char *second_key, const char *second_value
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_mimepart *field = NULL;

	curl_mime *form = curl_mime_init (curl);

	// add the first value
	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, first_key);
	(void) curl_mime_data (field, first_value, CURL_ZERO_TERMINATED);

	// add the second value
	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, second_key);
	(void) curl_mime_data (field, second_value, CURL_ZERO_TERMINATED);

	/* what URL that receives this POST */
	(void) curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	(void) curl_easy_setopt (curl, CURLOPT_MIMEPOST, form);

	result = curl_perform_request (curl, expected_status);

	curl_mime_free (form);

	return result;

}

// uploads a file to the requested route performing a multi-part request
// returns 0 on success, 1 on error
CurlResult curl_upload_file (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename
) {

	CurlResult result = CURL_RESULT_NONE;

	// create the form
	curl_mime *form = curl_mime_init (curl);

	// fill in the file upload field
	curl_mimepart *field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "file");
	(void) curl_mime_filedata (field, filename);

	/* what URL that receives this POST */
	(void) curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	(void) curl_easy_setopt (curl, CURLOPT_MIMEPOST, form);

	result = curl_perform_request (curl, expected_status);

	curl_mime_free (form);

	return result;

}

// uploads two files in the same multi-part request
// returns 0 on success, 1 on error
CurlResult curl_upload_two_files (
	CURL *curl, const char *address,
	const http_status expected_status,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename_one, const char *filename_two
) {

	CurlResult result = CURL_RESULT_NONE;

	// create the form
	curl_mime *form = curl_mime_init (curl);

	curl_mimepart *field = NULL;

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "one");
	(void) curl_mime_filedata (field, filename_one);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "two");
	(void) curl_mime_filedata (field, filename_two);

	/* what URL that receives this POST */
	(void) curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	(void) curl_easy_setopt (curl, CURLOPT_MIMEPOST, form);

	result = curl_perform_request (curl, expected_status);

	curl_mime_free (form);

	return result;

}

// uploads a file to the requested route performing a multi-part request
// and also adds another value to the request
// returns 0 on success, 1 on error
CurlResult curl_upload_file_with_extra_value (
	CURL *curl, const char *address,
	const http_status expected_status,
	const char *filename,
	const char *key, const char *value
) {

	CurlResult result = CURL_RESULT_NONE;

	curl_mimepart *field = NULL;

	curl_mime *form = curl_mime_init (curl);

	/* Fill in the file upload field */
	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "file");
	(void) curl_mime_filedata (field, filename);

	/* Fill in the filename field */
	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, key);
	(void) curl_mime_data (field, value, CURL_ZERO_TERMINATED);

	/* what URL that receives this POST */
	(void) curl_easy_setopt (curl, CURLOPT_URL, address);

	(void) curl_easy_setopt (curl, CURLOPT_MIMEPOST, form);

	result = curl_perform_request (curl, expected_status);

	curl_mime_free (form);

	return result;

}
