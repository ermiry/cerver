#ifndef _CERVER_HTTP_JWT_OPENSSL_H_
#define _CERVER_HTTP_JWT_OPENSSL_H_

#include "cerver/config.h"

#include "cerver/http/jwt/jwt.h"

CERVER_PRIVATE int jwt_sign_sha_hmac (
	jwt_t *jwt, char **out, unsigned int *len, const char *str
);

CERVER_PRIVATE int jwt_verify_sha_hmac (
	jwt_t *jwt, const char *head, const char *sig
);

CERVER_PRIVATE int jwt_sign_sha_pem (
	jwt_t *jwt, char **out, unsigned int *len, const char *str
);

CERVER_PRIVATE int jwt_verify_sha_pem (
	jwt_t *jwt, const char *head, const char *sig_b64
);

#endif