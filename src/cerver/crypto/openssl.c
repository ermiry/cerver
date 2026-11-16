#include <limits.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/types/types.h"

#include "cerver/crypto/hash.h"
#include "cerver/crypto/key.h"
#include "cerver/crypto/provider.h"
#include "cerver/crypto/signature.h"

#ifdef CERVER_CRYPTO_OPENSSL

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/objects.h>
#include <openssl/param_build.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>

// the methods of OpenSSL that are used here return 1 on success
#define CRYPTO_OPENSSL_SUCCESS					1

// the size of the name of a curve that OpenSSL gives
#define CRYPTO_OPENSSL_GROUP_NAME_SIZE			64

// the bytes of each of R and S in the signature of a curve
#define CRYPTO_OPENSSL_P256_COORDINATE_SIZE		32
#define CRYPTO_OPENSSL_P384_COORDINATE_SIZE		48
#define CRYPTO_OPENSSL_P521_COORDINATE_SIZE		66

// a signature of ECDSA is R and S
#define CRYPTO_OPENSSL_SIGNATURE_PARTS			2

// the DER of the signature of the largest curve: two integers of 67 bytes with their headers, inside a sequence
#define CRYPTO_OPENSSL_DER_SIZE					160

// the public point of a curve without compression: its mark and its two coordinates
#define CRYPTO_OPENSSL_POINT_UNCOMPRESSED		0x04
#define CRYPTO_OPENSSL_POINT_SIZE				(1 + (CRYPTO_OPENSSL_SIGNATURE_PARTS * CRYPTO_OPENSSL_P521_COORDINATE_SIZE))

// the bytes of the keys and of the signatures of the Edwards curves (RFC 8032)
#define CRYPTO_OPENSSL_ED25519_KEY_SIZE			32
#define CRYPTO_OPENSSL_ED25519_SIGNATURE_SIZE	64
#define CRYPTO_OPENSSL_ED448_KEY_SIZE			57
#define CRYPTO_OPENSSL_ED448_SIGNATURE_SIZE		114

// the numbers of a key of RSA: its modulus, its two exponents, its two primes, their exponents and the coefficient
#define CRYPTO_OPENSSL_RSA_NUMBERS				8

// the digest of NULL data, that OpenSSL takes as an empty input
static const u8 cerver_crypto_openssl_empty = 0;

#pragma region tables

typedef enum CryptoOpensslFamily {

	CRYPTO_OPENSSL_FAMILY_NONE		= 0,
	CRYPTO_OPENSSL_FAMILY_HMAC		= 1,
	CRYPTO_OPENSSL_FAMILY_RSA		= 2,
	CRYPTO_OPENSSL_FAMILY_RSA_PSS	= 3,
	CRYPTO_OPENSSL_FAMILY_ECDSA		= 4,
	CRYPTO_OPENSSL_FAMILY_EDWARDS	= 5

} CryptoOpensslFamily;

// what each algorithm of a signature needs from OpenSSL
typedef struct CryptoOpensslAlgorithm {

	CryptoOpensslFamily family;

	// an Edwards curve signs its message without a digest
	const EVP_MD *(*digest)(void);

	// the bytes of each of R and S of an ECDSA
	size_t coordinate_size;

	// the bytes of a signature of an Edwards curve
	size_t signature_size;

} CryptoOpensslAlgorithm;

// one for each row of CERVER_CRYPTO_SIGNATURE_MAP, in its order
static const CryptoOpensslAlgorithm crypto_openssl_algorithms[CERVER_CRYPTO_SIGNATURES] = {
	{ CRYPTO_OPENSSL_FAMILY_NONE, NULL, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_HMAC, EVP_sha256, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_HMAC, EVP_sha384, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_HMAC, EVP_sha512, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_RSA, EVP_sha256, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_RSA, EVP_sha384, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_RSA, EVP_sha512, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_RSA_PSS, EVP_sha256, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_RSA_PSS, EVP_sha384, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_RSA_PSS, EVP_sha512, 0, 0 },
	{ CRYPTO_OPENSSL_FAMILY_ECDSA, EVP_sha256, CRYPTO_OPENSSL_P256_COORDINATE_SIZE, 0 },
	{ CRYPTO_OPENSSL_FAMILY_ECDSA, EVP_sha384, CRYPTO_OPENSSL_P384_COORDINATE_SIZE, 0 },
	{ CRYPTO_OPENSSL_FAMILY_ECDSA, EVP_sha512, CRYPTO_OPENSSL_P521_COORDINATE_SIZE, 0 },
	{ CRYPTO_OPENSSL_FAMILY_EDWARDS, NULL, 0, CRYPTO_OPENSSL_ED25519_SIGNATURE_SIZE },
	{ CRYPTO_OPENSSL_FAMILY_EDWARDS, NULL, 0, CRYPTO_OPENSSL_ED448_SIGNATURE_SIZE }
};

