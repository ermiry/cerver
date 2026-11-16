#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/crypto/internal.h"
#include "cerver/crypto/key.h"
#include "cerver/crypto/memory.h"
#include "cerver/crypto/provider.h"

#include "cerver/json/document.h"
#include "cerver/json/json.h"
#include "cerver/json/value.h"

#include "cerver/utils/base64.h"

// the most bytes of the text of a JWK: a private key of RSA of 2^14 bits has eight numbers of 2^11 bytes
#define CRYPTO_JWK_SIZE_MAX					(1 << 16)

// the levels of a JWK: its object and the arrays and objects of the members that are not read
#define CRYPTO_JWK_DEPTH					(1 << 3)

// the members of a JWK (RFC 7517 section 4 and RFC 7518 section 6)
#define CRYPTO_JWK_TYPE						"kty"
#define CRYPTO_JWK_CURVE					"crv"
#define CRYPTO_JWK_SECRET					"k"
#define CRYPTO_JWK_MODULUS					"n"
#define CRYPTO_JWK_EXPONENT					"e"
#define CRYPTO_JWK_PRIVATE					"d"
#define CRYPTO_JWK_FIRST_PRIME				"p"
#define CRYPTO_JWK_SECOND_PRIME				"q"
#define CRYPTO_JWK_FIRST_EXPONENT			"dp"
#define CRYPTO_JWK_SECOND_EXPONENT			"dq"
#define CRYPTO_JWK_COEFFICIENT				"qi"
#define CRYPTO_JWK_OTHER_PRIMES				"oth"
#define CRYPTO_JWK_X						"x"
#define CRYPTO_JWK_Y						"y"

// what a JWK says that its key is for (RFC 7517 sections 4.2 and 4.3), and the values of a key of signatures
#define CRYPTO_JWK_USE						"use"
#define CRYPTO_JWK_USE_SIGNATURE			"sig"
#define CRYPTO_JWK_OPERATIONS				"key_ops"
#define CRYPTO_JWK_OPERATION_SIGN			"sign"
#define CRYPTO_JWK_OPERATION_VERIFY			"verify"

// the values of the type of a JWK
#define CRYPTO_JWK_TYPE_SECRET				"oct"
#define CRYPTO_JWK_TYPE_RSA					"RSA"
#define CRYPTO_JWK_TYPE_CURVE				"EC"
#define CRYPTO_JWK_TYPE_EDWARDS				"OKP"

// a JWK that is being read: its members are decoded one after the other into memory of the call
typedef struct CryptoJwk {

	const CerverJsonValue *object;

	// where the next member is decoded
	u8 *decoded;

	// a member that is there is not a string, or is not base64url
	bool failed;

} CryptoJwk;

// the bytes of a member of the JWK, without bytes when it is not there
static CryptoBytes crypto_jwk_member (CryptoJwk *jwk, const char *name) {

	CryptoBytes bytes = { NULL, 0 };

	const CerverJsonValue *member = cerver_json_object_get (jwk->object, name);
	if (!member) return bytes;

	size_t size = 0;
	if (
		cerver_json_is_string (member)
		&& !base64url_decode (
			jwk->decoded, &size, cerver_json_string_value (member), cerver_json_string_length (member)
		)
	) {
		bytes.bytes = jwk->decoded;
		bytes.size = size;

		jwk->decoded += size;
	}

	else {
		jwk->failed = true;
	}

	return bytes;

}

// whether a value is a string with exactly the text
static bool crypto_jwk_string_is (const CerverJsonValue *value, const char *text) {

	const size_t length = strlen (text);

	return cerver_json_is_string (value)
		&& (cerver_json_string_length (value) == length)
		&& !memcmp (cerver_json_string_value (value), text, length);

}

// whether a member of the JWK is the text
static bool crypto_jwk_is (const CryptoJwk *jwk, const char *name, const char *text) {

	return crypto_jwk_string_is (cerver_json_object_get (jwk->object, name), text);

}

// the operations of a key are an array that has the one of signing or the one of verifying
static bool crypto_jwk_operations_sign (const CerverJsonValue *operations) {

	bool found = false;

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (operations);
	const CerverJsonValue *operation = NULL;
	while (!found && (operation = cerver_json_array_iterator_next (&iterator))) {
		found = crypto_jwk_string_is (operation, CRYPTO_JWK_OPERATION_SIGN)
			|| crypto_jwk_string_is (operation, CRYPTO_JWK_OPERATION_VERIFY);
	}

	return found;

}

// a JWK that says what its key is for is taken only when it is for signatures:
// a key that was made to encrypt never signs nor verifies
static bool crypto_jwk_is_for_signatures (const CryptoJwk *jwk) {

	const CerverJsonValue *use = cerver_json_object_get (jwk->object, CRYPTO_JWK_USE);
	if (use && !crypto_jwk_string_is (use, CRYPTO_JWK_USE_SIGNATURE)) return false;

	const CerverJsonValue *operations = cerver_json_object_get (jwk->object, CRYPTO_JWK_OPERATIONS);

	return !operations || crypto_jwk_operations_sign (operations);

}

