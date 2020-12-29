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