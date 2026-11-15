/**
 * @file cerver/http/jwt/key.h
 * @brief The keys of the tokens: a secret of HS256 / HS384 / HS512 or an OpenSSL key of RS256 / RS384 / RS512 and
 * ES256 / ES384 / ES512, and how they sign and verify.
 */

#ifndef _CERVER_HTTP_JWT_KEY_H_
#define _CERVER_HTTP_JWT_KEY_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/http/jwt/alg.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup http_jwt_key Keys
 * @ingroup http_security
 * @brief The keys that sign and verify the tokens (RFC 7518).
 * @{
 */

/** @brief The shortest RSA modulus that is accepted (RFC 7518 section 3.3). */
#define JWT_KEY_RSA_MINIMUM_BITS			2048

/** @brief What is used to know that an HMAC secret is a PEM key. */
#define JWT_KEY_PEM_PREFIX					"-----BEGIN"

/**
 * @brief The X-macro table of the types of a key: the number, the name and the string of each one.
 */
#define JWT_KEY_TYPE_MAP(XX)					\
	XX(0,	PRIVATE,	Private)				\
	XX(1,	PUBLIC,		Public)

/**
 * @brief The type of a key, one `JWT_KEY_TYPE_<name>` for each row of #JWT_KEY_TYPE_MAP: a private key signs and a
 * public key verifies.
 */
typedef enum JwtKeyType {

	#define XX(num, name, string) JWT_KEY_TYPE_##name = num,
	JWT_KEY_TYPE_MAP (XX)
	#undef XX

} JwtKeyType;

/**
 * @brief Returns the string of the type of a key.
 *
 * @param[in] type the type
 * @return the string of its row in #JWT_KEY_TYPE_MAP, a static string that must not be freed; NULL for a value out
 * of the table
 */
CERVER_PUBLIC const char *jwt_key_type_to_string (
	const JwtKeyType type
);

/**
 * @brief The X-macro table of why a key could not be created or is not strong enough: the number, the name and the
 * string of each error.
 */
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

/**
 * @brief Why a key could not be created or is not strong enough, one `JWT_KEY_ERROR_<name>` for each row of
 * #JWT_KEY_ERROR_MAP.
 */
typedef enum JwtKeyError {

	#define XX(num, name, string) JWT_KEY_ERROR_##name = num,
	JWT_KEY_ERROR_MAP (XX)
	#undef XX

} JwtKeyError;

/**
 * @brief Returns the string of a key error.
 *
 * @param[in] error the error
 * @return the string of its row in #JWT_KEY_ERROR_MAP, a static string that must not be freed; NULL for a value out
 * of the table
 */
CERVER_PUBLIC const char *jwt_key_error_to_string (
	const JwtKeyError error
);

struct evp_pkey_st;
struct bignum_st;

/**
 * @brief The key of an algorithm, created once and shared by every thread: the secret of HS256 / HS384 / HS512, or
 * the OpenSSL key of RS256 / RS384 / RS512 and ES256 / ES384 / ES512.
 */
typedef struct JwtKey {

	/** @brief The algorithm of the key. */
	jwt_alg_t alg;
	/** @brief Private or public. */
	JwtKeyType type;

	/** @brief The secret of an HS algorithm. */
	unsigned char *secret;
	/** @brief The length of #secret. */
	size_t secret_length;

	/** @brief The OpenSSL key of an RS or an ES algorithm. */
	struct evp_pkey_st *key;

	/** @brief The order of the curve of an ES key, to sign with a low S. */
	struct bignum_st *order;
	/** @brief Half the order of the curve of an ES key. */
	struct bignum_st *half_order;

} JwtKey;

/**
 * @brief Creates the key of an algorithm from its material: the secret of an HS algorithm, or the PEM of an RS or an
 * ES one.
 *
 * The type of the key and its curve must match the algorithm; an encrypted PEM is not accepted (nothing asks for its
 * passphrase).
 *
 * @param[in] alg the algorithm
 * @param[in] type a private key signs and a public key verifies
 * @param[in] material the secret or the PEM, copied
 * @param[in] material_length its bytes
 * @param[out] error the reason when it fails, JWT_KEY_ERROR_NONE on success; it can be NULL
 * @return the key, released with jwt_key_delete (); NULL on error
 */
CERVER_PUBLIC JwtKey *jwt_key_create (
	const jwt_alg_t alg, const JwtKeyType type,
	const unsigned char *material, const size_t material_length,
	JwtKeyError *error
);

/**
 * @brief Checks what RFC 7518 asks of a key: an HS secret with at least the size of its hash, that is not a PEM
 * key, and an RSA modulus of at least #JWT_KEY_RSA_MINIMUM_BITS.
 *
 * @param[in] key the key
 * @return JWT_KEY_ERROR_NONE if the key can be used, JWT_KEY_ERROR_EMPTY for a NULL key, or the reason
 */
CERVER_PUBLIC JwtKeyError jwt_key_check_strength (
	const JwtKey *key
);

/**
 * @brief Releases a key; its secret is cleared before it is released.
 *
 * @param[in] key the key, it can be NULL
 */
CERVER_PUBLIC void jwt_key_delete (JwtKey *key);

/**
 * @brief Signs a message with a private key or a secret.
 *
 * An ES signature is the raw R and S with a low S (RFC 7518 section 3.4).
 *
 * @param[in] key the key
 * @param[in] message the message
 * @param[in] message_length its bytes
 * @param[out] signature the signature, allocated with malloc ()
 * @param[out] signature_length its bytes
 * @retval 0 the message was signed
 * @retval EINVAL a NULL argument or the key can not sign
 * @retval ENOMEM the signature could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE int jwt_key_sign (
	const JwtKey *key,
	const char *message, const size_t message_length,
	unsigned char **signature, size_t *signature_length
);

/**
 * @brief Verifies the raw signature of a message with a public key or a secret; an HMAC is compared in constant
 * time.
 *
 * @param[in] key the key
 * @param[in] message the message
 * @param[in] message_length its bytes
 * @param[in] signature the signature
 * @param[in] signature_length its bytes
 * @retval 0 the signature is valid
 * @retval EINVAL a NULL argument or an invalid signature
 * @retval ENOMEM the memory to verify could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE int jwt_key_verify (
	const JwtKey *key,
	const char *message, const size_t message_length,
	const unsigned char *signature, const size_t signature_length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