static const CryptoOpensslAlgorithm *crypto_openssl_algorithm (
	const CerverCryptoSignature algorithm
) {

	return ((unsigned int) algorithm < CERVER_CRYPTO_SIGNATURES) ?
		&crypto_openssl_algorithms[algorithm] :
		&crypto_openssl_algorithms[CERVER_CRYPTO_SIGNATURE_NONE];

}

// a curve of cerver in OpenSSL
typedef struct CryptoOpensslCurve {

	CerverCryptoCurve curve;

	int identifier;
	const char *name;

	size_t coordinate_size;

} CryptoOpensslCurve;

static const CryptoOpensslCurve crypto_openssl_curves[] = {
	{ CERVER_CRYPTO_CURVE_P256, NID_X9_62_prime256v1, SN_X9_62_prime256v1, CRYPTO_OPENSSL_P256_COORDINATE_SIZE },
	{ CERVER_CRYPTO_CURVE_P384, NID_secp384r1, SN_secp384r1, CRYPTO_OPENSSL_P384_COORDINATE_SIZE },
	{ CERVER_CRYPTO_CURVE_P521, NID_secp521r1, SN_secp521r1, CRYPTO_OPENSSL_P521_COORDINATE_SIZE }
};

#define CRYPTO_OPENSSL_CURVES					(sizeof (crypto_openssl_curves) / sizeof (crypto_openssl_curves[0]))

static const CryptoOpensslCurve *crypto_openssl_curve_with_identifier (const int identifier) {

	const CryptoOpensslCurve *found = NULL;

	for (size_t i = 0; (i < CRYPTO_OPENSSL_CURVES) && !found; i++) {
		if (crypto_openssl_curves[i].identifier == identifier) found = &crypto_openssl_curves[i];
	}

	return found;

}

static const CryptoOpensslCurve *crypto_openssl_curve_get (const CerverCryptoCurve curve) {

	const CryptoOpensslCurve *found = NULL;

	for (size_t i = 0; (i < CRYPTO_OPENSSL_CURVES) && !found; i++) {
		if (crypto_openssl_curves[i].curve == curve) found = &crypto_openssl_curves[i];
	}

	return found;

}

#pragma endregion

#pragma region hash

bool cerver_crypto_provider_sha1 (
	const void *data, const size_t size,
	u8 digest[CERVER_CRYPTO_SHA1_SIZE]
) {

	unsigned int length = 0;

	const int result = EVP_Digest (
		data ? data : &cerver_crypto_openssl_empty, size,
		digest, &length,
		EVP_sha1 (), NULL
	);

	return (result == CRYPTO_OPENSSL_SUCCESS) && (length == CERVER_CRYPTO_SHA1_SIZE);

}

bool cerver_crypto_provider_equal (
	const void *first, const void *second, const size_t size
) {

	return !CRYPTO_memcmp (first, second, size);

}

#pragma endregion

#pragma region errors

// a failure of OpenSSL leaves its errors in the queue of the thread, and the next one that reads the queue
// (the SSL_get_error () of the application) would take them as its own: what a method of the provider added
// between its mark and its drop is removed, what was there before stays
static void crypto_openssl_errors_mark (void) {

	(void) ERR_set_mark ();

}

static void crypto_openssl_errors_drop (void) {

	(void) ERR_pop_to_mark ();

}

#pragma endregion

#pragma region key

// a key of OpenSSL with what an EC key needs to give its signatures one form
typedef struct CryptoOpensslKey {

	EVP_PKEY *key;

	// the order of the curve of an EC key and its half: a signature with a greater S is replaced
	BIGNUM *order;
	BIGNUM *half_order;

} CryptoOpensslKey;

