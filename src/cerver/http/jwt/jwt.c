/* Copyright (C) 2015-2018 Ben Collins <ben@cyphre.com>
   This file is part of the JWT C Library

   This Source Code Form is subject to the terms of the Mozilla Public
   License, v. 2.0. If a copy of the MPL was not distributed with this
   file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <errno.h>

#include <openssl/crypto.h>

#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"

#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/config.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"

#include "cerver/utils/base64.h"

// the name of the algorithm without a signature in a token (RFC 7518 §3.6)
#define JWT_ALG_NONE_HEADER_NAME		"none"

// the indentation of the pretty dumps
#define JWT_DUMP_INDENT					4

#define JWT_PART_SEPARATOR				'.'

// the header, the payload and the signature
typedef struct JwtTokenParts {

	const char *header;
	size_t header_length;

	const char *payload;
	size_t payload_length;

	const char *signature;
	size_t signature_length;

	// the header, the separator and the payload, what is signed
	size_t signed_length;

} JwtTokenParts;

static jwt_malloc_t pfn_malloc = NULL;
static jwt_realloc_t pfn_realloc = NULL;
static jwt_free_t pfn_free = NULL;

const char *jwt_error_str (const jwt_error_t error) {

	switch (error) {
		#define XX(num, name, string) case JWT_VALIDATION_##name: return #string;
		JWT_VALIDATION_MAP(XX)
		#undef XX
	}

	return NULL;

}

static void *jwt_malloc (size_t size) {

	if (pfn_malloc)
		return pfn_malloc (size);

	return malloc (size);

}

static void *jwt_realloc (void *ptr, size_t size) {

	if (pfn_realloc)
		return pfn_realloc (ptr, size);

	return realloc (ptr, size);

}

static void jwt_freemem (void *ptr) {

	if (pfn_free)
		pfn_free (ptr);

	else free (ptr);

}

static void *jwt_calloc (size_t nmemb, size_t size) {

	size_t total_size = nmemb * size;
	if (!total_size)
		return NULL;

	void *ptr = jwt_malloc (total_size);
	if (ptr)
		(void) memset (ptr, 0, total_size);

	return ptr;

}

static void jwt_scrub_key (jwt_t *jwt) {

	if (jwt->key) {
		// a memset () before a free () can be removed by the compiler
		OPENSSL_cleanse (jwt->key, jwt->key_len);

		jwt_freemem (jwt->key);
		jwt->key = NULL;
	}

	jwt->key_len = 0;
	jwt->alg = JWT_ALG_NONE;

}

int jwt_set_alg (
	jwt_t *jwt, jwt_alg_t alg, const unsigned char *key, size_t len
) {
	/* No matter what happens here, we do this. */
	jwt_scrub_key (jwt);

	if (alg < JWT_ALG_NONE || alg >= JWT_ALG_INVAL)
		return EINVAL;

	switch (alg) {
	case JWT_ALG_NONE:
		if (key || len)
			return EINVAL;
		break;

	default:
		if (!key || !len)
			return EINVAL;

		jwt->key = (unsigned char *) jwt_malloc (len);
		if (!jwt->key)
			return ENOMEM;

		memcpy (jwt->key, key, len);
	}

	jwt->alg = alg;
	jwt->key_len = len;

	return 0;

}

jwt_alg_t jwt_get_alg (jwt_t *jwt) {

	return jwt->alg;

}

int jwt_new (jwt_t **jwt) {

	if (!jwt)
		return EINVAL;

	*jwt = (jwt_t *) jwt_malloc (sizeof (jwt_t));
	if (!*jwt)
		return ENOMEM;

	(void) memset (*jwt, 0, sizeof (jwt_t));

	(*jwt)->grants = json_object ();
	if (!(*jwt)->grants) {
		jwt_freemem (*jwt);
		*jwt = NULL;
		return ENOMEM;
	}

	(*jwt)->headers = json_object ();
	if (!(*jwt)->headers) {
		json_decref ((*jwt)->grants);
		jwt_freemem (*jwt);
		*jwt = NULL;
		return ENOMEM;
	}

	return 0;

}

void jwt_free (jwt_t *jwt) {

	if (!jwt)
		return;

	jwt_scrub_key (jwt);

	json_decref (jwt->grants);
	json_decref (jwt->headers);

	jwt_freemem (jwt);

}