// a secret: an empty one and one that is too long are not a key
static CerverCryptoKey *crypto_jwk_secret (CryptoJwk *jwk, CerverCryptoKeyError *error) {

	const CryptoBytes secret = crypto_jwk_member (jwk, CRYPTO_JWK_SECRET);

	CerverCryptoKey *key = (secret.bytes && !jwk->failed) ?
		cerver_crypto_key_create_secret (secret.bytes, secret.size, error) : NULL;

	if (!key && (!error || (*error != CERVER_CRYPTO_KEY_ERROR_MEMORY))) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_FORMAT);
	}

	return key;

}

// a private key of RSA has its two primes, their exponents and the coefficient
static bool crypto_jwk_rsa_is_complete (const CryptoRsaParts *parts) {

	const bool is_private = parts->private_exponent.bytes != NULL;

	const bool factors = parts->first_prime.bytes && parts->second_prime.bytes
		&& parts->first_exponent.bytes && parts->second_exponent.bytes && parts->coefficient.bytes;

	const bool any_factor = parts->first_prime.bytes || parts->second_prime.bytes
		|| parts->first_exponent.bytes || parts->second_exponent.bytes || parts->coefficient.bytes;

	return parts->modulus.bytes && parts->exponent.bytes && (is_private ? factors : !any_factor);

}

static CerverCryptoKey *crypto_jwk_rsa (CryptoJwk *jwk, CerverCryptoKeyError *error) {

	// a key with more than two primes is not one of cerver
	if (cerver_json_object_get (jwk->object, CRYPTO_JWK_OTHER_PRIMES)) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
		return NULL;
	}

	CryptoRsaParts parts;
	parts.modulus = crypto_jwk_member (jwk, CRYPTO_JWK_MODULUS);
	parts.exponent = crypto_jwk_member (jwk, CRYPTO_JWK_EXPONENT);
	parts.private_exponent = crypto_jwk_member (jwk, CRYPTO_JWK_PRIVATE);
	parts.first_prime = crypto_jwk_member (jwk, CRYPTO_JWK_FIRST_PRIME);
	parts.second_prime = crypto_jwk_member (jwk, CRYPTO_JWK_SECOND_PRIME);
	parts.first_exponent = crypto_jwk_member (jwk, CRYPTO_JWK_FIRST_EXPONENT);
	parts.second_exponent = crypto_jwk_member (jwk, CRYPTO_JWK_SECOND_EXPONENT);
	parts.coefficient = crypto_jwk_member (jwk, CRYPTO_JWK_COEFFICIENT);

	if (jwk->failed || !crypto_jwk_rsa_is_complete (&parts)) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_FORMAT);
		return NULL;
	}

	return crypto_key_adopt (
		cerver_crypto_provider_key_create_rsa (&parts), parts.private_exponent.bytes != NULL, error
	);

}

// the curve of a JWK by its name, the string of its row in CERVER_CRYPTO_CURVE_MAP
static CerverCryptoCurve crypto_jwk_curve (const CryptoJwk *jwk) {

	CerverCryptoCurve curve = CERVER_CRYPTO_CURVE_NONE;

	#define XX(num, name, string) if (crypto_jwk_is (jwk, CRYPTO_JWK_CURVE, #string)) curve = CERVER_CRYPTO_CURVE_##name;
	CERVER_CRYPTO_CURVE_MAP(XX)
	#undef XX

	return curve;

}

static CerverCryptoKey *crypto_jwk_curve_key (CryptoJwk *jwk, CerverCryptoKeyError *error) {

	const CerverCryptoCurve curve = crypto_jwk_curve (jwk);
	if (curve == CERVER_CRYPTO_CURVE_NONE) {
		crypto_key_error_set (
			error, cerver_json_is_string (cerver_json_object_get (jwk->object, CRYPTO_JWK_CURVE)) ?
				CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED : CERVER_CRYPTO_KEY_ERROR_FORMAT
		);

		return NULL;
	}

	const CryptoBytes x = crypto_jwk_member (jwk, CRYPTO_JWK_X);
	const CryptoBytes y = crypto_jwk_member (jwk, CRYPTO_JWK_Y);
	const CryptoBytes secret = crypto_jwk_member (jwk, CRYPTO_JWK_PRIVATE);

	if (jwk->failed || !x.bytes || !y.bytes) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_FORMAT);
		return NULL;
	}

	return crypto_key_adopt (
		cerver_crypto_provider_key_create_curve (curve, x, y, secret), secret.bytes != NULL, error
	);

}

// the Edwards curve of a JWK: the type of its key, by the string of its row in CERVER_CRYPTO_KEY_TYPE_MAP
static CerverCryptoKeyType crypto_jwk_edwards (const CryptoJwk *jwk) {

	CerverCryptoKeyType type = CERVER_CRYPTO_KEY_TYPE_NONE;

	if (crypto_jwk_is (jwk, CRYPTO_JWK_CURVE, cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_ED25519))) {
		type = CERVER_CRYPTO_KEY_TYPE_ED25519;
	}

	else if (crypto_jwk_is (jwk, CRYPTO_JWK_CURVE, cerver_crypto_key_type_to_string (CERVER_CRYPTO_KEY_TYPE_ED448))) {
		type = CERVER_CRYPTO_KEY_TYPE_ED448;
	}

	return type;

}

