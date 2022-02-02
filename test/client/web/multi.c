#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/http/status.h>

#include <cerver/utils/log.h>

#include "curl.h"

#define DEFAULT_REQUESTS		16
#define RESPONSE_BUFFER_SIZE	1024

static const char *address = { "127.0.0.1:8080/test" };

static size_t multi_request_all_data_handler (
	void *contents, size_t size, size_t nmemb, void *storage
) {

	(void) strncpy ((char *) storage, (char *) contents, size * nmemb);

	return size * nmemb;

}

static CurlResult multi_single_request_internal (CURL *curl) {

	char response_buffer[RESPONSE_BUFFER_SIZE] = { 0 };

	CurlResult result = CURL_RESULT_NONE;

	curl_mimepart *field = NULL;

	curl_mime *form = curl_mime_init (curl);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "value");
	(void) curl_mime_data (field, "holaASDFADFADFASDFA", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "extra");
	(void) curl_mime_data (field, "extrakjhassadasfsdfsdgfgdfhbssdawerftdsdbkl", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "other");
	(void) curl_mime_data (field, "otherrasklsdafasdgfsdagsdjskjhasklawerj", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "cerver");
	(void) curl_mime_filedata (field, "./img/cerver.png");

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "one");
	(void) curl_mime_data (field, "oneaklsjfjklashfDSAFDSFADF", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "two");
	(void) curl_mime_data (field, "twoDASFASDFASDGFG", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "three");
	(void) curl_mime_data (field, "threeADFASDFADFASDF", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "four");
	(void) curl_mime_data (field, "fourasdasdaSFSDFS", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "five");
	(void) curl_mime_data (field, "fivesadasdasdasd", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "six");
	(void) curl_mime_data (field, "sixasdasdasfasf", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "seven");
	(void) curl_mime_data (field, "sevensadasdasasdasdsadsad", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "banner");
	(void) curl_mime_filedata (field, "./img/banner.png");

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "eight");
	(void) curl_mime_data (field, "sadasdasdasdasd", CURL_ZERO_TERMINATED);

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "file-one");
	(void) curl_mime_filedata (field, "./img/cerver-welcome-example.png");

	field = curl_mime_addpart (form);
	(void) curl_mime_name (field, "file-two");
	(void) curl_mime_filedata (field, "./img/cerver-exit-example.png");

	(void) curl_easy_setopt (curl, CURLOPT_URL, address);

	(void) curl_easy_setopt (curl, CURLOPT_MIMEPOST, form);

	curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, multi_request_all_data_handler);
	curl_easy_setopt (curl, CURLOPT_WRITEDATA, response_buffer);

	result = curl_perform_request (curl, HTTP_STATUS_OK);

	curl_mime_free (form);

	return result;

}

static unsigned int multi_single_request (void) {

	unsigned int retval = 1;

	CURL *curl = curl_easy_init ();
	if (curl) {
		retval = multi_single_request_internal (curl);
		
		curl_easy_cleanup (curl);	
	}

	return retval;

}

static unsigned int multi_request_all (const int count) {

	unsigned int errors = 0;

	for (int i = 0; i < count; i++) {
		errors |= multi_single_request ();
	}

	return errors;

}

int main (int argc, char **argv) {

	int code = 0;
	int requests = DEFAULT_REQUESTS;

	if (argc > 1) {
		requests = atoi (argv[1]);
	}

	cerver_log_init ();

	code = multi_request_all (requests);

	cerver_log_end ();

	return code;

}
