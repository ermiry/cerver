#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/cerver.h>
#include <cerver/version.h>

#include <cerver/http/http.h>

#include <cerver/utils/utils.h>
#include <cerver/utils/log.h>

#include "users.h"

static Cerver *api_cerver = NULL;

int main (int argc, char **argv) {

	srand (time (NULL));

	cerver_init ();

	cerver_version_print_full ();

	cerver_log_debug ("Cerver JWT decode test");
	printf ("\n");

	api_cerver = cerver_create (
		CERVER_TYPE_WEB,
		"api-cerver",
		8080,
		PROTOCOL_TCP,
		false,
		2
	);

	if (api_cerver) {
		/*** web cerver configuration ***/
		HttpCerver *http_cerver = (HttpCerver *) api_cerver->cerver_data;

		http_cerver_auth_set_jwt_algorithm (http_cerver, JWT_ALG_RS256);
		http_cerver_auth_set_jwt_priv_key_filename (http_cerver, "keys/key.key");
		http_cerver_auth_set_jwt_pub_key_filename (http_cerver, "keys/key.key.pub");

		http_cerver_init (http_cerver);

		/*** test ***/
		const char *bearer_token = { "Bearer eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpYXQiOjE2MDc1NTU2ODYsImlkIjoiMTYwNzU1NTY4NiIsIm5hbWUiOiJFcmljayBTYWxhcyIsInJvbGUiOiJjb21tb24iLCJ1c2VybmFtZSI6ImVybWlyeSJ9.QIEZXo8vkthUPQzjozQNJ8P5ZTxbA6w2OWjeVplWVB2cs1ZySTRMvZ6Dh8dDe-CAHH4P9zcJumJIR0LWxZQ63O61c-cKZuupaPiI9qQaqql30oBnywznWCDkkXgkoTh683fSxoFR71Gzqp5e41mX-KxopH-Kh4Bz3eM3d27p-8tKOUGUlVk1AgWfGU2LFfTu1ZLkVYwo4ceGxHdntS1C_6IiutG8TLFjuKixgujIhK0ireG1cfAs7uvtGhWLXokLpvTbIrrjKSKLEjCcPh_yPpWeay0Y4yV1e5zSQKHiG7ry0D3qDWJcfHtjNLjAFb93TnpXtoXhftq_uL4d7BV16Y9ssQ5MpNoEh3bxHBgaCHOCGuCAVJfdDsRBT5C60_jme7S1utQ4qkpA8YbAoYKi58yGkHxnnYYbaYFQYSykbsYPFZ-4dieFlaIc5-m-vymwG--AMjfhu8Sdm0V0xA3Vq_9FzUIfXvo32-k0eH4QGPX6W8-FinR_Lj-Zj29mXfgALpTlF-m7Sbo3hVebnpwAFKZR495UBLL8B8u3MHB9XQxX6z4fHAxv5Nkftr4ShGRWwI_yVD73sLq44V9FEBQksoXLEBm5RqRC8qvhoxOIzbEYokd6MWLak1sZRUfIIqu65fERA5sMjs9JRnlYu7twP3Fk_VPy-tZBXXA7KTc3lkE" };

		User *decoded_user = NULL;
		if (http_cerver_auth_validate_jwt (
			http_cerver,
			bearer_token,
			user_parse_from_json,
			((void **) &decoded_user)
		)) {
			(void) printf ("\n\n");
			user_print (decoded_user);
			(void) printf ("\n\n");

			user_delete (decoded_user);
		}

		else {
			cerver_log_error ("Token is invalid!");
		}

		cerver_teardown (api_cerver);
	}

	else {
		cerver_log_error ("Failed to create cerver!");

		cerver_delete (api_cerver);
	}

	cerver_end ();

	return 0;

}