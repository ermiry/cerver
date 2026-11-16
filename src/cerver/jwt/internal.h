/**
 * @file jwt/internal.h
 * @internal
 * @brief What the parts of the tokens share and never export: the struct of a key.
 */

#ifndef _CERVER_JWT_INTERNAL_H_
#define _CERVER_JWT_INTERNAL_H_

#include <stddef.h>

#include "cerver/config.h"

#include "cerver/crypto/key.h"
#include "cerver/crypto/signature.h"

#include "cerver/jwt/jwt.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @internal @brief The character between the parts of a token. */
#define JWT_SEPARATOR						'.'

/**
 * @internal
 * @brief A key of a token (cerver/jwt/jwt.h): a key of the provider with its algorithm and what never changes
 * in its tokens.
 */
struct CerverJwtKey {

	/** @internal @brief The key of the provider, that belongs to this one. */
	CerverCryptoKey *key;

	/** @internal @brief The algorithm of its tokens. */
	CerverJwtAlgorithm algorithm;
	/** @internal @brief The algorithm of the provider that signs them. */
	CerverCryptoSignature signature;
	/** @internal @brief The bytes of each of its signatures. */
	size_t signature_size;

	/** @internal @brief The header of its tokens, `{"alg":"<algorithm>","typ":"JWT"}`, in base64url. */
	char header[CERVER_JWT_HEADER_ENCODED_SIZE];
	/** @internal @brief The characters of #header. */
	size_t header_length;

};

#ifdef __cplusplus
}
#endif

#endif
