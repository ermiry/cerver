#ifndef _TEST_APP_AUTH_H_
#define _TEST_APP_AUTH_H_

#include <cerver/types/types.h>

#include <cerver/serializer.h>

#define USERNAME_SIZE		256
#define PASSWORD_SIZE		256

#define CREDENTIALS_VERSION		1

// the credentials go as their bytes, or as an object of their schema to a cerver with an auth schema
#define CREDENTIALS_FIELDS(T, SCALAR, ENUM, STRING, ARRAY, OBJECT, OBJECTS)		\
	STRING(T, 1, TEXT, username, USERNAME_SIZE, SERIALIZER_REQUIRED)			\
	STRING(T, 2, TEXT, password, PASSWORD_SIZE, SERIALIZER_REQUIRED)

SERIALIZER_DECLARE (Credentials, credentials, CREDENTIALS_FIELDS)

extern Credentials *credentials_new (
	const char *username, const char *password
);

extern void credentials_delete (void *credentials_ptr);

extern u8 app_auth_method (void *auth_method_ptr);

#endif
