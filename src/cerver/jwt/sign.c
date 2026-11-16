#include <stddef.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/crypto/key.h"
#include "cerver/crypto/signature.h"

#include "cerver/jwt/internal.h"
#include "cerver/jwt/jwt.h"
#include "cerver/jwt/sign.h"

#include "cerver/utils/base64.h"

// the separators of the three parts of a token
#define JWT_SIGN_SEPARATORS					2

// the characters of the token of some claims: its header, its claims and its signature, with their separators
static size_t jwt_sign_length (const CerverJwtKey *key, const size_t claims_size) {

	return key->header_length
		+ base64url_encode_length (claims_size)
		+ base64url_encode_length (key->signature_size)
		+ JWT_SIGN_SEPARATORS;

}

size_t cerver_jwt_sign_size (
	const CerverJwtKey *key, const size_t claims_size
) {

	// claims that are bigger than a token are refused before their size is used
	if (!key || (claims_size > CERVER_JWT_SIZE_MAX)) return 0;

	const size_t length = jwt_sign_length (key, claims_size);

	return (length <= CERVER_JWT_SIZE_MAX) ? length + 1 : 0;

}

// writes the signature of the header and the claims that are in the token, after them
static CerverJwtError jwt_sign_write (
	const CerverJwtKey *key, char *token, const size_t signed_length, size_t *length
) {

	u8 signature[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t signature_size = 0;

	if (
		!cerver_crypto_sign (
			key->key, key->signature, token, signed_length,
			signature, sizeof (signature), &signature_size
		)
	) {
		return CERVER_JWT_ERROR_SIGNATURE;
	}

	size_t position = signed_length;
	token[position++] = JWT_SEPARATOR;
	position += base64url_encode (token + position, signature, signature_size);

	*length = position;

	return CERVER_JWT_ERROR_NONE;

}

CerverJwtError cerver_jwt_sign (
	const CerverJwtKey *key,
	const char *claims, const size_t claims_size,
	char *token, const size_t capacity, size_t *length
) {

	if (length) *length = 0;

	if (!key || !claims || !claims_size || !token || !length) return CERVER_JWT_ERROR_ARGUMENTS;

	if (!cerver_crypto_key_is_private (key->key)) return CERVER_JWT_ERROR_KEY;

	const size_t needed = cerver_jwt_sign_size (key, claims_size);
	if (!needed || (capacity < needed)) return CERVER_JWT_ERROR_SIZE;

	// the header and the claims, that are what is signed
	(void) memcpy (token, key->header, key->header_length);

	size_t position = key->header_length;
	token[position++] = JWT_SEPARATOR;
	position += base64url_encode (token + position, (const unsigned char *) claims, claims_size);

	const CerverJwtError error = jwt_sign_write (key, token, position, length);

	// a token without its signature is never left in the buffer
	if (error != CERVER_JWT_ERROR_NONE) token[0] = '\0';

	return error;

}
