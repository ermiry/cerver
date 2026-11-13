#ifndef _CERVER_HTTP_JWT_KEY_H_
#define _CERVER_HTTP_JWT_KEY_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/jwt/alg.h"

#ifdef __cplusplus
extern "C" {
#endif

// the shortest RSA modulus that is accepted (RFC 7518 §3.3)
#define JWT_KEY_RSA_MINIMUM_BITS			2048

// what is used to know that an HMAC secret is a PEM key
#define JWT_KEY_PEM_PREFIX					"-----BEGIN"

#define JWT_KEY_TYPE_MAP(XX)					\
	XX(0,	PRIVATE,	Private)				\
	XX(1,	PUBLIC,		Public)

typedef enum JwtKeyType {

	#define XX(num, name, string) JWT_KEY_TYPE_##name = num,
	JWT_KEY_TYPE_MAP (XX)
	#undef XX

} JwtKeyType;

CERVER_PUBLIC const char *jwt_key_type_to_string (
	const JwtKeyType type
);

#define JWT_KEY_ERROR_MAP(XX)																	\
	XX(0,	NONE,		None)																	\
	XX(1,	ALGORITHM,	The algorithm does not use a key)										\
	XX(2,	EMPTY,		The key is empty)														\
	XX(3,	PEM,		The key is not a valid PEM key without a passphrase)					\
	XX(4,	TYPE,		The type of the key does not match the algorithm)						\
	XX(5,	CURVE,		The curve of the key does not match the algorithm)						\
	XX(6,	STRENGTH,	The key is shorter than what the algorithm requires)					\
	XX(7,	SECRET,		The HMAC secret is a PEM key)											\
	XX(8,	MEMORY,		Failed to allocate the key)

typedef enum JwtKeyError {

	#define XX(num, name, string) JWT_KEY_ERROR_##name = num,
	JWT_KEY_ERROR_MAP (XX)
	#undef XX

} JwtKeyError;

CERVER_PUBLIC const char *jwt_key_error_to_string (
	const JwtKeyError error
);

struct evp_pkey_st;
struct bignum_st;

// the key of an algorithm, created once and shared by every thread:
// the secret of HS256 / HS384 / HS512,
// or the OpenSSL key of RS256 / RS384 / RS512 and ES256 / ES384 / ES512
typedef struct JwtKey {

	jwt_alg_t alg;
	JwtKeyType type;

	unsigned char *secret;
	size_t secret_length;

	struct evp_pkey_st *key;

	// the order of the curve of an ES key, to sign with a low S
	struct bignum_st *order;
	struct bignum_st *half_order;

} JwtKey;

// creates the key of an algorithm from its material:
// the secret of an HS algorithm, or the PEM of an RS or an ES one,
// a private key signs and a public key verifies
// the type of the key and its curve must match the algorithm,
// an encrypted PEM is not accepted (nothing asks for its passphrase)
// returns the key, or NULL and the reason in error
CERVER_PUBLIC JwtKey *jwt_key_create (
	const jwt_alg_t alg, const JwtKeyType type,
	const unsigned char *material, const size_t material_length,
	JwtKeyError *error
);

// checks what RFC 7518 asks of a key:
// an HS secret with at least the size of its hash, that is not a PEM key,
// and an RSA modulus of at least JWT_KEY_RSA_MINIMUM_BITS
// returns JWT_KEY_ERROR_NONE if the key can be used
CERVER_PUBLIC JwtKeyError jwt_key_check_strength (
	const JwtKey *key
);

// clears the secret before it is released
CERVER_PUBLIC void jwt_key_delete (JwtKey *key);

// signs the message with a private key or a secret,
// an ES signature is the raw R and S with a low S (RFC 7518 §3.4)
// the signature is allocated with malloc ()
// returns 0 on success, EINVAL or ENOMEM on error
CERVER_PRIVATE int jwt_key_sign (
	const JwtKey *key,
	const char *message, const size_t message_length,
	unsigned char **signature, size_t *signature_length
);

// verifies the raw signature of the message with a public key or a secret,
// an HMAC is compared in constant time
// returns 0 if the signature is valid, EINVAL or ENOMEM on error
CERVER_PRIVATE int jwt_key_verify (
	const JwtKey *key,
	const char *message, const size_t message_length,
	const unsigned char *signature, const size_t signature_length
);

#ifdef __cplusplus
}
#endif

#endif