jwt_t *jwt_dup (jwt_t *jwt) {

	jwt_t *new_thing = NULL;

	if (!jwt) {
		errno = EINVAL;
		goto dup_fail;
	}

	errno = 0;

	new_thing = (jwt_t *) jwt_malloc (sizeof (jwt_t));
	if (!new_thing) {
		errno = ENOMEM;
		return NULL;
	}

	(void) memset (new_thing, 0, sizeof (jwt_t));

	if (jwt->key_len) {
		new_thing->alg = jwt->alg;
		new_thing->key = (unsigned char *) jwt_malloc (jwt->key_len);
		if (!new_thing->key) {
			errno = ENOMEM;
			goto dup_fail;
		}

		(void) memcpy (new_thing->key, jwt->key, jwt->key_len);
		new_thing->key_len = jwt->key_len;
	}

	new_thing->grants = json_deep_copy (jwt->grants);
	if (!new_thing->grants)
		errno = ENOMEM;

	new_thing->headers = json_deep_copy (jwt->headers);
	if (!new_thing->headers)
		errno = ENOMEM;

dup_fail:
	if (errno) {
		jwt_free (new_thing);
		new_thing = NULL;
	}

	return new_thing;

}

static const char *get_js_string (
	json_t *js, const char *key
) {

	const char *val = NULL;
	json_t *js_val;

	js_val = json_object_get (js, key);
	if (js_val) {
		val = json_string_value (js_val);
	} else {
		errno = ENOENT;
	}

	return val;

}

static long get_js_int (json_t *js, const char *key) {

	long val = -1;
	json_t *js_val;

	js_val = json_object_get (js, key);
	if (js_val) {
		val = (long)json_integer_value (js_val);
	} else {
		errno = ENOENT;
	}

	return val;

}

static int get_js_bool (json_t *js, const char *key) {

	int val = -1;
	json_t *js_val;

	js_val = json_object_get (js, key);
	if (js_val) {
		val = (int)json_is_true (js_val);
	} else {
		errno = ENOENT;
	}
	return val;

}

#pragma region decode

// a token is three parts separated by dots: header.payload.signature
// returns 0 on success, EINVAL if it has another form
static int jwt_token_split (
	const char *token, const size_t token_length, JwtTokenParts *parts
) {

	const char *end = token + token_length;

	const char *first = (const char *) memchr (token, JWT_PART_SEPARATOR, token_length);
	if (!first) return EINVAL;

	const char *second = (const char *) memchr (first + 1, JWT_PART_SEPARATOR, (size_t) (end - (first + 1)));
	if (!second) return EINVAL;

	// a third separator is not part of the signature
	if (memchr (second + 1, JWT_PART_SEPARATOR, (size_t) (end - (second + 1)))) return EINVAL;

	parts->header = token;
	parts->header_length = (size_t) (first - token);

	parts->payload = first + 1;
	parts->payload_length = (size_t) (second - parts->payload);

	parts->signature = second + 1;
	parts->signature_length = (size_t) (end - parts->signature);

	parts->signed_length = (size_t) (second - token);

	return 0;

}

// decodes a canonical base64url part, the buffer is allocated with malloc ()
// returns 0 on success, EINVAL or ENOMEM on error
static int jwt_part_decode (
	const char *part, const size_t part_length,
	unsigned char **decoded, size_t *decoded_length
) {

	int retval = ENOMEM;

	// one more byte, the result is never empty for malloc ()
	*decoded = (unsigned char *) jwt_malloc (base64url_decode_length (part_length) + 1);
	if (*decoded) {
		retval = 0;

		if (base64url_decode (*decoded, decoded_length, part, part_length)) {
			jwt_freemem (*decoded);
			*decoded = NULL;
			retval = EINVAL;
		}
	}

	return retval;

}

// the header and the payload are json objects without repeated keys
static json_t *jwt_part_decode_object (
	const char *part, const size_t part_length
) {

	json_t *object = NULL;

	unsigned char *decoded = NULL;
	size_t decoded_length = 0;
	if (!jwt_part_decode (part, part_length, &decoded, &decoded_length)) {
		object = json_loadb ((const char *) decoded, decoded_length, JSON_REJECT_DUPLICATES, NULL);
		if (object && !json_is_object (object)) {
			json_decref (object);
			object = NULL;
		}

		jwt_freemem (decoded);
	}

	return object;

}

