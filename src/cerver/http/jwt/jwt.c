/* Copyright (C) 2015-2018 Ben Collins <ben@cyphre.com>
   This file is part of the JWT C Library

   This Source Code Form is subject to the terms of the Mozilla Public
   License, v. 2.0. If a copy of the MPL was not distributed with this
   file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/crypto.h>

#include "cerver/http/json/json.h"
#include "cerver/http/json/value.h"
#include "cerver/http/jwt/alg.h"
#include "cerver/http/jwt/jwt.h"
#include "cerver/http/jwt/key.h"

#include "cerver/utils/base64.h"

// the name of the algorithm without a signature in a token (RFC 7518 §3.6)
#define JWT_ALG_NONE_HEADER_NAME		"none"

// the indentation of the pretty dumps
#define JWT_DUMP_INDENT					4

#define JWT_PART_SEPARATOR				'.'
#define JWT_PART_SEPARATOR_STRING		"."

#define JWT_DUMP_NEW_LINE				"\n"

// the value of a missing integer or boolean grant
#define JWT_GRANT_NOT_FOUND				-1

// the default type of a signed token (RFC 7519 §5.1)
#define JWT_TYPE_HEADER					"typ"
#define JWT_TYPE_HEADER_VALUE			"JWT"

#define JWT_ALG_HEADER					"alg"

#define JWT_CLAIM_EXPIRATION			"exp"
#define JWT_CLAIM_NOT_BEFORE			"nbf"
#define JWT_CLAIM_ISSUER				"iss"
#define JWT_CLAIM_SUBJECT				"sub"
#define JWT_CLAIM_AUDIENCE				"aud"

// the dumps of the grants and the headers, sorted for repeatability
#define JWT_JSON_DUMP_FLAGS				(JSON_SORT_KEYS | JSON_COMPACT | JSON_ENCODE_ANY)

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

const char *jwt_error_str (const jwt_error_t error) {

	switch (error) {
		#define XX(num, name, string) case JWT_VALIDATION_##name: return #string;
		JWT_VALIDATION_MAP(XX)
		#undef XX
	}

	return NULL;

}

#pragma region key

static void jwt_scrub_key (jwt_t *jwt) {

	if (jwt->key) {
		// a memset () before a free () can be removed by the compiler
		OPENSSL_cleanse (jwt->key, jwt->key_length);

		free (jwt->key);
		jwt->key = NULL;
	}

	jwt->key_length = 0;
	jwt->alg = JWT_ALG_NONE;

}

// an unsigned token has no key, a signed one always has one
static bool jwt_key_matches_alg (
	const jwt_alg_t alg, const unsigned char *key, const size_t length
) {

	return (alg == JWT_ALG_NONE) ? (!key && !length) : (key && length);

}

int jwt_set_alg (
	jwt_t *jwt, jwt_alg_t alg, const unsigned char *key, size_t length
) {

	// whatever happens, the previous key is cleared
	jwt_scrub_key (jwt);

	if ((alg < JWT_ALG_NONE) || (alg >= JWT_ALG_INVAL)) return EINVAL;

	if (!jwt_key_matches_alg (alg, key, length)) return EINVAL;

	if (alg != JWT_ALG_NONE) {
		jwt->key = (unsigned char *) malloc (length);
		if (!jwt->key) return ENOMEM;

		(void) memcpy (jwt->key, key, length);
	}

	jwt->alg = alg;
	jwt->key_length = length;

	return 0;

}

jwt_alg_t jwt_get_alg (jwt_t *jwt) {

	return jwt->alg;

}

#pragma endregion

#pragma region main

int jwt_new (jwt_t **jwt) {

	if (!jwt) return EINVAL;

	*jwt = (jwt_t *) calloc (1, sizeof (jwt_t));
	if (!*jwt) return ENOMEM;

	(*jwt)->grants = json_object ();
	(*jwt)->headers = json_object ();

	if (!(*jwt)->grants || !(*jwt)->headers) {
		json_decref ((*jwt)->grants);
		json_decref ((*jwt)->headers);

		free (*jwt);
		*jwt = NULL;

		return ENOMEM;
	}

	return 0;

}

void jwt_free (jwt_t *jwt) {

	if (jwt) {
		jwt_scrub_key (jwt);

		json_decref (jwt->grants);
		json_decref (jwt->headers);

		free (jwt);
	}

}

// copies the key, the grants and the headers of the jwt
// returns 0 on success, ENOMEM on error
static int jwt_dup_values (jwt_t *copy, const jwt_t *jwt) {

	if (jwt->key_length) {
		copy->alg = jwt->alg;

		copy->key = (unsigned char *) malloc (jwt->key_length);
		if (!copy->key) return ENOMEM;

		(void) memcpy (copy->key, jwt->key, jwt->key_length);
		copy->key_length = jwt->key_length;
	}

	copy->grants = json_deep_copy (jwt->grants);
	copy->headers = json_deep_copy (jwt->headers);

	return (copy->grants && copy->headers) ? 0 : ENOMEM;

}

jwt_t *jwt_dup (jwt_t *jwt) {

	if (!jwt) {
		errno = EINVAL;
		return NULL;
	}

	jwt_t *copy = (jwt_t *) calloc (1, sizeof (jwt_t));
	if (!copy) {
		errno = ENOMEM;
		return NULL;
	}

	errno = jwt_dup_values (copy, jwt);
	if (errno) {
		jwt_free (copy);
		copy = NULL;
	}

	return copy;

}

#pragma endregion

#pragma region values

static const char *jwt_json_get_string (
	json_t *json, const char *key
) {

	const char *value = NULL;

	json_t *json_value = json_object_get (json, key);
	if (json_value) {
		value = json_string_value (json_value);
	}

	else {
		errno = ENOENT;
	}

	return value;

}

static long jwt_json_get_integer (json_t *json, const char *key) {

	long value = JWT_GRANT_NOT_FOUND;

	json_t *json_value = json_object_get (json, key);
	if (json_value) {
		value = (long) json_integer_value (json_value);
	}

	else {
		errno = ENOENT;
	}

	return value;

}

static int jwt_json_get_bool (json_t *json, const char *key) {

	int value = JWT_GRANT_NOT_FOUND;

	json_t *json_value = json_object_get (json, key);
	if (json_value) {
		value = (int) json_is_true (json_value);
	}

	else {
		errno = ENOENT;
	}

	return value;

}

// a name of a grant or of a header that can be used
static bool jwt_name_is_valid (const char *name) {

	return name && name[0];

}

// the value with the name, or the whole object without a name,
// dumped with its keys sorted
static char *jwt_json_dump (json_t *object, const char *name) {

	errno = EINVAL;

	json_t *json_value = jwt_name_is_valid (name) ? json_object_get (object, name) : object;
	if (!json_value) return NULL;

	errno = 0;

	return json_dumps (json_value, JWT_JSON_DUMP_FLAGS);

}

// a value of any type is never replaced
// returns 0 on success, EEXIST if the name has a value, or EINVAL on error
static int jwt_json_add (json_t *object, const char *name, json_t *value) {

	int retval = EEXIST;

	if (json_object_get (object, name)) {
		json_decref (value);
	}

	else {
		retval = json_object_set_new (object, name, value) ? EINVAL : 0;
	}

	return retval;

}

// adds the values of an object of json text without repeated keys
static int jwt_json_add_json (json_t *object, const char *json) {

	int retval = -1;

	json_t *values = json_loads (json, JSON_REJECT_DUPLICATES, NULL);

	if (json_is_object (values)) retval = json_object_update (object, values);

	json_decref (values);

	return retval ? EINVAL : 0;

}

// deletes the value with the name, or every one without a name
static void jwt_json_delete (json_t *object, const char *name) {

	if (jwt_name_is_valid (name)) {
		(void) json_object_del (object, name);
	}

	else {
		(void) json_object_clear (object);
	}

}

#pragma endregion

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
	*decoded = (unsigned char *) malloc (base64url_decode_length (part_length) + 1);
	if (*decoded) {
		retval = 0;

		if (base64url_decode (*decoded, decoded_length, part, part_length)) {
			free (*decoded);
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

		free (decoded);
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

	return jwt_str_alg (jwt_json_get_string (jwt->headers, JWT_ALG_HEADER));

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

		free (signature);
	}

	return retval;

}

// an unsigned token can not have a key nor a signature
static int jwt_decode_unsigned (
	const JwtTokenParts *parts, const size_t key_length
) {

	return (key_length || parts->signature_length) ? EINVAL : 0;

}

// verifies a token with the raw key of the first API,
// a key is created for the algorithm that the header names
static int jwt_decode_raw_key (
	const jwt_alg_t alg, const JwtTokenParts *parts,
	const unsigned char *key, const size_t key_length
) {

	int retval = EINVAL;

	// a token that says HS256 with a public PEM as the key would be verified
	// with the PEM as its secret, and anybody can sign with a public key
	JwtKey *verification = jwt_key_create (alg, JWT_KEY_TYPE_PUBLIC, key, key_length, NULL);
	if (verification && (jwt_key_check_strength (verification) != JWT_KEY_ERROR_SECRET)) {
		retval = jwt_verify_signature (verification, parts);
	}

	jwt_key_delete (verification);

	return retval;

}

static int jwt_decode_parts (
	jwt_t *decoded, const JwtTokenParts *parts,
	const unsigned char *key, const size_t key_length
) {

	int retval = jwt_parse_headers (decoded, parts);
	if (retval) return retval;

	jwt_alg_t alg = jwt_headers_alg (decoded);
	if (alg == JWT_ALG_INVAL) {
		retval = EINVAL;
	}

	else if (alg == JWT_ALG_NONE) {
		retval = jwt_decode_unsigned (parts, key_length);
	}

	// without a key, the token is decoded but not verified, as an unsigned one
	else if (!key_length) {
		alg = JWT_ALG_NONE;
	}

	else {
		retval = jwt_decode_raw_key (alg, parts, key, key_length);
	}

	if (!retval) retval = jwt_parse_grants (decoded, parts);

	decoded->alg = alg;

	return retval;

}

int jwt_decode (
	jwt_t **jwt, const char *token,
	const unsigned char *key, size_t key_length
) {

	if (!jwt) return EINVAL;

	*jwt = NULL;

	if (!token || (key_length && !key)) return EINVAL;

	JwtTokenParts parts = { NULL, 0, NULL, 0, NULL, 0, 0 };
	int retval = jwt_token_split (token, strlen (token), &parts);
	if (retval) return retval;

	jwt_t *decoded = NULL;
	retval = jwt_new (&decoded);
	if (!retval) {
		retval = jwt_decode_parts (decoded, &parts, key, key_length);

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

#pragma region grants

const char *jwt_get_grant (jwt_t *jwt, const char *grant) {

	if (!jwt || !jwt_name_is_valid (grant)) {
		errno = EINVAL;
		return NULL;
	}

	errno = 0;

	return jwt_json_get_string (jwt->grants, grant);

}

long jwt_get_grant_int (jwt_t *jwt, const char *grant) {

	if (!jwt || !jwt_name_is_valid (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return jwt_json_get_integer (jwt->grants, grant);

}

int jwt_get_grant_bool (jwt_t *jwt, const char *grant) {

	if (!jwt || !jwt_name_is_valid (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return jwt_json_get_bool (jwt->grants, grant);

}

char *jwt_get_grants_json (jwt_t *jwt, const char *grant) {

	if (!jwt) {
		errno = EINVAL;
		return NULL;
	}

	return jwt_json_dump (jwt->grants, grant);

}

int jwt_add_grant (
	jwt_t *jwt, const char *grant, const char *value
) {

	if (!jwt || !jwt_name_is_valid (grant) || !value) return EINVAL;

	return jwt_json_add (jwt->grants, grant, json_string (value));

}

int jwt_add_grant_int (
	jwt_t *jwt, const char *grant, long value
) {

	if (!jwt || !jwt_name_is_valid (grant)) return EINVAL;

	return jwt_json_add (jwt->grants, grant, json_integer ((json_int_t) value));

}

int jwt_add_grant_bool (
	jwt_t *jwt, const char *grant, int value
) {

	if (!jwt || !jwt_name_is_valid (grant)) return EINVAL;

	return jwt_json_add (jwt->grants, grant, json_boolean (value));

}

int jwt_add_grants_json (jwt_t *jwt, const char *json) {

	if (!jwt) return EINVAL;

	return jwt_json_add_json (jwt->grants, json);

}

int jwt_del_grants (jwt_t *jwt, const char *grant) {

	if (!jwt) return EINVAL;

	jwt_json_delete (jwt->grants, grant);

	return 0;

}

#pragma endregion

#pragma region headers

const char *jwt_get_header (
	jwt_t *jwt, const char *header
) {

	if (!jwt || !jwt_name_is_valid (header)) {
		errno = EINVAL;
		return NULL;
	}

	errno = 0;

	return jwt_json_get_string (jwt->headers, header);

}

long jwt_get_header_int (
	jwt_t *jwt, const char *header
) {

	if (!jwt || !jwt_name_is_valid (header)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return jwt_json_get_integer (jwt->headers, header);

}

int jwt_get_header_bool (
	jwt_t *jwt, const char *header
) {

	if (!jwt || !jwt_name_is_valid (header)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return jwt_json_get_bool (jwt->headers, header);

}

char *jwt_get_headers_json (
	jwt_t *jwt, const char *header
) {

	if (!jwt) {
		errno = EINVAL;
		return NULL;
	}

	return jwt_json_dump (jwt->headers, header);

}

int jwt_add_header (
	jwt_t *jwt, const char *header, const char *value
) {

	if (!jwt || !jwt_name_is_valid (header) || !value) return EINVAL;

	return jwt_json_add (jwt->headers, header, json_string (value));

}

int jwt_add_header_int (
	jwt_t *jwt, const char *header, long value
) {

	if (!jwt || !jwt_name_is_valid (header)) return EINVAL;

	return jwt_json_add (jwt->headers, header, json_integer ((json_int_t) value));

}

int jwt_add_header_bool (
	jwt_t *jwt, const char *header, int value
) {

	if (!jwt || !jwt_name_is_valid (header)) return EINVAL;

	return jwt_json_add (jwt->headers, header, json_boolean (value));

}

int jwt_add_headers_json (jwt_t *jwt, const char *json) {

	if (!jwt) return EINVAL;

	return jwt_json_add_json (jwt->headers, json);

}

int jwt_del_headers (jwt_t *jwt, const char *header) {

	if (!jwt) return EINVAL;

	jwt_json_delete (jwt->headers, header);

	return 0;

}

#pragma endregion

#pragma region dump

// appends the string to the buffer, that is created by the first one
// returns 0 on success, ENOMEM on error
static int jwt_string_append (char **buffer, const char *string) {

	const size_t length = *buffer ? strlen (*buffer) : 0;

	char *appended = (char *) realloc (*buffer, length + strlen (string) + 1);
	if (!appended) return ENOMEM;

	(void) strcpy (appended + length, string);

	*buffer = appended;

	return 0;

}

static int jwt_json_write (
	const json_t *json, char **buffer, int pretty
) {

	// the keys are sorted for repeatability
	const size_t flags = JSON_SORT_KEYS | (pretty ? JSON_INDENT (JWT_DUMP_INDENT) : JSON_COMPACT);

	char *serialized = json_dumps (json, flags);
	if (!serialized) return ENOMEM;

	int retval = pretty ? jwt_string_append (buffer, JWT_DUMP_NEW_LINE) : 0;

	if (!retval) retval = jwt_string_append (buffer, serialized);

	if (!retval && pretty) retval = jwt_string_append (buffer, JWT_DUMP_NEW_LINE);

	free (serialized);

	return retval;

}

// the name of the algorithm in the header of a token
static const char *jwt_alg_header_name (const jwt_alg_t alg) {

	return (alg == JWT_ALG_NONE) ? JWT_ALG_NONE_HEADER_NAME : jwt_alg_str (alg);

}

static int jwt_write_head (
	jwt_t *jwt, char **buffer, int pretty
) {

	int retval = 0;

	// the default type is only added if it has not been defined,
	// so any value of it can signal an extension of JWT, like PASSporT (RFC 8225)
	if (jwt->alg != JWT_ALG_NONE) {
		retval = jwt_add_header (jwt, JWT_TYPE_HEADER, JWT_TYPE_HEADER_VALUE);
		if (retval && (retval != EEXIST)) return retval;
	}

	retval = jwt_del_headers (jwt, JWT_ALG_HEADER);
	if (retval) return retval;

	retval = jwt_add_header (jwt, JWT_ALG_HEADER, jwt_alg_header_name (jwt->alg));
	if (retval) return retval;

	return jwt_json_write (jwt->headers, buffer, pretty);

}

static int jwt_write_body (jwt_t *jwt, char **buffer, int pretty) {

	return jwt_json_write (jwt->grants, buffer, pretty);

}

static int jwt_dump (jwt_t *jwt, char **buffer, int pretty) {

	int retval = jwt_write_head (jwt, buffer, pretty);

	if (!retval) retval = jwt_string_append (buffer, JWT_PART_SEPARATOR_STRING);

	if (!retval) retval = jwt_write_body (jwt, buffer, pretty);

	return retval;

}

int jwt_dump_fp (jwt_t *jwt, FILE *fp, int pretty) {

	char *dumped = NULL;

	const int retval = jwt_dump (jwt, &dumped, pretty);

	if (!retval) (void) fputs (dumped, fp);

	free (dumped);

	return retval;

}

char *jwt_dump_str (jwt_t *jwt, int pretty) {

	char *dumped = NULL;

	errno = jwt_dump (jwt, &dumped, pretty);
	if (errno) {
		free (dumped);
		dumped = NULL;
	}

	return dumped;

}

#pragma endregion

#pragma region encode

// adds the separator and the signature in base64url to the signed parts
static int jwt_encode_signature (
	char **token, size_t signed_length,
	const unsigned char *signature, const size_t signature_length
) {

	int retval = ENOMEM;

	char *signed_token = (char *) realloc (
		*token, signed_length + 1 + base64url_encode_length (signature_length) + 1
	);

	if (signed_token) {
		signed_token[signed_length] = JWT_PART_SEPARATOR;

		(void) base64url_encode (
			signed_token + signed_length + 1, signature, signature_length
		);

		*token = signed_token;
		retval = 0;
	}

	return retval;

}

// the header and the payload in base64url, signed with the key,
// or with an empty signature if there is no key
static int jwt_encode_serialized (
	const JwtKey *key, const char *header, const char *payload, char **encoded
) {

	const size_t header_length = strlen (header);
	const size_t payload_length = strlen (payload);

	const size_t signed_length = base64url_encode_length (header_length)
		+ 1 + base64url_encode_length (payload_length);

	// the separator of the signature and the null byte
	char *token = (char *) malloc (signed_length + 2);
	if (!token) return ENOMEM;

	size_t position = base64url_encode (token, (const unsigned char *) header, header_length);
	token[position++] = JWT_PART_SEPARATOR;
	(void) base64url_encode (token + position, (const unsigned char *) payload, payload_length);

	int retval = 0;
	if (key) {
		unsigned char *signature = NULL;
		size_t signature_length = 0;

		retval = jwt_key_sign (key, token, signed_length, &signature, &signature_length);
		if (!retval) retval = jwt_encode_signature (&token, signed_length, signature, signature_length);

		free (signature);
	}

	else {
		token[signed_length] = JWT_PART_SEPARATOR;
		token[signed_length + 1] = '\0';
	}

	if (retval) free (token);
	else *encoded = token;

	return retval;

}

static int jwt_encode_parts (
	jwt_t *jwt, const JwtKey *key, char **encoded
) {

	char *header = NULL;
	char *payload = NULL;

	int retval = jwt_write_head (jwt, &header, 0);

	if (!retval) retval = jwt_write_body (jwt, &payload, 0);

	if (!retval) retval = jwt_encode_serialized (key, header, payload, encoded);

	free (header);
	free (payload);

	return retval;

}

// the token of the first API: the key is created from the raw key of the jwt
static int jwt_encode (jwt_t *jwt, char **encoded) {

	if (jwt->alg == JWT_ALG_NONE) return jwt_encode_parts (jwt, NULL, encoded);

	JwtKey *signing = jwt_key_create (
		jwt->alg, JWT_KEY_TYPE_PRIVATE, jwt->key, jwt->key_length, NULL
	);

	if (!signing) return EINVAL;

	const int retval = jwt_encode_parts (jwt, signing, encoded);

	jwt_key_delete (signing);

	return retval;

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
		free (token);
		token = NULL;
	}

	return token;

}

int jwt_encode_fp (jwt_t *jwt, FILE *fp) {

	char *token = NULL;

	const int retval = jwt_encode (jwt, &token);

	if (!retval) (void) fputs (token, fp);

	free (token);

	return retval;

}

char *jwt_encode_str (jwt_t *jwt) {

	char *token = NULL;

	errno = jwt_encode (jwt, &token);
	if (errno) {
		free (token);
		token = NULL;
	}

	return token;

}

void jwt_free_str (char *str) {

	free (str);

}

#pragma endregion

#pragma region valid

int jwt_valid_new (
	jwt_valid_t **jwt_valid, jwt_alg_t alg
) {

	if (!jwt_valid) return EINVAL;

	*jwt_valid = (jwt_valid_t *) calloc (1, sizeof (jwt_valid_t));
	if (!*jwt_valid) return ENOMEM;

	(*jwt_valid)->alg = alg;

	(*jwt_valid)->status = JWT_VALIDATION_ERROR;

	(*jwt_valid)->required_grants = json_object ();
	if (!(*jwt_valid)->required_grants) {
		free (*jwt_valid);
		*jwt_valid = NULL;
		return ENOMEM;
	}

	return 0;

}

void jwt_valid_free (jwt_valid_t *jwt_valid) {

	if (jwt_valid) {
		json_decref (jwt_valid->required_grants);

		free (jwt_valid);
	}

}

unsigned int jwt_valid_get_status (jwt_valid_t *jwt_valid) {

	return jwt_valid ? jwt_valid->status : JWT_VALIDATION_ERROR;

}

int jwt_valid_add_grant (
	jwt_valid_t *jwt_valid,
	const char *grant, const char *value
) {

	if (!jwt_valid || !jwt_name_is_valid (grant) || !value) return EINVAL;

	if (jwt_json_get_string (jwt_valid->required_grants, grant)) return EEXIST;

	return json_object_set_new (jwt_valid->required_grants, grant, json_string (value)) ? EINVAL : 0;

}

int jwt_valid_add_grant_int (
	jwt_valid_t *jwt_valid,
	const char *grant, long value
) {

	if (!jwt_valid || !jwt_name_is_valid (grant)) return EINVAL;

	if (jwt_json_get_integer (jwt_valid->required_grants, grant) != JWT_GRANT_NOT_FOUND) return EEXIST;

	return json_object_set_new (
		jwt_valid->required_grants, grant, json_integer ((json_int_t) value)
	) ? EINVAL : 0;

}

int jwt_valid_add_grant_bool (
	jwt_valid_t *jwt_valid,
	const char *grant, int value
) {

	if (!jwt_valid || !jwt_name_is_valid (grant)) return EINVAL;

	if (jwt_json_get_integer (jwt_valid->required_grants, grant) != JWT_GRANT_NOT_FOUND) return EEXIST;

	return json_object_set_new (
		jwt_valid->required_grants, grant, json_boolean (value)
	) ? EINVAL : 0;

}

int jwt_valid_add_grants_json (
	jwt_valid_t *jwt_valid, const char *json
) {

	if (!jwt_valid) return EINVAL;

	return jwt_json_add_json (jwt_valid->required_grants, json);

}

char *jwt_valid_get_grants_json (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid) {
		errno = EINVAL;
		return NULL;
	}

	return jwt_json_dump (jwt_valid->required_grants, grant);

}

const char *jwt_valid_get_grant (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid || !jwt_name_is_valid (grant)) {
		errno = EINVAL;
		return NULL;
	}

	errno = 0;

	return jwt_json_get_string (jwt_valid->required_grants, grant);

}

long jwt_valid_get_grant_int (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid || !jwt_name_is_valid (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return jwt_json_get_integer (jwt_valid->required_grants, grant);

}

int jwt_valid_get_grant_bool (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid || !jwt_name_is_valid (grant)) {
		errno = EINVAL;
		return 0;
	}

	errno = 0;

	return jwt_json_get_bool (jwt_valid->required_grants, grant);

}

int jwt_valid_set_now (
	jwt_valid_t *jwt_valid, const time_t now
) {

	if (!jwt_valid) return EINVAL;

	jwt_valid->now = now;

	return 0;

}

int jwt_valid_set_headers (
	jwt_valid_t *jwt_valid, int headers
) {

	if (!jwt_valid) return EINVAL;

	jwt_valid->headers = headers;

	return 0;

}

int jwt_valid_del_grants (
	jwt_valid_t *jwt_valid, const char *grant
) {

	if (!jwt_valid) return EINVAL;

	jwt_json_delete (jwt_valid->required_grants, grant);

	return 0;

}

// a claim that is both in the header and in the payload must be the same
static bool jwt_claim_is_replicated_differently (
	const jwt_t *jwt, const char *claim
) {

	const char *header_value = jwt_json_get_string (jwt->headers, claim);
	const char *grant_value = jwt_json_get_string (jwt->grants, claim);

	return header_value && grant_value && strcmp (header_value, grant_value);

}

// the times of the token against the time of the validation
static unsigned int jwt_validate_times (
	const jwt_t *jwt, const jwt_valid_t *jwt_valid
) {

	unsigned int status = JWT_VALIDATION_SUCCESS;

	const time_t expiration = (time_t) jwt_json_get_integer (jwt->grants, JWT_CLAIM_EXPIRATION);
	if (jwt_valid->now && (expiration != JWT_GRANT_NOT_FOUND) && (jwt_valid->now >= expiration)) {
		status |= JWT_VALIDATION_EXPIRED;
	}

	const time_t not_before = (time_t) jwt_json_get_integer (jwt->grants, JWT_CLAIM_NOT_BEFORE);
	if (jwt_valid->now && (not_before != JWT_GRANT_NOT_FOUND) && (jwt_valid->now < not_before)) {
		status |= JWT_VALIDATION_TOO_NEW;
	}

	return status;

}

// the claims that are in the header and in the payload, the audience can be an array
static unsigned int jwt_validate_replicated (const jwt_t *jwt) {

	unsigned int status = JWT_VALIDATION_SUCCESS;

	if (jwt_claim_is_replicated_differently (jwt, JWT_CLAIM_ISSUER)) status |= JWT_VALIDATION_ISS_MISMATCH;

	if (jwt_claim_is_replicated_differently (jwt, JWT_CLAIM_SUBJECT)) status |= JWT_VALIDATION_SUB_MISMATCH;

	json_t *header_audience = json_object_get (jwt->headers, JWT_CLAIM_AUDIENCE);
	json_t *grant_audience = json_object_get (jwt->grants, JWT_CLAIM_AUDIENCE);
	if (header_audience && grant_audience && !json_equal (header_audience, grant_audience)) {
		status |= JWT_VALIDATION_AUD_MISMATCH;
	}

	return status;

}

static unsigned int jwt_validate_required_grants (
	const jwt_t *jwt, const jwt_valid_t *jwt_valid
) {

	unsigned int status = JWT_VALIDATION_SUCCESS;

	const char *required_grant = NULL;
	json_t *required_value = NULL;
	json_object_foreach (jwt_valid->required_grants, required_grant, required_value) {
		json_t *actual_value = json_object_get (jwt->grants, required_grant);

		if (!actual_value) {
			status |= JWT_VALIDATION_GRANT_MISSING;
		}

		else if (!json_equal (required_value, actual_value)) {
			status |= JWT_VALIDATION_GRANT_MISMATCH;
		}
	}

	return status;

}

unsigned int jwt_validate (
	jwt_t *jwt, jwt_valid_t *jwt_valid
) {

	if (!jwt_valid) return JWT_VALIDATION_ERROR;

	if (!jwt) {
		jwt_valid->status = JWT_VALIDATION_ERROR;
		return jwt_valid->status;
	}

	jwt_valid->status = JWT_VALIDATION_SUCCESS;

	if (jwt_valid->alg != jwt_get_alg (jwt)) jwt_valid->status |= JWT_VALIDATION_ALG_MISMATCH;

	jwt_valid->status |= jwt_validate_times (jwt, jwt_valid);

	jwt_valid->status |= jwt_validate_replicated (jwt);

	jwt_valid->status |= jwt_validate_required_grants (jwt, jwt_valid);

	return jwt_valid->status;

}

#pragma endregion