void cerver_crypto_provider_key_delete (void *provider_key) {

	if (provider_key) {
		CryptoOpensslKey *key = (CryptoOpensslKey *) provider_key;

		EVP_PKEY_free (key->key);

		BN_free (key->order);
		BN_free (key->half_order);

		free (key);
	}

}

// the curve of an EC key of OpenSSL, NULL when it is not one of the ones of cerver
static const CryptoOpensslCurve *crypto_openssl_key_curve (const EVP_PKEY *key) {

	int identifier = NID_undef;

	char name[CRYPTO_OPENSSL_GROUP_NAME_SIZE] = { 0 };
	size_t name_length = 0;
	if (EVP_PKEY_get_group_name (key, name, sizeof (name), &name_length) == CRYPTO_OPENSSL_SUCCESS) {
		identifier = OBJ_sn2nid (name);
		if (identifier == NID_undef) identifier = EC_curve_nist2nid (name);
	}

	return crypto_openssl_curve_with_identifier (identifier);

}

// the order of the curve of an EC key and its half
static bool crypto_openssl_key_order (
	CryptoOpensslKey *key, const CryptoOpensslCurve *curve
) {

	bool created = false;

	EC_GROUP *group = EC_GROUP_new_by_curve_name (curve->identifier);
	if (group) {
		key->order = BN_dup (EC_GROUP_get0_order (group));
		key->half_order = BN_new ();

		created = key->order && key->half_order && BN_rshift1 (key->half_order, key->order);

		EC_GROUP_free (group);
	}

	return created;

}

// the key of the provider around a key of OpenSSL, that it takes also when it fails;
// an EC key of a curve of cerver gets the order of its curve
static void *crypto_openssl_key_wrap (EVP_PKEY *openssl_key) {

	if (!openssl_key) return NULL;

	CryptoOpensslKey *key = (CryptoOpensslKey *) malloc (sizeof (CryptoOpensslKey));
	if (!key) {
		EVP_PKEY_free (openssl_key);
		return NULL;
	}

	key->key = openssl_key;
	key->order = NULL;
	key->half_order = NULL;

	const CryptoOpensslCurve *curve = (EVP_PKEY_get_base_id (openssl_key) == EVP_PKEY_EC) ?
		crypto_openssl_key_curve (openssl_key) : NULL;

	if (curve && !crypto_openssl_key_order (key, curve)) {
		cerver_crypto_provider_key_delete (key);
		key = NULL;
	}

	return key;

}

// a PEM is never decrypted: OpenSSL would ask for its passphrase in the terminal
static int crypto_openssl_passphrase_refused (
	char *buffer, int size, int rwflag, void *data
) {

	(void) buffer;
	(void) size;
	(void) rwflag;
	(void) data;

	return 0;

}

void *cerver_crypto_provider_key_read_pem (
	const void *pem, const size_t size, const bool is_private
) {

	EVP_PKEY *key = NULL;

	crypto_openssl_errors_mark ();

	if (size <= (size_t) INT_MAX) {
		BIO *buffer = BIO_new_mem_buf (pem, (int) size);
		if (buffer) {
			key = is_private ?
				PEM_read_bio_PrivateKey (buffer, NULL, crypto_openssl_passphrase_refused, NULL) :
				PEM_read_bio_PUBKEY (buffer, NULL, crypto_openssl_passphrase_refused, NULL);

			BIO_free (buffer);
		}
	}

	void *provider_key = crypto_openssl_key_wrap (key);

	crypto_openssl_errors_drop ();

	return provider_key;

}

