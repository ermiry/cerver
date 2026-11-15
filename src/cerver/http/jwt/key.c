#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/objects.h>
#include <openssl/pem.h>

#include "cerver/config.h"

#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/key.h"

// the size of the name of a curve that OpenSSL gives
#define JWT_KEY_GROUP_NAME_SIZE				64

#define JWT_KEY_ES256_COORDINATE_SIZE		32
#define JWT_KEY_ES384_COORDINATE_SIZE		48
#define JWT_KEY_ES512_COORDINATE_SIZE		66

#define JWT_KEY_RAW_SIGNATURE_PARTS			2

typedef enum JwtKeyFamily {

	JWT_KEY_FAMILY_NONE		= 0,
	JWT_KEY_FAMILY_HMAC		= 1,
	JWT_KEY_FAMILY_RSA		= 2,
	JWT_KEY_FAMILY_EC		= 3

} JwtKeyFamily;

// what each algorithm needs from its key
typedef struct JwtKeyAlgorithm {

	JwtKeyFamily family;

	const EVP_MD *(*digest)(void);

	// the NID of the curve of an ES algorithm
	int curve;

	// the size of each of R and S in an ES signature
	size_t coordinate_size;

} JwtKeyAlgorithm;

static const JwtKeyAlgorithm jwt_key_algorithm_none = { JWT_KEY_FAMILY_NONE, NULL, NID_undef, 0 };

static const JwtKeyAlgorithm jwt_key_algorithm_hs256 = { JWT_KEY_FAMILY_HMAC, EVP_sha256, NID_undef, 0 };
static const JwtKeyAlgorithm jwt_key_algorithm_hs384 = { JWT_KEY_FAMILY_HMAC, EVP_sha384, NID_undef, 0 };
static const JwtKeyAlgorithm jwt_key_algorithm_hs512 = { JWT_KEY_FAMILY_HMAC, EVP_sha512, NID_undef, 0 };

static const JwtKeyAlgorithm jwt_key_algorithm_rs256 = { JWT_KEY_FAMILY_RSA, EVP_sha256, NID_undef, 0 };
static const JwtKeyAlgorithm jwt_key_algorithm_rs384 = { JWT_KEY_FAMILY_RSA, EVP_sha384, NID_undef, 0 };
static const JwtKeyAlgorithm jwt_key_algorithm_rs512 = { JWT_KEY_FAMILY_RSA, EVP_sha512, NID_undef, 0 };

static const JwtKeyAlgorithm jwt_key_algorithm_es256 = {
	JWT_KEY_FAMILY_EC, EVP_sha256, NID_X9_62_prime256v1, JWT_KEY_ES256_COORDINATE_SIZE
};

static const JwtKeyAlgorithm jwt_key_algorithm_es384 = {
	JWT_KEY_FAMILY_EC, EVP_sha384, NID_secp384r1, JWT_KEY_ES384_COORDINATE_SIZE
};

static const JwtKeyAlgorithm jwt_key_algorithm_es512 = {
	JWT_KEY_FAMILY_EC, EVP_sha512, NID_secp521r1, JWT_KEY_ES512_COORDINATE_SIZE
};

static const JwtKeyAlgorithm *jwt_key_algorithm_get (const jwt_alg_t alg) {

	const JwtKeyAlgorithm *algorithm = &jwt_key_algorithm_none;

	switch (alg) {
		case JWT_ALG_HS256: algorithm = &jwt_key_algorithm_hs256; break;
		case JWT_ALG_HS384: algorithm = &jwt_key_algorithm_hs384; break;
		case JWT_ALG_HS512: algorithm = &jwt_key_algorithm_hs512; break;

		case JWT_ALG_RS256: algorithm = &jwt_key_algorithm_rs256; break;
		case JWT_ALG_RS384: algorithm = &jwt_key_algorithm_rs384; break;
		case JWT_ALG_RS512: algorithm = &jwt_key_algorithm_rs512; break;

		case JWT_ALG_ES256: algorithm = &jwt_key_algorithm_es256; break;
		case JWT_ALG_ES384: algorithm = &jwt_key_algorithm_es384; break;
		case JWT_ALG_ES512: algorithm = &jwt_key_algorithm_es512; break;

		default: break;
	}

	return algorithm;

}

