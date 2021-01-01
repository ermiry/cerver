#include <stdio.h>

#include <curl/curl.h>

#include <cerver/utils/log.h>

#include "curl.h"

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
			"curl_simple_full () failed: %s\n",
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
			"curl_simple_full () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

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
			"curl_easy_perform () failed: %s\n",
			curl_easy_strerror (res)
		);
	}

	curl_mime_free (form);

	return retval;

}