bool cerver_crypto_provider_key_describe (
	const void *provider_key,
	CerverCryptoKeyType *type, unsigned int *bits, CerverCryptoCurve *curve
) {

	const EVP_PKEY *key = ((const CryptoOpensslKey *) provider_key)->key;

	const CryptoOpensslCurve *found = NULL;

	*type = CERVER_CRYPTO_KEY_TYPE_NONE;
	*bits = (unsigned int) EVP_PKEY_get_bits (key);
	*curve = CERVER_CRYPTO_CURVE_NONE;

	switch (EVP_PKEY_get_base_id (key)) {
		case EVP_PKEY_RSA: *type = CERVER_CRYPTO_KEY_TYPE_RSA; break;

		// the bits of the bytes of the key: the versions of OpenSSL do not report the same ones
		case EVP_PKEY_ED25519:
			*type = CERVER_CRYPTO_KEY_TYPE_ED25519;
			*bits = CRYPTO_OPENSSL_ED25519_KEY_SIZE * CHAR_BIT;
			break;

		case EVP_PKEY_ED448:
			*type = CERVER_CRYPTO_KEY_TYPE_ED448;
			*bits = CRYPTO_OPENSSL_ED448_KEY_SIZE * CHAR_BIT;
			break;

		// an EC key of another curve is not one of cerver
		case EVP_PKEY_EC:
			found = crypto_openssl_key_curve (key);
			if (found) *type = CERVER_CRYPTO_KEY_TYPE_EC;
			if (found) *curve = found->curve;
			break;

		default: break;
	}

	return *type != CERVER_CRYPTO_KEY_TYPE_NONE;

}

// a key of OpenSSL from the params of a builder, of the type with that name
static EVP_PKEY *crypto_openssl_key_from_builder (
	const char *type, OSSL_PARAM_BLD *builder, const int selection
) {

	EVP_PKEY *key = NULL;

	OSSL_PARAM *params = OSSL_PARAM_BLD_to_param (builder);
	EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_from_name (NULL, type, NULL);

	if (
		params && context
		&& (EVP_PKEY_fromdata_init (context) == CRYPTO_OPENSSL_SUCCESS)
		&& (EVP_PKEY_fromdata (context, &key, selection, params) != CRYPTO_OPENSSL_SUCCESS)
	) {
		key = NULL;
	}

	EVP_PKEY_CTX_free (context);
	OSSL_PARAM_free (params);

	return key;

}

// a key that was created from its numbers is one: a public point of its curve, or a private key whose numbers agree
static bool crypto_openssl_key_is_valid (EVP_PKEY *key, const bool is_private) {

	bool valid = false;

	EVP_PKEY_CTX *context = EVP_PKEY_CTX_new_from_pkey (NULL, key, NULL);
	if (context) {
		valid = (
			is_private ? EVP_PKEY_check (context) : EVP_PKEY_public_check (context)
		) == CRYPTO_OPENSSL_SUCCESS;

		EVP_PKEY_CTX_free (context);
	}

	return valid;

}

// a key of OpenSSL that is not valid is released
static EVP_PKEY *crypto_openssl_key_validated (EVP_PKEY *key, const bool is_private) {

	if (key && !crypto_openssl_key_is_valid (key, is_private)) {
		EVP_PKEY_free (key);
		key = NULL;
	}

	return key;

}

// a number of a key as a param of its builder; a part that the key does not have is skipped,
// and one with more bytes than the modulus of the largest key of RSA is not a number of a key:
// the provider would check numbers of any size
static bool crypto_openssl_number_push (
	OSSL_PARAM_BLD *builder, const char *name,
	const CryptoBytes bytes, BIGNUM **number
) {

	if (!bytes.bytes) return true;

	if (bytes.size > CERVER_CRYPTO_SIGNATURE_SIZE_MAX) return false;

	*number = BN_bin2bn (bytes.bytes, (int) bytes.size, NULL);

	return *number && (OSSL_PARAM_BLD_push_BN (builder, name, *number) == CRYPTO_OPENSSL_SUCCESS);

}

static bool crypto_openssl_rsa_push (
	OSSL_PARAM_BLD *builder, const CryptoRsaParts *parts,
	BIGNUM *numbers[CRYPTO_OPENSSL_RSA_NUMBERS]
) {

	return crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_N, parts->modulus, &numbers[0])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_E, parts->exponent, &numbers[1])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_D, parts->private_exponent, &numbers[2])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_FACTOR1, parts->first_prime, &numbers[3])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_FACTOR2, parts->second_prime, &numbers[4])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_EXPONENT1, parts->first_exponent, &numbers[5])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_EXPONENT2, parts->second_exponent, &numbers[6])
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_RSA_COEFFICIENT1, parts->coefficient, &numbers[7]);

}

