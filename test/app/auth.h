#ifndef _TEST_APP_AUTH_H_
#define _TEST_APP_AUTH_H_

#include <cerver/types/types.h>

#define USERNAME_SIZE		256
#define PASSWORD_SIZE		256

typedef struct Credentials {

	char username[USERNAME_SIZE];
	char password[PASSWORD_SIZE];

} Credentials;

extern Credentials *credentials_new (
	const char *username, const char *password
);

extern void credentials_delete (void *credentials_ptr);

extern u8 app_auth_method (void *auth_method_ptr);

#endif