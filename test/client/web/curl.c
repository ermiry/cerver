#include <stdio.h>

#include <curl/curl.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define AUTH_HEADER_SIZE		1024

static CurlResult curl_perform_request (
	CURL *curl, const unsigned int expected_status
) {

	CurlResult result = CURL_RESULT_NONE;

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

	else if (expected_status != (unsigned int) http_code) {
		cerver_log_error (
			"curl_perform_request () expected status %u but got %u instead!",
			expected_status, (unsigned int) http_code
		);

		result = CURL_RESULT_BAD_STATUS;
	}

	return result;

}

// performs a simple curl request to the specified address
// creates an destroy a local CURL structure
// returns 0 on success, 1 on any error
unsigned int curl_simple_full (
	const char *address
) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		curl_easy_setopt (curl, CURLOPT_URL, address);

		// perfrom the request
		CURLcode res = curl_easy_perform (curl);
		if (res == CURLE_OK) {
			retval = 0;
		}

		else {
			cerver_log_error (
				"curl_simple_full () failed: %s\n",
				curl_easy_strerror (res)
			);
		}

		curl_easy_cleanup (curl);
	}

	return retval;

}

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
unsigned int curl_simple (
	CURL *curl, const char *address
) {

	unsigned int retval = 1;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	// perfrom the request
	CURLcode res = curl_easy_perform (curl);
	if (res == CURLE_OK) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"curl_simple () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	return retval;

}

// works like curl_simple () but adds a custom auth header
// returns 0 on success, 1 on any error
unsigned int curl_simple_with_auth (
	CURL *curl, const char *address,
	const char *authorization
) {

	unsigned int retval = 1;

	// add custom Authorization header
	struct curl_slist *headers = NULL;
	char auth_header[AUTH_HEADER_SIZE] = { 0 };
	(void) snprintf (
		auth_header, AUTH_HEADER_SIZE - 1,
		"Authorization: %s", authorization
	);

	headers = curl_slist_append (headers, auth_header);

	curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt (curl, CURLOPT_URL, address);
	curl_easy_setopt (curl, CURLOPT_CUSTOMREQUEST, "GET");

	// perfrom the request
	CURLcode res = curl_easy_perform (curl);
	if (res == CURLE_OK) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"curl_simple_with_auth () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	return retval;

}

// performs a simple curl request to the specified address
// uses an already created CURL structure
// returns 0 on success, 1 on any error
unsigned int curl_simple_handle_data (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer
) {

	unsigned int retval = 1;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	// perfrom the request
	CURLcode res = curl_easy_perform (curl);
	if (res == CURLE_OK) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"curl_simple_handle_data () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	return retval;

}

// performs a POST with application/x-www-form-urlencoded data
// uses an already created CURL structure
// returns 0 on success, 1 on any error
unsigned int curl_simple_post (
	CURL *curl, const char *address,
	const char *data, const size_t datalen
) {

	unsigned int retval = 1;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, datalen);

	// perfrom the request
	CURLcode res = curl_easy_perform (curl);
	if (res == CURLE_OK) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"curl_simple_post () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	return retval;

}

// works like curl_simple_post ()
// but sets a custom Authorization header
unsigned int curl_simple_post_with_auth (
	CURL *curl, const char *address,
	const char *data, const size_t datalen,
	const char *authorization
) {

	unsigned int retval = 1;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	// add custom Authorization header
	struct curl_slist *headers = NULL;
	char auth_header[AUTH_HEADER_SIZE] = { 0 };
	(void) snprintf (
		auth_header, AUTH_HEADER_SIZE - 1,
		"Authorization: %s", authorization
	);

	headers = curl_slist_append (headers, auth_header);

	curl_easy_setopt (curl, CURLOPT_HTTPHEADER, headers);

	curl_easy_setopt (curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, datalen);

	// perfrom the request
	CURLcode res = curl_easy_perform (curl);
	if (res == CURLE_OK) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"curl_simple_post_with_auth () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	curl_slist_free_all (headers);

	return retval;

}

// works like curl_simple_post ()
// but has the option to handle the result data
unsigned int curl_simple_post_handle_data (
	CURL *curl, const char *address,
	const char *data, const size_t datalen,
	curl_write_data_cb write_cb, char *buffer
) {

	unsigned int retval = 1;

	curl_easy_setopt (curl, CURLOPT_URL, address);

	curl_easy_setopt (curl, CURLOPT_POSTFIELDS, data);
	curl_easy_setopt (curl, CURLOPT_POSTFIELDSIZE, datalen);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, write_cb);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, buffer);

	// perfrom the request
	CURLcode res = curl_easy_perform (curl);
	if (res == CURLE_OK) {
		retval = 0;
	}

	else {
		cerver_log_error (
			"curl_simple_post_handle_data () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	return retval;

}

// performs a multi-part request with just one value
// returns 0 on success, 1 on error
unsigned int curl_post_form_value (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer,
	const char *key, const char *value,
	const unsigned int expected_status
) {

	unsigned int retval = 1;

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

	retval = curl_perform_request (curl, expected_status);

	curl_mime_free (form);

	return retval;

}

// uploads a file to the requested route performing a multi-part request
// returns 0 on success, 1 on error
unsigned int curl_upload_file (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename
) {

	unsigned int retval = 1;

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

	/* Perform the request, res will get the return code */
	CURLcode res = curl_easy_perform (curl);

	if (res == CURLE_OK) retval = 0;
	else {
		cerver_log_error (
			"curl_upload_file () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	curl_mime_free (form);

	return retval;

}

// uploads two files in the same multi-part request
// returns 0 on success, 1 on error
unsigned int curl_upload_two_files (
	CURL *curl, const char *address,
	curl_write_data_cb write_cb, char *buffer,
	const char *filename_one, const char *filename_two
) {

	unsigned int retval = 1;

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

	/* Perform the request, res will get the return code */
	CURLcode res = curl_easy_perform (curl);

	if (res == CURLE_OK) retval = 0;
	else {
		cerver_log_error (
			"curl_upload_two_files () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	curl_mime_free (form);

	return retval;

}

// uploads a file to the requested route performing a multi-part request
// and also adds another value to the request
// returns 0 on success, 1 on error
unsigned int curl_upload_file_with_extra_value (
	CURL *curl, const char *address,
	const char *filename,
	const char *key, const char *value
) {

	unsigned int retval = 1;

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

	/* Perform the request, res will get the return code */
	CURLcode res = curl_easy_perform (curl);

	if (res == CURLE_OK) retval = 0;
	else {
		cerver_log_error (
			"curl_upload_file_with_extra_value () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	curl_mime_free (form);

	return retval;

}