void *cerver_crypto_provider_key_create_rsa (const CryptoRsaParts *parts) {

	EVP_PKEY *key = NULL;

	const bool is_private = parts->private_exponent.bytes != NULL;

	BIGNUM *numbers[CRYPTO_OPENSSL_RSA_NUMBERS] = { NULL };

	crypto_openssl_errors_mark ();

	OSSL_PARAM_BLD *builder = OSSL_PARAM_BLD_new ();
	if (builder && crypto_openssl_rsa_push (builder, parts, numbers)) {
		key = crypto_openssl_key_validated (
			crypto_openssl_key_from_builder (
				SN_rsa, builder, is_private ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY
			),
			is_private
		);
	}

	OSSL_PARAM_BLD_free (builder);

	for (size_t i = 0; i < CRYPTO_OPENSSL_RSA_NUMBERS; i++) BN_clear_free (numbers[i]);

	void *provider_key = crypto_openssl_key_wrap (key);

	crypto_openssl_errors_drop ();

	return provider_key;

}

// the public point of a curve without compression: its mark, x and y
static size_t crypto_openssl_point_write (
	u8 point[CRYPTO_OPENSSL_POINT_SIZE],
	const CryptoOpensslCurve *curve, const CryptoBytes x, const CryptoBytes y
) {

	if ((x.size != curve->coordinate_size) || (y.size != curve->coordinate_size)) return 0;

	point[0] = CRYPTO_OPENSSL_POINT_UNCOMPRESSED;
	(void) memcpy (point + 1, x.bytes, x.size);
	(void) memcpy (point + 1 + x.size, y.bytes, y.size);

	return 1 + x.size + y.size;

}

static bool crypto_openssl_curve_push (
	OSSL_PARAM_BLD *builder, const CryptoOpensslCurve *curve,
	const u8 *point, const size_t point_size,
	const CryptoBytes secret, BIGNUM **number
) {

	return point_size
		&& (OSSL_PARAM_BLD_push_utf8_string (builder, OSSL_PKEY_PARAM_GROUP_NAME, curve->name, 0) == CRYPTO_OPENSSL_SUCCESS)
		&& (OSSL_PARAM_BLD_push_octet_string (builder, OSSL_PKEY_PARAM_PUB_KEY, point, point_size) == CRYPTO_OPENSSL_SUCCESS)
		&& crypto_openssl_number_push (builder, OSSL_PKEY_PARAM_PRIV_KEY, secret, number);

}

void *cerver_crypto_provider_key_create_curve (
	const CerverCryptoCurve curve,
	const CryptoBytes x, const CryptoBytes y, const CryptoBytes secret
) {

	EVP_PKEY *key = NULL;

	const bool is_private = secret.bytes != NULL;

	const CryptoOpensslCurve *found = crypto_openssl_curve_get (curve);
	if (!found || !x.bytes || !y.bytes) return NULL;

	// the private number has the bytes of its curve, like the coordinates (RFC 7518 section 6.2.2.1)
	if (secret.bytes && (secret.size != found->coordinate_size)) return NULL;

	u8 point[CRYPTO_OPENSSL_POINT_SIZE] = { 0 };
	const size_t point_size = crypto_openssl_point_write (point, found, x, y);

	BIGNUM *number = NULL;

	crypto_openssl_errors_mark ();

	OSSL_PARAM_BLD *builder = OSSL_PARAM_BLD_new ();
	if (builder && crypto_openssl_curve_push (builder, found, point, point_size, secret, &number)) {
		key = crypto_openssl_key_validated (
			crypto_openssl_key_from_builder (
				SN_X9_62_id_ecPublicKey, builder, is_private ? EVP_PKEY_KEYPAIR : EVP_PKEY_PUBLIC_KEY
			),
			is_private
		);
	}

	OSSL_PARAM_BLD_free (builder);

	BN_clear_free (number);

	void *provider_key = crypto_openssl_key_wrap (key);

	crypto_openssl_errors_drop ();

	return provider_key;

}

// the public key of a private key of an Edwards curve is the one that came with it
static bool crypto_openssl_edwards_matches (
	const EVP_PKEY *key, const CryptoBytes point
) {

	u8 derived[CRYPTO_OPENSSL_ED448_KEY_SIZE] = { 0 };
	size_t derived_size = sizeof (derived);

	return (EVP_PKEY_get_raw_public_key (key, derived, &derived_size) == CRYPTO_OPENSSL_SUCCESS)
		&& (derived_size == point.size)
		&& !memcmp (derived, point.bytes, point.size);

}