static int jwt_parse_headers (jwt_t *jwt, const JwtTokenParts *parts) {

	json_decref (jwt->headers);

	jwt->headers = jwt_part_decode_object (parts->header, parts->header_length);

	return jwt->headers ? 0 : EINVAL;

}

static int jwt_parse_grants (jwt_t *jwt, const JwtTokenParts *parts) {

	json_decref (jwt->grants);

	jwt->grants = jwt_part_decode_object (parts->payload, parts->payload_length);

	return jwt->grants ? 0 : EINVAL;

}

// the algorithm that the header names, with the exact names of RFC 7518
static jwt_alg_t jwt_headers_alg (const jwt_t *jwt) {

	return jwt_str_alg (get_js_string (jwt->headers, "alg"));

}

// checks the signature of the parts with the key
static int jwt_verify_signature (
	const JwtKey *key, const JwtTokenParts *parts
) {

	unsigned char *signature = NULL;
	size_t signature_length = 0;

	int retval = parts->signature_length ?
		jwt_part_decode (parts->signature, parts->signature_length, &signature, &signature_length) :
		EINVAL;

	if (!retval) {
		retval = jwt_key_verify (
			key,
			parts->header, parts->signed_length,
			signature, signature_length
		);

		jwt_freemem (signature);
	}

	return retval;

}

// an unsigned token can not have a key nor a signature
static int jwt_decode_unsigned (
	const JwtTokenParts *parts, const size_t key_len
) {

	return (key_len || parts->signature_length) ? EINVAL : 0;

}

// verifies a token with the raw key of the first API,
// a key is created for the algorithm that the header names
static int jwt_decode_raw_key (
	const jwt_alg_t alg, const JwtTokenParts *parts,
	const unsigned char *key, const size_t key_len
) {

	int retval = EINVAL;

	// a token that says HS256 with a public PEM as the key would be verified
	// with the PEM as its secret, and anybody can sign with a public key
	JwtKey *verification = jwt_key_create (alg, JWT_KEY_TYPE_PUBLIC, key, key_len, NULL);
	if (verification && (jwt_key_check_strength (verification) != JWT_KEY_ERROR_SECRET)) {
		retval = jwt_verify_signature (verification, parts);
	}

	jwt_key_delete (verification);

	return retval;

}

static int jwt_decode_parts (
	jwt_t *decoded, const JwtTokenParts *parts,
	const unsigned char *key, const size_t key_len
) {

	int retval = jwt_parse_headers (decoded, parts);
	if (retval) return retval;

	jwt_alg_t alg = jwt_headers_alg (decoded);
	if (alg == JWT_ALG_INVAL) {
		retval = EINVAL;
	}

	else if (alg == JWT_ALG_NONE) {
		retval = jwt_decode_unsigned (parts, key_len);
	}

	// without a key, the token is decoded but not verified, as an unsigned one
	else if (!key_len) {
		alg = JWT_ALG_NONE;
	}

	else {
		retval = jwt_decode_raw_key (alg, parts, key, key_len);
	}

	if (!retval) retval = jwt_parse_grants (decoded, parts);

	decoded->alg = alg;

	return retval;

}

int jwt_decode (
	jwt_t **jwt, const char *token,
	const unsigned char *key, size_t key_len
) {

	if (!jwt) return EINVAL;

	*jwt = NULL;

	if (!token || (key_len && !key)) return EINVAL;

	JwtTokenParts parts = { NULL, 0, NULL, 0, NULL, 0, 0 };
	int retval = jwt_token_split (token, strlen (token), &parts);
	if (retval) return retval;

	jwt_t *decoded = NULL;
	retval = jwt_new (&decoded);
	if (!retval) {
		retval = jwt_decode_parts (decoded, &parts, key, key_len);

		if (retval) jwt_free (decoded);
		else *jwt = decoded;
	}

	return retval;

}

// the header must name the algorithm of the key,
// so a token can never choose how it is verified
static int jwt_decode_with_key_parts (
	jwt_t *decoded, const JwtTokenParts *parts, const JwtKey *key
) {

	int retval = jwt_parse_headers (decoded, parts);
	if (retval) return retval;

	if (jwt_headers_alg (decoded) != key->alg) return EINVAL;

	retval = jwt_verify_signature (key, parts);
	if (retval) return retval;

	retval = jwt_parse_grants (decoded, parts);

	decoded->alg = key->alg;

	return retval;

}