static CerverCryptoKey *crypto_jwk_edwards_key (CryptoJwk *jwk, CerverCryptoKeyError *error) {

	const CerverCryptoKeyType type = crypto_jwk_edwards (jwk);
	if (type == CERVER_CRYPTO_KEY_TYPE_NONE) {
		crypto_key_error_set (
			error, cerver_json_is_string (cerver_json_object_get (jwk->object, CRYPTO_JWK_CURVE)) ?
				CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED : CERVER_CRYPTO_KEY_ERROR_FORMAT
		);

		return NULL;
	}

	const CryptoBytes point = crypto_jwk_member (jwk, CRYPTO_JWK_X);
	const CryptoBytes seed = crypto_jwk_member (jwk, CRYPTO_JWK_PRIVATE);

	if (jwk->failed || !point.bytes) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_FORMAT);
		return NULL;
	}

	return crypto_key_adopt (
		cerver_crypto_provider_key_create_edwards (type, point, seed), seed.bytes != NULL, error
	);

}

// the key of a JWK by its type
static CerverCryptoKey *crypto_jwk_key (CryptoJwk *jwk, CerverCryptoKeyError *error) {

	if (!crypto_jwk_is_for_signatures (jwk)) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED);
		return NULL;
	}

	if (crypto_jwk_is (jwk, CRYPTO_JWK_TYPE, CRYPTO_JWK_TYPE_SECRET)) return crypto_jwk_secret (jwk, error);
	if (crypto_jwk_is (jwk, CRYPTO_JWK_TYPE, CRYPTO_JWK_TYPE_RSA)) return crypto_jwk_rsa (jwk, error);
	if (crypto_jwk_is (jwk, CRYPTO_JWK_TYPE, CRYPTO_JWK_TYPE_CURVE)) return crypto_jwk_curve_key (jwk, error);
	if (crypto_jwk_is (jwk, CRYPTO_JWK_TYPE, CRYPTO_JWK_TYPE_EDWARDS)) return crypto_jwk_edwards_key (jwk, error);

	// another type is a key that cerver does not use; without a type the text is not a JWK
	crypto_key_error_set (
		error, cerver_json_is_string (cerver_json_object_get (jwk->object, CRYPTO_JWK_TYPE)) ?
			CERVER_CRYPTO_KEY_ERROR_UNSUPPORTED : CERVER_CRYPTO_KEY_ERROR_FORMAT
	);

	return NULL;

}

// reads the JWK in a copy of its text and decodes its members after it:
// the memory has the numbers of a private key, so it is erased before it is released
static CerverCryptoKey *crypto_jwk_read (
	char *text, const size_t size, CerverCryptoKeyError *error
) {

	CerverCryptoKey *key = NULL;

	CerverJsonLimits limits = CERVER_ZERO_INITIALIZER;
	limits.depth = CRYPTO_JWK_DEPTH;

	CerverJsonError json_error = CERVER_ZERO_INITIALIZER;
	CerverJsonDocument *document = cerver_json_parse_in_situ (
		NULL, text, size,
		CERVER_JSON_PARSE_REJECT_DUPLICATES | CERVER_JSON_PARSE_REJECT_NUL,
		&limits, &json_error
	);

	CryptoJwk jwk;
	jwk.object = cerver_json_document_root (document);
	jwk.decoded = (u8 *) text + size + CERVER_JSON_PADDING;
	jwk.failed = false;

	if (cerver_json_is_object (jwk.object)) {
		key = crypto_jwk_key (&jwk, error);
	}

	else {
		crypto_key_error_set (
			error, (json_error.code == CERVER_JSON_ERROR_MEMORY) ?
				CERVER_CRYPTO_KEY_ERROR_MEMORY : CERVER_CRYPTO_KEY_ERROR_FORMAT
		);
	}

	cerver_json_document_delete (document);

	return key;

}

CerverCryptoKey *cerver_crypto_key_create_jwk (
	const char *jwk, const size_t size,
	CerverCryptoKeyError *error
) {

	if (!jwk || !size) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_ARGUMENTS);
		return NULL;
	}

	if (size > CRYPTO_JWK_SIZE_MAX) {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_FORMAT);
		return NULL;
	}

	CerverCryptoKey *key = NULL;

	// the text with what the reader writes after it, and the bytes of its members, that are less than the text
	const size_t memory_size = size + CERVER_JSON_PADDING + size;
	char *memory = (char *) malloc (memory_size);
	if (memory) {
		(void) memcpy (memory, jwk, size);

		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_NONE);
		key = crypto_jwk_read (memory, size, error);

		cerver_crypto_cleanse (memory, memory_size);
		free (memory);
	}

	else {
		crypto_key_error_set (error, CERVER_CRYPTO_KEY_ERROR_MEMORY);
	}

	return key;

}