void *cerver_crypto_provider_key_create_edwards (
	const CerverCryptoKeyType type,
	const CryptoBytes point, const CryptoBytes seed
) {

	EVP_PKEY *key = NULL;

	const int identifier = (type == CERVER_CRYPTO_KEY_TYPE_ED25519) ? EVP_PKEY_ED25519 : EVP_PKEY_ED448;
	const size_t key_size = (type == CERVER_CRYPTO_KEY_TYPE_ED25519) ?
		CRYPTO_OPENSSL_ED25519_KEY_SIZE : CRYPTO_OPENSSL_ED448_KEY_SIZE;

	if (!point.bytes || (point.size != key_size)) return NULL;

	crypto_openssl_errors_mark ();

	if (!seed.bytes) {
		key = EVP_PKEY_new_raw_public_key (identifier, NULL, point.bytes, point.size);
	}

	else if (seed.size == key_size) {
		key = EVP_PKEY_new_raw_private_key (identifier, NULL, seed.bytes, seed.size);

		if (key && !crypto_openssl_edwards_matches (key, point)) {
			EVP_PKEY_free (key);
			key = NULL;
		}
	}

	void *provider_key = crypto_openssl_key_wrap (key);

	crypto_openssl_errors_drop ();

	return provider_key;

}

#pragma endregion

#pragma region signature

size_t cerver_crypto_provider_signature_size (
	const void *provider_key, const CerverCryptoSignature algorithm
) {

	const CryptoOpensslAlgorithm *found = crypto_openssl_algorithm (algorithm);

	size_t size = 0;

	switch (found->family) {
		case CRYPTO_OPENSSL_FAMILY_RSA:
		case CRYPTO_OPENSSL_FAMILY_RSA_PSS:
			size = (size_t) EVP_PKEY_get_size (((const CryptoOpensslKey *) provider_key)->key);
			break;

		case CRYPTO_OPENSSL_FAMILY_ECDSA:
			size = CRYPTO_OPENSSL_SIGNATURE_PARTS * found->coordinate_size;
			break;

		case CRYPTO_OPENSSL_FAMILY_EDWARDS: size = found->signature_size; break;

		default: break;
	}

	return size;

}

bool cerver_crypto_provider_mac (
	const CerverCryptoSignature algorithm,
	const u8 *secret, const size_t secret_size,
	const void *message, const size_t message_size,
	u8 mac[CERVER_CRYPTO_MAC_SIZE_MAX], size_t *mac_size
) {

	const CryptoOpensslAlgorithm *found = crypto_openssl_algorithm (algorithm);

	unsigned int length = 0;

	const bool computed = (found->family == CRYPTO_OPENSSL_FAMILY_HMAC)
		&& (secret_size <= (size_t) INT_MAX)
		&& HMAC (
			found->digest (), secret, (int) secret_size,
			message ? (const unsigned char *) message : &cerver_crypto_openssl_empty, message_size,
			mac, &length
		);

	*mac_size = computed ? length : 0;

	return computed;

}

// the salt of PSS has the size of its digest, and its mask is MGF1 with the digest of the signature
static bool crypto_openssl_context_pss (EVP_PKEY_CTX *key_context) {

	return (EVP_PKEY_CTX_set_rsa_padding (key_context, RSA_PKCS1_PSS_PADDING) == CRYPTO_OPENSSL_SUCCESS)
		&& (EVP_PKEY_CTX_set_rsa_pss_saltlen (key_context, RSA_PSS_SALTLEN_DIGEST) == CRYPTO_OPENSSL_SUCCESS);

}

// starts a signature or its verification with the key and what its algorithm needs
static bool crypto_openssl_context_init (
	EVP_MD_CTX *context, const CryptoOpensslAlgorithm *found,
	EVP_PKEY *key, const bool signs
) {

	// it belongs to the context of the digest
	EVP_PKEY_CTX *key_context = NULL;

	const EVP_MD *digest = found->digest ? found->digest () : NULL;

	bool ready = (
		signs ?
			EVP_DigestSignInit (context, &key_context, digest, NULL, key) :
			EVP_DigestVerifyInit (context, &key_context, digest, NULL, key)
	) == CRYPTO_OPENSSL_SUCCESS;

	if (ready && (found->family == CRYPTO_OPENSSL_FAMILY_RSA_PSS)) {
		ready = crypto_openssl_context_pss (key_context);
	}

	return ready;

}

