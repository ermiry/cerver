#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/http.h>

#include <cerver/http/json/json.h>

#include <cerver/utils/log.h>
#include <cerver/utils/utils.h>

#include "curl.h"

static const char *address = { "127.0.0.1:8080" };

static const char *token = { "Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MjUwNDY4NjksImlkIjoiMTYyNTA0Njg2NyIsIm5hbWUiOiJFcmljayBTYWxhcyIsInJvbGUiOiJjb21tb24iLCJ1c2VybmFtZSI6ImVyaWNrIn0.U3Mf0d4Mob0ufH-UZFJgaY2_k8NvFr_bM9Xq487Vktga32IKSQLhEgqyUsOwOwBnEKhIMnkfpU4hmkJlwnI8FUSqtUd_yP4QVsBBrdBoZyjsbvCCBL5oUoAWuikFOJGnY09lh47EOYkzp2jNAdHBveCg4YMRxsdwAFh0ZuWUEMlbU-mx_YRiSoFjhmhS0A0V3hKwbx835vOU9Gwbq011drMRF-KflR3sVvUUGdpIfSq7HNzasMfSH4TRjpTvuad3pKnh7ZXvOeixD7L3fMOavdX8UdoFnNFiS-SWCxMi0F5lmTX86xBkJ_xIydDRRIQFIEbMWDuzm5pa1zJYi8wlTEqyN_lOZl9Ld-wQ43xJIg1YCTNaG04wSdemEInbpb9VlcY7M56OpxLu0ZFmnUtQLUaVvobx4PozE3W8B2mh0OfL1PMgxnHpJssE_2y0AXw_v71v2S-stLPsKVfpT-PHPixxwWqqvLMPPi0ivw4-ot4O0jdHODYTHlphS58Xr7lHi7IbMfbegS8ccu-SOGbDGBJsYErjU-ycUesqSxD25V5NUSjNfBza8kV5cKnK7FaS1HOeAzS1aHKiBJUJH5_ruoEoQe4Jj8WljE-P15ExvlYyDEQdr7GGx_X6KG26VH62kzzIrRq3Yjk_E5q8fC2RbfEgtdsqyR1j3FwP6pQRQmE" };

static size_t auth_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static unsigned int auth_request_custom_success (char *data_buffer) {

	unsigned int retval = 1;

	char actual_address[128] = { 0 };

	CURL *curl = curl_easy_init ();
	if (curl) {
		// POST /auth/custom
		(void) snprintf (actual_address, 128, "%s/auth/custom", address);
		retval = curl_post_form_value (
			curl, actual_address,
			auth_request_all_data_handler, data_buffer,
			"key", "okay",
			(unsigned int) HTTP_STATUS_OK
		);

		curl_easy_cleanup (curl);
	}

	return retval;

}

static unsigned int auth_request_custom_bad (char *data_buffer) {

	unsigned int retval = 1;

	char actual_address[128] = { 0 };

	CURL *curl = curl_easy_init ();
	if (curl) {
		// POST /auth/custom
		(void) snprintf (actual_address, 128, "%s/auth/custom", address);
		retval = curl_post_form_value (
			curl, actual_address,
			auth_request_all_data_handler, data_buffer,
			"key", "bad",
			(unsigned int) HTTP_STATUS_UNAUTHORIZED
		);

		curl_easy_cleanup (curl);
	}

	return retval;

}

static unsigned int auth_request_all_actual (
	CURL *curl
) {

	unsigned int errors = 0;

	char data_buffer[4096] = { 0 };
	char actual_address[128] = { 0 };

	// GET /test
	(void) snprintf (actual_address, 128, "%s/test", address);
	errors |= curl_simple_handle_data (
		curl, address,
		auth_request_all_data_handler, data_buffer
	);

	// GET /auth/token
	(void) snprintf (actual_address, 128, "%s/auth/token", address);
	errors |= curl_simple_with_auth (curl, actual_address, token);

	// POST /auth/custom
	errors |= auth_request_custom_success (data_buffer);

	// POST /auth/custom
	errors |= auth_request_custom_bad (data_buffer);

	return errors;

}

// perform requests to every route
static unsigned int auth_request_all (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		if (!auth_request_all_actual (curl)) {
			cerver_log_line_break ();
			cerver_log_line_break ();

			cerver_log_success (
				"auth_request_all () - All requests succeeded!"
			);

			cerver_log_line_break ();
			cerver_log_line_break ();

			retval = 0;
		}
	}

	curl_easy_cleanup (curl);

	return retval;

}

int main (int argc, char **argv) {

	int code = 0;

	cerver_log_init ();

	code = (int) auth_request_all ();

	cerver_log_end ();

	return code;

}