const char *jwt_key_type_to_string (const JwtKeyType type) {

	switch (type) {
		#define XX(num, name, string) case JWT_KEY_TYPE_##name: return #string;
		JWT_KEY_TYPE_MAP(XX)
		#undef XX
	}

	return NULL;

}

const char *jwt_key_error_to_string (const JwtKeyError error) {

	switch (error) {
		#define XX(num, name, string) case JWT_KEY_ERROR_##name: return #string;
		JWT_KEY_ERROR_MAP(XX)
		#undef XX
	}

	return NULL;

}

#pragma region create

static JwtKey *jwt_key_new (const jwt_alg_t alg, const JwtKeyType type) {

	JwtKey *key = (JwtKey *) malloc (sizeof (JwtKey));
	if (key) {
		key->alg = alg;
		key->type = type;

		key->secret = NULL;
		key->secret_length = 0;

		key->key = NULL;

		key->order = NULL;
		key->half_order = NULL;
	}

	return key;

}

void jwt_key_delete (JwtKey *key) {

	if (key) {
		if (key->secret) {
			OPENSSL_cleanse (key->secret, key->secret_length);
			free (key->secret);
		}

		EVP_PKEY_free (key->key);

		BN_free (key->order);
		BN_free (key->half_order);

		free (key);
	}

}

static JwtKeyError jwt_key_create_secret (
	JwtKey *key, const unsigned char *material, const size_t material_length
) {

	JwtKeyError error = JWT_KEY_ERROR_MEMORY;

	key->secret = (unsigned char *) malloc (material_length);
	if (key->secret) {
		(void) memcpy (key->secret, material, material_length);
		key->secret_length = material_length;

		error = JWT_KEY_ERROR_NONE;
	}

	return error;

}

// a PEM is never decrypted: OpenSSL would ask for its passphrase in the terminal
static int jwt_key_passphrase_refused (
	char *buffer, int size, int rwflag, void *data
) {

	(void) buffer;
	(void) size;
	(void) rwflag;
	(void) data;

	return 0;

}

static EVP_PKEY *jwt_key_read_pem (
	const JwtKeyType type,
	const unsigned char *material, const size_t material_length
) {

	EVP_PKEY *pem_key = NULL;

	if (material_length <= (size_t) INT_MAX) {
		BIO *buffer = BIO_new_mem_buf (material, (int) material_length);
		if (buffer) {
			pem_key = (type == JWT_KEY_TYPE_PRIVATE) ?
				PEM_read_bio_PrivateKey (buffer, NULL, jwt_key_passphrase_refused, NULL) :
				PEM_read_bio_PUBKEY (buffer, NULL, jwt_key_passphrase_refused, NULL);

			BIO_free (buffer);
		}
	}

	return pem_key;

}

static int jwt_key_curve (const EVP_PKEY *pem_key) {

	int curve = NID_undef;

	char name[JWT_KEY_GROUP_NAME_SIZE] = { 0 };
	size_t name_length = 0;
	if (EVP_PKEY_get_group_name (pem_key, name, sizeof (name), &name_length) == 1) {
		curve = OBJ_sn2nid (name);
		if (curve == NID_undef) curve = EC_curve_nist2nid (name);
	}

	return curve;

}

// the order of the curve and its half, a signature with a greater S is replaced
static JwtKeyError jwt_key_create_order (JwtKey *key, const int curve) {

	JwtKeyError error = JWT_KEY_ERROR_MEMORY;

	EC_GROUP *group = EC_GROUP_new_by_curve_name (curve);
	if (group) {
		key->order = BN_dup (EC_GROUP_get0_order (group));
		key->half_order = BN_new ();

		if (key->order && key->half_order && BN_rshift1 (key->half_order, key->order)) {
			error = JWT_KEY_ERROR_NONE;
		}

		EC_GROUP_free (group);
	}

	return error;

}