// the DER of the signature of a curve as R and S, each one with the bytes of the curve,
// with S replaced by order - S when it is in the upper half of the order
static bool crypto_openssl_der_to_raw (
	const CryptoOpensslKey *key, const size_t coordinate_size,
	const u8 *der, const size_t der_size,
	u8 *signature, size_t *size
) {

	bool converted = false;

	const unsigned char *der_position = der;
	ECDSA_SIG *curve_signature = d2i_ECDSA_SIG (NULL, &der_position, (long) der_size);
	BIGNUM *low_s = BN_new ();
	if (curve_signature && low_s) {
		const BIGNUM *r = NULL;
		const BIGNUM *s = NULL;
		ECDSA_SIG_get0 (curve_signature, &r, &s);

		converted = ((BN_cmp (s, key->half_order) > 0) ? BN_sub (low_s, key->order, s) : (BN_copy (low_s, s) != NULL))
			&& (BN_bn2binpad (r, signature, (int) coordinate_size) > 0)
			&& (BN_bn2binpad (low_s, signature + coordinate_size, (int) coordinate_size) > 0);

		if (converted) *size = CRYPTO_OPENSSL_SIGNATURE_PARTS * coordinate_size;
	}

	BN_free (low_s);
	ECDSA_SIG_free (curve_signature);

	return converted;

}

static bool crypto_openssl_sign_curve (
	const CryptoOpensslKey *key, const CryptoOpensslAlgorithm *found,
	EVP_MD_CTX *context,
	const unsigned char *message, const size_t message_size,
	u8 *signature, size_t *size
) {

	u8 der[CRYPTO_OPENSSL_DER_SIZE] = { 0 };
	size_t der_size = CRYPTO_OPENSSL_DER_SIZE;

	return (EVP_DigestSign (context, der, &der_size, message, message_size) == CRYPTO_OPENSSL_SUCCESS)
		&& crypto_openssl_der_to_raw (key, found->coordinate_size, der, der_size, signature, size);

}

// R and S of the signature of a curve as the DER that OpenSSL verifies
// returns the bytes of the DER, 0 when the signature does not have the bytes of the curve twice
static size_t crypto_openssl_raw_to_der (
	const size_t coordinate_size,
	const u8 *signature, const size_t signature_size,
	u8 der[CRYPTO_OPENSSL_DER_SIZE]
) {

	size_t der_size = 0;

	if (signature_size != (CRYPTO_OPENSSL_SIGNATURE_PARTS * coordinate_size)) return der_size;

	ECDSA_SIG *curve_signature = ECDSA_SIG_new ();
	BIGNUM *r = BN_bin2bn (signature, (int) coordinate_size, NULL);
	BIGNUM *s = BN_bin2bn (signature + coordinate_size, (int) coordinate_size, NULL);

	if (curve_signature && r && s && ECDSA_SIG_set0 (curve_signature, r, s)) {
		// the signature owns them now
		r = NULL;
		s = NULL;

		const int needed = i2d_ECDSA_SIG (curve_signature, NULL);
		if ((needed > 0) && (needed <= CRYPTO_OPENSSL_DER_SIZE)) {
			unsigned char *der_position = der;
			der_size = (size_t) i2d_ECDSA_SIG (curve_signature, &der_position);
		}
	}

	BN_free (r);
	BN_free (s);
	ECDSA_SIG_free (curve_signature);

	return der_size;

}

static bool crypto_openssl_verify_curve (
	const CryptoOpensslAlgorithm *found, EVP_MD_CTX *context,
	const unsigned char *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
) {

	u8 der[CRYPTO_OPENSSL_DER_SIZE] = { 0 };
	const size_t der_size = crypto_openssl_raw_to_der (found->coordinate_size, signature, signature_size, der);

	return der_size
		&& (EVP_DigestVerify (context, der, der_size, message, message_size) == CRYPTO_OPENSSL_SUCCESS);

}

