#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cerver/auth.h>
#include <cerver/packets.h>

#include "app.h"
#include "auth.h"

Credentials *credentials_new (
	const char *username, const char *password
) {

	Credentials *credentials = (Credentials *) malloc (sizeof (Credentials));
	if (credentials) {
		(void) strncpy (credentials->username, username, USERNAME_SIZE - 1);
		(void) strncpy (credentials->password, password, PASSWORD_SIZE - 1);
	}

	return credentials;

}

void credentials_delete (void *credentials_ptr) {
	
	if (credentials_ptr) free (credentials_ptr);
	
}

static u8 app_auth_method_username (
	AuthMethod *auth_method, const char *username
) {

	u8 retval = 1;

	if (username) {
		if (strlen (username) > 0) {
			if (!strcmp (username, "ermiry")) {
				retval = 0;		// success
			}

			else {
				auth_method->error_message = str_new ("Username does not exists!");
			}
		}

		else {
			auth_method->error_message = str_new ("Username is required!");
		}
	}

	return retval;

}

static u8 app_auth_method_password (
	AuthMethod *auth_method, const char *password
) {
	
	u8 retval = 1;

	if (password) {
		if (strlen (password) > 0) {
			if (!strcmp (password, "049ec1af7c1332193d602986f2fdad5b4d1c2ff90e5cdc65388c794c1f10226b")) {
				retval = 0;		// success auth
			}

			else {
				auth_method->error_message = str_new ("Password is incorrect!");
			}
		}

		else {
			auth_method->error_message = str_new ("Password is required!");
		}
	}

	return retval;

}

u8 app_auth_method (void *auth_method_ptr) {

	u8 retval = 1;

	if (auth_method_ptr) {
		AuthMethod *auth_method = (AuthMethod *) auth_method_ptr;
		if (auth_method->auth_data) {
			if (auth_method->auth_data->auth_data_size >= sizeof (Credentials)) {
				Credentials *credentials = (Credentials *) auth_method->auth_data->auth_data;

				if (!app_auth_method_username (auth_method, credentials->username)) {
					if (!app_auth_method_password (auth_method, credentials->password)) {
						retval = 0;		// success
					}
				}
			}

			else {
				auth_method->error_message = str_new ("Missing auth data!");
			}
		}
	}

	return retval;

}