static JwtKeyError jwt_key_create_pem (
	JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const unsigned char *material, const size_t material_length
) {

	JwtKeyError error = JWT_KEY_ERROR_NONE;

	key->key = jwt_key_read_pem (key->type, material, material_length);
	if (!key->key) {
		error = JWT_KEY_ERROR_PEM;
	}

	else if (algorithm->family == JWT_KEY_FAMILY_RSA) {
		if (EVP_PKEY_get_base_id (key->key) != EVP_PKEY_RSA) error = JWT_KEY_ERROR_TYPE;
	}

	else if (EVP_PKEY_get_base_id (key->key) != EVP_PKEY_EC) {
		error = JWT_KEY_ERROR_TYPE;
	}

	else if (jwt_key_curve (key->key) != algorithm->curve) {
		error = JWT_KEY_ERROR_CURVE;
	}

	else {
		error = jwt_key_create_order (key, algorithm->curve);
	}

	return error;

}

JwtKey *jwt_key_create (
	const jwt_alg_t alg, const JwtKeyType type,
	const unsigned char *material, const size_t material_length,
	JwtKeyError *error
) {

	JwtKeyError result = JWT_KEY_ERROR_NONE;

	JwtKey *key = NULL;

	const JwtKeyAlgorithm *algorithm = jwt_key_algorithm_get (alg);
	if (algorithm->family == JWT_KEY_FAMILY_NONE) {
		result = JWT_KEY_ERROR_ALGORITHM;
	}

	else if (!material || !material_length) {
		result = JWT_KEY_ERROR_EMPTY;
	}

	else {
		key = jwt_key_new (alg, type);
		if (!key) {
			result = JWT_KEY_ERROR_MEMORY;
		}

		else {
			result = (algorithm->family == JWT_KEY_FAMILY_HMAC) ?
				jwt_key_create_secret (key, material, material_length) :
				jwt_key_create_pem (key, algorithm, material, material_length);
		}
	}

	if (result != JWT_KEY_ERROR_NONE) {
		jwt_key_delete (key);
		key = NULL;
	}

	if (error) *error = result;

	return key;

}

static bool jwt_key_secret_is_pem (const JwtKey *key) {

	const size_t prefix_length = strlen (JWT_KEY_PEM_PREFIX);

	bool is_pem = false;
	for (size_t i = 0; (i + prefix_length) <= key->secret_length && !is_pem; i++) {
		is_pem = !memcmp (key->secret + i, JWT_KEY_PEM_PREFIX, prefix_length);
	}

	return is_pem;

}

JwtKeyError jwt_key_check_strength (const JwtKey *key) {

	JwtKeyError error = JWT_KEY_ERROR_NONE;

	if (!key) return JWT_KEY_ERROR_EMPTY;

	const JwtKeyAlgorithm *algorithm = jwt_key_algorithm_get (key->alg);
	switch (algorithm->family) {
		case JWT_KEY_FAMILY_HMAC: {
			if (key->secret_length < (size_t) EVP_MD_get_size (algorithm->digest ())) {
				error = JWT_KEY_ERROR_STRENGTH;
			}

			else if (jwt_key_secret_is_pem (key)) {
				error = JWT_KEY_ERROR_SECRET;
			}
		} break;

		case JWT_KEY_FAMILY_RSA: {
			if (EVP_PKEY_get_bits (key->key) < JWT_KEY_RSA_MINIMUM_BITS) {
				error = JWT_KEY_ERROR_STRENGTH;
			}
		} break;

		// the curve was checked when the key was created
		case JWT_KEY_FAMILY_EC: break;

		default: error = JWT_KEY_ERROR_ALGORITHM; break;
	}

	return error;

}

#pragma endregion

#pragma region hmac

static int jwt_key_hmac (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const char *message, const size_t message_length,
	unsigned char *mac, unsigned int *mac_length
) {

	return HMAC (
		algorithm->digest (),
		key->secret, (int) key->secret_length,
		(const unsigned char *) message, message_length,
		mac, mac_length
	) ? 0 : EINVAL;

}

static int jwt_key_sign_hmac (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const char *message, const size_t message_length,
	unsigned char **signature, size_t *signature_length
) {

	unsigned char mac[EVP_MAX_MD_SIZE] = { 0 };
	unsigned int mac_length = 0;

	int retval = jwt_key_hmac (key, algorithm, message, message_length, mac, &mac_length);
	if (!retval) {
		*signature = (unsigned char *) malloc (mac_length);
		if (*signature) {
			(void) memcpy (*signature, mac, mac_length);
			*signature_length = mac_length;
		}

		else {
			retval = ENOMEM;
		}
	}

	OPENSSL_cleanse (mac, sizeof (mac));

	return retval;

}