// the signature of a message with a key of OpenSSL and an algorithm
static bool crypto_openssl_verify (
	const CryptoOpensslKey *key, const CryptoOpensslAlgorithm *found,
	const unsigned char *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
) {

	bool result = false;

	EVP_MD_CTX *context = EVP_MD_CTX_new ();
	if (context) {
		if (crypto_openssl_context_init (context, found, key->key, false)) {
			result = (found->family == CRYPTO_OPENSSL_FAMILY_ECDSA) ?
				crypto_openssl_verify_curve (found, context, message, message_size, signature, signature_size) :
				(EVP_DigestVerify (context, signature, signature_size, message, message_size) == CRYPTO_OPENSSL_SUCCESS);
		}

		EVP_MD_CTX_free (context);
	}

	return result;

}

bool cerver_crypto_provider_verify (
	const void *provider_key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	const u8 *signature, const size_t signature_size
) {

	const CryptoOpensslKey *key = (const CryptoOpensslKey *) provider_key;
	const CryptoOpensslAlgorithm *found = crypto_openssl_algorithm (algorithm);

	const unsigned char *bytes = message ? (const unsigned char *) message : &cerver_crypto_openssl_empty;

	crypto_openssl_errors_mark ();

	const bool result = crypto_openssl_verify (key, found, bytes, message_size, signature, signature_size);

	crypto_openssl_errors_drop ();

	return result;

}

// the signature that a key of OpenSSL computes for a message
static bool crypto_openssl_sign (
	const CryptoOpensslKey *key, const CryptoOpensslAlgorithm *found,
	const unsigned char *message, const size_t message_size,
	u8 *signature, size_t *size
) {

	bool result = false;

	EVP_MD_CTX *context = EVP_MD_CTX_new ();
	if (context) {
		if (crypto_openssl_context_init (context, found, key->key, true)) {
			result = (found->family == CRYPTO_OPENSSL_FAMILY_ECDSA) ?
				crypto_openssl_sign_curve (key, found, context, message, message_size, signature, size) :
				(EVP_DigestSign (context, signature, size, message, message_size) == CRYPTO_OPENSSL_SUCCESS);
		}

		EVP_MD_CTX_free (context);
	}

	return result;

}

static bool crypto_openssl_family_is_rsa (const CryptoOpensslFamily family) {

	return (family == CRYPTO_OPENSSL_FAMILY_RSA) || (family == CRYPTO_OPENSSL_FAMILY_RSA_PSS);

}

// a signature of RSA that its own key does not verify is never given: it is erased and the signature fails.
// A fault in the computation of a signature of RSA can give away its key, and with OpenSSL 3.5 an allocation
// that failed inside a signature made it, and the next ones of the same key, a wrong signature that was
// returned without an error (test/faults/crypto.c makes every allocation of a signature fail).
// The verification of RSA costs a few percent of its signature; the one of a curve costs more than its
// signature, and the same test never saw a curve sign wrong
static bool crypto_openssl_sign_checked (
	const CryptoOpensslKey *key, const CryptoOpensslAlgorithm *found,
	const unsigned char *message, const size_t message_size,
	u8 *signature, size_t *size
) {

	bool result = crypto_openssl_sign (key, found, message, message_size, signature, size);

	if (
		result && crypto_openssl_family_is_rsa (found->family)
		&& !crypto_openssl_verify (key, found, message, message_size, signature, *size)
	) {
		OPENSSL_cleanse (signature, *size);
		result = false;
	}

	return result;

}

bool cerver_crypto_provider_sign (
	const void *provider_key, const CerverCryptoSignature algorithm,
	const void *message, const size_t message_size,
	u8 *signature, size_t *size
) {

	const CryptoOpensslKey *key = (const CryptoOpensslKey *) provider_key;
	const CryptoOpensslAlgorithm *found = crypto_openssl_algorithm (algorithm);

	const unsigned char *bytes = message ? (const unsigned char *) message : &cerver_crypto_openssl_empty;

	*size = cerver_crypto_provider_signature_size (provider_key, algorithm);

	crypto_openssl_errors_mark ();

	const bool result = crypto_openssl_sign_checked (key, found, bytes, message_size, signature, size);

	crypto_openssl_errors_drop ();

	if (!result) *size = 0;

	return result;

}

#pragma endregion

#endif