int jwt_decode_with_key (
	jwt_t **jwt, const char *token, const size_t token_length,
	const JwtKey *key
) {

	if (!jwt) return EINVAL;

	*jwt = NULL;

	if (!token || !key || (key->alg == JWT_ALG_NONE)) return EINVAL;

	JwtTokenParts parts = { NULL, 0, NULL, 0, NULL, 0, 0 };
	int retval = jwt_token_split (token, token_length, &parts);
	if (retval) return retval;

	jwt_t *decoded = NULL;
	retval = jwt_new (&decoded);
	if (!retval) {
		retval = jwt_decode_with_key_parts (decoded, &parts, key);

		if (retval) jwt_free (decoded);
		else *jwt = decoded;
	}

	return retval;

}

#pragma endregion

const char *jwt_get_grant (jwt_t *jwt, const char *grant) {

	if (!jwt || !grant || !strlen (grant)) {
		errno = EINVAL;
		return NULL;
	}

	errno = 0;

	return get_js_string (jwt->grants, grant);

}

long jwt_get_grant_int (jwt_t *jwt, const char *grant) {

	if (!jwt || !grant || !strlen (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return get_js_int (jwt->grants, grant);

}

int jwt_get_grant_bool (jwt_t *jwt, const char *grant) {

	if (!jwt || !grant || !strlen (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return get_js_bool (jwt->grants, grant);

}

char *jwt_get_grants_json (jwt_t *jwt, const char *grant) {

	json_t *js_val = NULL;

	errno = EINVAL;

	if (!jwt)
		return NULL;

	if (grant && strlen (grant))
		js_val = json_object_get (jwt->grants, grant);
	else
		js_val = jwt->grants;

	if (js_val == NULL)
		return NULL;

	errno = 0;

	return json_dumps (
		js_val, JSON_SORT_KEYS | JSON_COMPACT | JSON_ENCODE_ANY
	);

}

int jwt_add_grant (
	jwt_t *jwt, const char *grant, const char *val
) {

	if (!jwt || !grant || !strlen (grant) || !val)
		return EINVAL;

	// a grant of any type is never replaced
	if (json_object_get (jwt->grants, grant))
		return EEXIST;

	if (json_object_set_new (jwt->grants, grant, json_string (val)))
		return EINVAL;

	return 0;

}

int jwt_add_grant_int (
	jwt_t *jwt, const char *grant, long val
) {

	if (!jwt || !grant || !strlen (grant))
		return EINVAL;

	if (json_object_get (jwt->grants, grant))
		return EEXIST;

	if (json_object_set_new (
		jwt->grants, grant, json_integer ((json_int_t)val)
	))
		return EINVAL;

	return 0;

}

int jwt_add_grant_bool (
	jwt_t *jwt, const char *grant, int val
) {

	if (!jwt || !grant || !strlen (grant))
		return EINVAL;

	if (json_object_get (jwt->grants, grant))
		return EEXIST;

	if (json_object_set_new (jwt->grants, grant, json_boolean (val)))
		return EINVAL;

	return 0;

}

int jwt_add_grants_json (jwt_t *jwt, const char *json) {

	json_t *js_val;
	int ret = -1;

	if (!jwt)
		return EINVAL;

	js_val = json_loads (json, JSON_REJECT_DUPLICATES, NULL);

	if (json_is_object (js_val))
		ret = json_object_update (jwt->grants, js_val);

	json_decref (js_val);

	return ret ? EINVAL : 0;

}

int jwt_del_grants (jwt_t *jwt, const char *grant) {

	if (!jwt)
		return EINVAL;

	if (grant == NULL || !strlen (grant))
		json_object_clear (jwt->grants);
	else
		json_object_del (jwt->grants, grant);

	return 0;

}

const char *jwt_get_header (
	jwt_t *jwt, const char *header
) {

	if (!jwt || !header || !strlen (header)) {
		errno = EINVAL;
		return NULL;
	}

	errno = 0;

	return get_js_string (jwt->headers, header);

}

long jwt_get_header_int (
	jwt_t *jwt, const char *header
) {

	if (!jwt || !header || !strlen (header)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return get_js_int (jwt->headers, header);

}

int jwt_get_header_bool (
	jwt_t *jwt, const char *header
) {

	if (!jwt || !header || !strlen (header)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return get_js_bool (jwt->headers, header);

}

char *jwt_get_headers_json (
	jwt_t *jwt, const char *header
) {

	json_t *js_val = NULL;

	errno = EINVAL;

	if (!jwt)
		return NULL;

	if (header && strlen (header))
		js_val = json_object_get (jwt->headers, header);
	else
		js_val = jwt->headers;

	if (js_val == NULL)
		return NULL;

	errno = 0;

	return json_dumps (
		js_val, JSON_SORT_KEYS | JSON_COMPACT | JSON_ENCODE_ANY
	);

}

int jwt_add_header (
	jwt_t *jwt, const char *header, const char *val
) {

	if (!jwt || !header || !strlen (header) || !val)
		return EINVAL;

	// a header of any type is never replaced
	if (json_object_get (jwt->headers, header))
		return EEXIST;

	if (json_object_set_new (jwt->headers, header, json_string (val)))
		return EINVAL;

	return 0;

}

int jwt_add_header_int (
	jwt_t *jwt, const char *header, long val
) {

	if (!jwt || !header || !strlen (header))
		return EINVAL;

	if (json_object_get (jwt->headers, header))
		return EEXIST;

	if (json_object_set_new (
		jwt->headers, header, json_integer ((json_int_t) val)
	))
		return EINVAL;

	return 0;

}

int jwt_add_header_bool (
	jwt_t *jwt, const char *header, int val
) {

	if (!jwt || !header || !strlen (header))
		return EINVAL;

	if (json_object_get (jwt->headers, header))
		return EEXIST;

	if (json_object_set_new (jwt->headers, header, json_boolean (val)))
		return EINVAL;

	return 0;

}

int jwt_add_headers_json (jwt_t *jwt, const char *json) {

	json_t *js_val;
	int ret = -1;

	if (!jwt)
		return EINVAL;

	js_val = json_loads (json, JSON_REJECT_DUPLICATES, NULL);

	if (json_is_object (js_val))
		ret = json_object_update (jwt->headers, js_val);

	json_decref (js_val);

	return ret ? EINVAL : 0;

}

int jwt_del_headers (jwt_t *jwt, const char *header) {

	if (!jwt)
		return EINVAL;

	if (header == NULL || !strlen (header))
		json_object_clear (jwt->headers);
	else
		json_object_del (jwt->headers, header);

	return 0;

}

static int __append_str (char **buf, const char *str) {

	char *new_thing = NULL;

	if (*buf == NULL) {
		new_thing = (char *) jwt_calloc (1, strlen (str) + 1);
	}
	
	else {
		new_thing = (char *) jwt_realloc (*buf, strlen (*buf) + strlen (str) + 1);
	}

	if (new_thing == NULL)
		return ENOMEM;

	strcat (new_thing, str);

	*buf = new_thing;

	return 0;

}

static int write_js (
	const json_t *js, char **buf, int pretty
) {

	/* Sort keys for repeatability */
	const size_t flags = JSON_SORT_KEYS | (pretty ? JSON_INDENT (JWT_DUMP_INDENT) : JSON_COMPACT);

	char *serial = json_dumps (js, flags);
	if (!serial)
		return ENOMEM;

	int ret = pretty ? __append_str (buf, "\n") : 0;

	if (!ret)
		ret = __append_str (buf, serial);

	if (!ret && pretty)
		ret = __append_str (buf, "\n");

	jwt_freemem (serial);

	return ret;

}

// the name of the algorithm in the header of a token
static const char *jwt_alg_header_name (const jwt_alg_t alg) {

	return (alg == JWT_ALG_NONE) ? JWT_ALG_NONE_HEADER_NAME : jwt_alg_str (alg);

}

static int jwt_write_head (
	jwt_t *jwt, char **buf, int pretty
) {

	int ret = 0;

	if (jwt->alg != JWT_ALG_NONE) {

		/* Only add default 'typ' header if it has not been defined,
		 * allowing for any value of it. This allows for signaling
		 * of application specific extentions to JWT, such as PASSporT,
		 * RFC 8225. */
		if ((ret = jwt_add_header (jwt, "typ", "JWT"))) {
			if (ret != EEXIST) {
				return ret;
			}
		}
	}

	if ((ret = jwt_del_headers (jwt, "alg")))
		return ret;

	if ((ret = jwt_add_header (jwt, "alg", jwt_alg_header_name (jwt->alg))))
		return ret;

	return write_js (jwt->headers, buf, pretty);

}

static int jwt_write_body (jwt_t *jwt, char **buf, int pretty) {

	return write_js (jwt->grants, buf, pretty);

}

static int jwt_dump (jwt_t *jwt, char **buf, int pretty) {

	int ret;

	ret = jwt_write_head (jwt, buf, pretty);

	if (ret == 0)
		ret = __append_str (buf, ".");

	if (ret == 0)
		ret = jwt_write_body (jwt, buf, pretty);

	return ret;

}

int jwt_dump_fp (jwt_t *jwt, FILE *fp, int pretty) {

	char *out = NULL;
	int ret = 0;

	ret = jwt_dump (jwt, &out, pretty);

	if (ret == 0)
		(void) fputs (out, fp);

	if (out)
		jwt_freemem (out);

	return ret;

}

char *jwt_dump_str (jwt_t *jwt, int pretty) {

	char *out = NULL;
	int err;

	err = jwt_dump (jwt, &out, pretty);

	if (err) {
		errno = err;
		if (out)
			jwt_freemem (out);
		out = NULL;
	} else {
		errno = 0;
	}

	return out;

}

#pragma region encode

// adds the separator and the signature in base64url to the signed parts
static int jwt_encode_signature (
	char **token, size_t signed_length,
	const unsigned char *signature, const size_t signature_length
) {

	int ret = ENOMEM;

	char *signed_token = (char *) jwt_realloc (
		*token, signed_length + 1 + base64url_encode_length (signature_length) + 1
	);

	if (signed_token) {
		signed_token[signed_length] = JWT_PART_SEPARATOR;

		(void) base64url_encode (
			signed_token + signed_length + 1, signature, signature_length
		);

		*token = signed_token;
		ret = 0;
	}

	return ret;

}

// the header and the payload in base64url, signed with the key,
// or with an empty signature if there is no key
static int jwt_encode_serialized (
	const JwtKey *key, const char *header, const char *payload, char **out
) {

	const size_t header_length = strlen (header);
	const size_t payload_length = strlen (payload);

	const size_t signed_length = base64url_encode_length (header_length)
		+ 1 + base64url_encode_length (payload_length);

	// the separator of the signature and the null byte
	char *token = (char *) jwt_malloc (signed_length + 2);
	if (!token)
		return ENOMEM;

	size_t position = base64url_encode (token, (const unsigned char *) header, header_length);
	token[position++] = JWT_PART_SEPARATOR;
	(void) base64url_encode (token + position, (const unsigned char *) payload, payload_length);

	int ret = 0;
	if (key) {
		unsigned char *signature = NULL;
		size_t signature_length = 0;

		ret = jwt_key_sign (key, token, signed_length, &signature, &signature_length);
		if (!ret)
			ret = jwt_encode_signature (&token, signed_length, signature, signature_length);

		jwt_freemem (signature);
	}

	else {
		token[signed_length] = JWT_PART_SEPARATOR;
		token[signed_length + 1] = '\0';
	}

	if (ret) jwt_freemem (token);
	else *out = token;

	return ret;

}

static int jwt_encode_parts (
	jwt_t *jwt, const JwtKey *key, char **out
) {

	char *header = NULL;
	char *payload = NULL;

	int ret = jwt_write_head (jwt, &header, 0);

	if (!ret)
		ret = jwt_write_body (jwt, &payload, 0);

	if (!ret)
		ret = jwt_encode_serialized (key, header, payload, out);

	jwt_freemem (header);
	jwt_freemem (payload);

	return ret;

}

// the token of the first API: the key is created from the raw key of the jwt
static int jwt_encode (jwt_t *jwt, char **out) {

	if (jwt->alg == JWT_ALG_NONE)
		return jwt_encode_parts (jwt, NULL, out);

	JwtKey *signing = jwt_key_create (
		jwt->alg, JWT_KEY_TYPE_PRIVATE, jwt->key, jwt->key_len, NULL
	);

	if (!signing)
		return EINVAL;

	int ret = jwt_encode_parts (jwt, signing, out);

	jwt_key_delete (signing);

	return ret;

}

char *jwt_encode_with_key (jwt_t *jwt, const JwtKey *key) {

	char *token = NULL;

	if (!jwt || !key || (key->alg == JWT_ALG_NONE)) {
		errno = EINVAL;
		return token;
	}

	// the raw key of the first API is not used
	jwt_scrub_key (jwt);
	jwt->alg = key->alg;

	errno = jwt_encode_parts (jwt, key, &token);
	if (errno) {
		jwt_freemem (token);
		token = NULL;
	}

	return token;

}

#pragma endregion

int jwt_encode_fp (jwt_t *jwt, FILE *fp) {

	char *str = NULL;
	int ret;

	ret = jwt_encode (jwt, &str);
	if (ret) {
		if (str)
			jwt_freemem (str);
		return ret;
	}

	(void) fputs (str, fp);
	jwt_freemem (str);

	return 0;

}

char *jwt_encode_str (jwt_t *jwt) {

	char *str = NULL;

	errno = jwt_encode (jwt, &str);
	if (errno) {
		if (str)
			jwt_freemem (str);
		str = NULL;
	}

	return str;

}

void jwt_free_str (char *str) {

	if (str) jwt_freemem (str);

}

int jwt_valid_new (
	jwt_valid_t **jwt_valid, jwt_alg_t alg
) {

	if (!jwt_valid)
		return EINVAL;

	*jwt_valid = (jwt_valid_t *) jwt_malloc (sizeof (jwt_valid_t));
	if (!*jwt_valid)
		return ENOMEM;

	(void) memset (*jwt_valid, 0, sizeof (jwt_valid_t));
	(*jwt_valid)->alg = alg;

	(*jwt_valid)->status = JWT_VALIDATION_ERROR;

	(*jwt_valid)->req_grants = json_object ();
	if (!(*jwt_valid)->req_grants) {
		jwt_freemem (*jwt_valid);
		*jwt_valid = NULL;
		return ENOMEM;
	}

	return 0;

}

void jwt_valid_free (jwt_valid_t *jwt_valid) {

	if (!jwt_valid)
		return;

	json_decref (jwt_valid->req_grants);

	jwt_freemem (jwt_valid);

}

unsigned int jwt_valid_get_status (jwt_valid_t *jwt_valid) {

	if (!jwt_valid)
		return JWT_VALIDATION_ERROR;

	return jwt_valid->status;

}

int jwt_valid_add_grant (
	jwt_valid_t *jwt_valid,
	const char *grant, const char *val
) {

	if (!jwt_valid || !grant || !strlen (grant) || !val)
		return EINVAL;

	if (get_js_string (jwt_valid->req_grants, grant) != NULL)
		return EEXIST;

	if (json_object_set_new (jwt_valid->req_grants, grant, json_string (val)))
		return EINVAL;

	return 0;

}

int jwt_valid_add_grant_int (
	jwt_valid_t *jwt_valid,
	const char *grant, long val
) {

	if (!jwt_valid || !grant || !strlen (grant))
		return EINVAL;

	if (get_js_int (jwt_valid->req_grants, grant) != -1)
		return EEXIST;

	if (json_object_set_new (
		jwt_valid->req_grants, grant, json_integer ((json_int_t) val))
	)
		return EINVAL;

	return 0;

}

int jwt_valid_add_grant_bool (
	jwt_valid_t *jwt_valid,
	const char *grant, int val
) {

	if (!jwt_valid || !grant || !strlen (grant))
		return EINVAL;

	if (get_js_int (jwt_valid->req_grants, grant) != -1)
		return EEXIST;

	if (json_object_set_new (
		jwt_valid->req_grants, grant, json_boolean (val))
	)
		return EINVAL;

	return 0;

}

int jwt_valid_add_grants_json (
	jwt_valid_t *jwt_valid, const char *json
) {

	json_t *js_val;
	int ret = -1;

	if (!jwt_valid)
		return EINVAL;

	js_val = json_loads (json, JSON_REJECT_DUPLICATES, NULL);

	if (json_is_object (js_val))
		ret = json_object_update (jwt_valid->req_grants, js_val);

	json_decref (js_val);

	return ret ? EINVAL : 0;

}

char *jwt_valid_get_grants_json (
	jwt_valid_t *jwt_valid, const char *grant
) {

	json_t *js_val = NULL;

	errno = EINVAL;

	if (!jwt_valid)
		return NULL;

	if (grant && strlen (grant))
		js_val = json_object_get (jwt_valid->req_grants, grant);
	else
		js_val = jwt_valid->req_grants;

	if (js_val == NULL)
		return NULL;

	errno = 0;

	return json_dumps (
		js_val, JSON_SORT_KEYS | JSON_COMPACT | JSON_ENCODE_ANY
	);

}

const char *jwt_valid_get_grant (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid || !grant || !strlen (grant)) {
		errno = EINVAL;
		return NULL;
	}

	errno = 0;

	return get_js_string (jwt_valid->req_grants, grant);

}

long jwt_valid_get_grant_int (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid || !grant || !strlen (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return get_js_int (jwt_valid->req_grants, grant);

}

int jwt_valid_get_grant_bool (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid || !grant || !strlen (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return get_js_bool (jwt_valid->req_grants, grant);

}

int jwt_valid_set_now (
	jwt_valid_t *jwt_valid, const time_t now
) {

	if (!jwt_valid)
		return EINVAL;

	jwt_valid->now = now;

	return 0;

}

int jwt_valid_set_headers (
	jwt_valid_t *jwt_valid, int hdr
) {

	if (!jwt_valid)
		return EINVAL;

	jwt_valid->hdr = hdr;

	return 0;

}

int jwt_valid_del_grants (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid)
		return EINVAL;

	if (grant == NULL || !strlen (grant))
		json_object_clear (jwt_valid->req_grants);
	else
		json_object_del (jwt_valid->req_grants, grant);

	return 0;

}

unsigned int jwt_validate (
	jwt_t *jwt, jwt_valid_t *jwt_valid
) {

	const char *jwt_hdr_str, *jwt_body_str, *req_grant;
	json_t *js_val_1, *js_val_2;
	time_t t;

	if (!jwt_valid)
		return JWT_VALIDATION_ERROR;

	if (!jwt) {
		jwt_valid->status = JWT_VALIDATION_ERROR;
		return jwt_valid->status;
	}

	jwt_valid->status = JWT_VALIDATION_SUCCESS;

	/* Validate algorithm */
	if (jwt_valid->alg != jwt_get_alg (jwt))
		jwt_valid->status |= JWT_VALIDATION_ALG_MISMATCH;

	/* Validate expires */
	t = get_js_int (jwt->grants, "exp");
	if (jwt_valid->now && t != -1 && jwt_valid->now >= t)
		jwt_valid->status |= JWT_VALIDATION_EXPIRED;

	/* Validate not-before */
	t = get_js_int (jwt->grants, "nbf");
	if (jwt_valid->now && t != -1 && jwt_valid->now < t)
		jwt_valid->status |= JWT_VALIDATION_TOO_NEW;

	/* Validate replicated issuer */
	jwt_hdr_str = get_js_string (jwt->headers, "iss");
	jwt_body_str = get_js_string (jwt->grants, "iss");
	if (jwt_hdr_str && jwt_body_str && strcmp (jwt_hdr_str, jwt_body_str))
		jwt_valid->status |= JWT_VALIDATION_ISS_MISMATCH;

	/* Validate replicated subject */
	jwt_hdr_str = get_js_string (jwt->headers, "sub");
	jwt_body_str = get_js_string (jwt->grants, "sub");
	if (jwt_hdr_str && jwt_body_str && strcmp (jwt_hdr_str, jwt_body_str))
		jwt_valid->status |= JWT_VALIDATION_SUB_MISMATCH;

	/* Validate replicated audience (might be array or string) */
	js_val_1 = json_object_get (jwt->headers, "aud");
	js_val_2 = json_object_get (jwt->grants, "aud");
	if (js_val_1 && js_val_2 && !json_equal (js_val_1, js_val_2))
		jwt_valid->status |= JWT_VALIDATION_AUD_MISMATCH;

	/* Validate required grants */
	json_object_foreach (jwt_valid->req_grants, req_grant, js_val_1) {
		json_t *act_js_val = json_object_get (jwt->grants, req_grant);

		if (act_js_val && json_equal (js_val_1, act_js_val))
			continue;

		if (act_js_val)
			jwt_valid->status |= JWT_VALIDATION_GRANT_MISMATCH;
		else
			jwt_valid->status |= JWT_VALIDATION_GRANT_MISSING;
	}

	return jwt_valid->status;

}