static int jwt_key_verify_hmac (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const char *message, const size_t message_length,
	const unsigned char *signature, const size_t signature_length
) {

	unsigned char mac[EVP_MAX_MD_SIZE] = { 0 };
	unsigned int mac_length = 0;

	int retval = jwt_key_hmac (key, algorithm, message, message_length, mac, &mac_length);
	if (!retval) {
		if (
			(signature_length != mac_length)
			|| CRYPTO_memcmp (mac, signature, mac_length)
		) retval = EINVAL;
	}

	OPENSSL_cleanse (mac, sizeof (mac));

	return retval;

}

#pragma endregion

#pragma region pem

// the DER signature of an ES key as the raw R and S, each one padded to the size of the curve,
// with S replaced by order - S if it is in the upper half of the order
static int jwt_key_der_to_raw (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const unsigned char *der, const size_t der_length,
	unsigned char **signature, size_t *signature_length
) {

	int retval = EINVAL;

	const unsigned char *der_position = der;
	ECDSA_SIG *ec_signature = d2i_ECDSA_SIG (NULL, &der_position, (long) der_length);
	BIGNUM *low_s = BN_new ();
	if (ec_signature && low_s) {
		const BIGNUM *r = NULL;
		const BIGNUM *s = NULL;
		ECDSA_SIG_get0 (ec_signature, &r, &s);

		if ((BN_cmp (s, key->half_order) > 0) ? BN_sub (low_s, key->order, s) : (BN_copy (low_s, s) != NULL)) {
			const size_t raw_length = JWT_KEY_RAW_SIGNATURE_PARTS * algorithm->coordinate_size;

			*signature = (unsigned char *) malloc (raw_length);
			if (!*signature) {
				retval = ENOMEM;
			}

			else if (
				(BN_bn2binpad (r, *signature, (int) algorithm->coordinate_size) > 0)
				&& (BN_bn2binpad (low_s, *signature + algorithm->coordinate_size, (int) algorithm->coordinate_size) > 0)
			) {
				*signature_length = raw_length;
				retval = 0;
			}

			else {
				free (*signature);
				*signature = NULL;
			}
		}
	}

	BN_free (low_s);
	ECDSA_SIG_free (ec_signature);

	return retval;

}

static int jwt_key_sign_pem_final (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	EVP_MD_CTX *context,
	unsigned char **signature, size_t *signature_length
) {

	int retval = EINVAL;

	size_t length = 0;
	if (EVP_DigestSignFinal (context, NULL, &length) == 1) {
		unsigned char *der = (unsigned char *) malloc (length);
		if (!der) {
			retval = ENOMEM;
		}

		else if (EVP_DigestSignFinal (context, der, &length) == 1) {
			if (algorithm->family == JWT_KEY_FAMILY_EC) {
				retval = jwt_key_der_to_raw (key, algorithm, der, length, signature, signature_length);
				free (der);
			}

			else {
				*signature = der;
				*signature_length = length;
				retval = 0;
			}
		}

		else {
			free (der);
		}
	}

	return retval;

}

static int jwt_key_sign_pem (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const char *message, const size_t message_length,
	unsigned char **signature, size_t *signature_length
) {

	int retval = EINVAL;

	if (key->type != JWT_KEY_TYPE_PRIVATE) return retval;

	EVP_MD_CTX *context = EVP_MD_CTX_new ();
	if (!context) {
		retval = ENOMEM;
	}

	else {
		if (
			(EVP_DigestSignInit (context, NULL, algorithm->digest (), NULL, key->key) == 1)
			&& (EVP_DigestSignUpdate (context, message, message_length) == 1)
		) {
			retval = jwt_key_sign_pem_final (key, algorithm, context, signature, signature_length);
		}

		EVP_MD_CTX_free (context);
	}

	return retval;

}

