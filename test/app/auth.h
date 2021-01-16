#ifndef _TEST_APP_HANDLER_H_
#define _TEST_APP_HANDLER_H_

#include <cerver/types/types.h>

#define USERNAME_NAME		64
#define PASSWORD_NAME		64

typedef struct Credentials {

	char username[USERNAME_NAME];
	char password[PASSWORD_NAME];

} Credentials;

extern u8 app_auth_method (void *auth_method_ptr);

#endif