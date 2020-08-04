#ifndef JWT_PRIVATE_H
#define JWT_PRIVATE_H

#include "cerver/http/json/json.h"

#include "cerver/http/jwt/alg.h"

struct jwt {
	jwt_alg_t alg;
	unsigned char *key;
	int key_len;
	json_t *grants;
	json_t *headers;
};

struct jwt_valid {
	jwt_alg_t alg;
	time_t now;
	int hdr;
	json_t *req_grants;
	unsigned int status;
};

/** Opaque JWT object. */
typedef struct jwt jwt_t;

/** Opaque JWT validation object. */
typedef struct jwt_valid jwt_valid_t;

/* Memory allocators. */
extern void *jwt_malloc(size_t size);
extern void jwt_freemem(void *ptr);

/* Helper routines. */
extern void jwt_base64uri_encode(char *str);
extern void *jwt_b64_decode(const char *src, int *ret_len);

/* These routines are implemented by the crypto backend. */
extern int jwt_sign_sha_hmac(jwt_t *jwt, char **out, unsigned int *len,
		      const char *str);

extern int jwt_verify_sha_hmac(jwt_t *jwt, const char *head, const char *sig);

extern int jwt_sign_sha_pem(jwt_t *jwt, char **out, unsigned int *len,
		     const char *str);

extern int jwt_verify_sha_pem(jwt_t *jwt, const char *head, const char *sig_b64);

#endif