// the raw R and S of an ES signature as the DER that OpenSSL verifies
// returns the length of the DER in der, or 0 on error
static int jwt_key_raw_to_der (
	const JwtKeyAlgorithm *algorithm,
	const unsigned char *signature, const size_t signature_length,
	unsigned char **der
) {

	int der_length = 0;

	if (signature_length != (JWT_KEY_RAW_SIGNATURE_PARTS * algorithm->coordinate_size)) return der_length;

	ECDSA_SIG *ec_signature = ECDSA_SIG_new ();
	BIGNUM *r = BN_bin2bn (signature, (int) algorithm->coordinate_size, NULL);
	BIGNUM *s = BN_bin2bn (signature + algorithm->coordinate_size, (int) algorithm->coordinate_size, NULL);

	if (ec_signature && r && s && ECDSA_SIG_set0 (ec_signature, r, s)) {
		// the signature owns them now
		r = NULL;
		s = NULL;

		*der = NULL;
		der_length = i2d_ECDSA_SIG (ec_signature, der);
		if (der_length < 0) der_length = 0;
	}

	BN_free (r);
	BN_free (s);
	ECDSA_SIG_free (ec_signature);

	return der_length;

}

static int jwt_key_verify_pem_digest (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const char *message, const size_t message_length,
	const unsigned char *signature, const size_t signature_length
) {

	int retval = ENOMEM;

	EVP_MD_CTX *context = EVP_MD_CTX_new ();
	if (context) {
		retval = (
			(EVP_DigestVerifyInit (context, NULL, algorithm->digest (), NULL, key->key) == 1)
			&& (EVP_DigestVerifyUpdate (context, message, message_length) == 1)
			&& (EVP_DigestVerifyFinal (context, signature, signature_length) == 1)
		) ? 0 : EINVAL;

		EVP_MD_CTX_free (context);
	}

	return retval;

}

static int jwt_key_verify_pem (
	const JwtKey *key, const JwtKeyAlgorithm *algorithm,
	const char *message, const size_t message_length,
	const unsigned char *signature, const size_t signature_length
) {

	int retval = EINVAL;

	if (algorithm->family == JWT_KEY_FAMILY_EC) {
		unsigned char *der = NULL;
		int der_length = jwt_key_raw_to_der (algorithm, signature, signature_length, &der);
		if (der_length > 0) {
			retval = jwt_key_verify_pem_digest (
				key, algorithm, message, message_length, der, (size_t) der_length
			);
		}

		OPENSSL_free (der);
	}

	else {
		retval = jwt_key_verify_pem_digest (
			key, algorithm, message, message_length, signature, signature_length
		);
	}

	return retval;

}

#pragma endregion

int jwt_key_sign (
	const JwtKey *key,
	const char *message, const size_t message_length,
	unsigned char **signature, size_t *signature_length
) {

	if (!key || !message || !signature || !signature_length) return EINVAL;

	*signature = NULL;
	*signature_length = 0;

	const JwtKeyAlgorithm *algorithm = jwt_key_algorithm_get (key->alg);

	int retval = EINVAL;
	switch (algorithm->family) {
		case JWT_KEY_FAMILY_HMAC:
			retval = jwt_key_sign_hmac (key, algorithm, message, message_length, signature, signature_length);
			break;

		case JWT_KEY_FAMILY_RSA:
		case JWT_KEY_FAMILY_EC:
			retval = jwt_key_sign_pem (key, algorithm, message, message_length, signature, signature_length);
			break;

		default: break;
	}

	return retval;

}

int jwt_key_verify (
	const JwtKey *key,
	const char *message, const size_t message_length,
	const unsigned char *signature, const size_t signature_length
) {

	if (!key || !message || !signature) return EINVAL;

	const JwtKeyAlgorithm *algorithm = jwt_key_algorithm_get (key->alg);

	int retval = EINVAL;
	switch (algorithm->family) {
		case JWT_KEY_FAMILY_HMAC:
			retval = jwt_key_verify_hmac (key, algorithm, message, message_length, signature, signature_length);
			break;

		case JWT_KEY_FAMILY_RSA:
		case JWT_KEY_FAMILY_EC:
			retval = jwt_key_verify_pem (key, algorithm, message, message_length, signature, signature_length);
			break;

		default: break;
	}

	return retval;

}
