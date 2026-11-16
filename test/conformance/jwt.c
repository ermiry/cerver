#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cerver/types/types.h>

#include <cerver/collections/arena.h>

#include <cerver/crypto/key.h>
#include <cerver/crypto/signature.h>

#include <cerver/json/document.h>
#include <cerver/json/json.h>
#include <cerver/json/value.h>
#include <cerver/json/writer.h>

#include <cerver/jwt/jwt.h>
#include <cerver/jwt/key.h>
#include <cerver/jwt/verify.h>

// runs the vectors of a suite of cryptography (Wycheproof) through the provider of cerver/crypto and through the
// tokens of cerver/jwt: every file of the suite has groups of tests with a key, and each test has a message with
// its signature (or a whole token) and what an implementation must do with it: valid, invalid or acceptable.
// The tests that cerver decides, and how many of each kind every file has, are in a file of the repository
// usage: test/bin/conformance/jwt <directory of the vectors> <file of the decisions>

#define CONFORMANCE_PATH_SIZE				(1 << 10)
#define CONFORMANCE_LINE_SIZE				(1 << 10)
#define CONFORMANCE_NAME_SIZE				(1 << 8)

// the most decisions that the file of the repository can have
#define CONFORMANCE_DECISIONS_SIZE			(1 << 8)

// the arguments of the program: itself, the directory and the file; a line of the file has as many fields
#define CONFORMANCE_ARGUMENTS				3

// the text of a JWK of a group
#define CONFORMANCE_JWK_SIZE				(1 << 13)

#define CONFORMANCE_HEX_DIGITS				2
#define CONFORMANCE_HEX_SHIFT				4
#define CONFORMANCE_HEX_LETTER				10

#define CONFORMANCE_BYTE_BITS				8

static const char CONFORMANCE_ACCEPT[] = "accept";
static const char CONFORMANCE_REJECT[] = "reject";
static const char CONFORMANCE_SUITE[] = "suite";

// the first character of a line of the decisions that is not read
#define CONFORMANCE_COMMENT					'#'

// the members of a file of the suite, of its groups and of its tests
#define CONFORMANCE_MEMBER_GROUPS			"testGroups"
#define CONFORMANCE_MEMBER_TESTS			"tests"
#define CONFORMANCE_MEMBER_ID				"tcId"
#define CONFORMANCE_MEMBER_COMMENT			"comment"
#define CONFORMANCE_MEMBER_RESULT			"result"
#define CONFORMANCE_MEMBER_MESSAGE			"msg"
#define CONFORMANCE_MEMBER_SIGNATURE		"sig"
#define CONFORMANCE_MEMBER_SECRET			"key"
#define CONFORMANCE_MEMBER_TAG				"tag"
#define CONFORMANCE_MEMBER_TAG_BITS			"tagSize"
#define CONFORMANCE_MEMBER_TOKEN			"jws"
#define CONFORMANCE_MEMBER_PEM				"publicKeyPem"
#define CONFORMANCE_MEMBER_PUBLIC			"public"
#define CONFORMANCE_MEMBER_PRIVATE			"private"
#define CONFORMANCE_MEMBER_ALGORITHM		"alg"

// the name that the suite gives the algorithm of its keys of P-521, after their curve
#define CONFORMANCE_ALGORITHM_P521			"ES521"

// any secret, to ask an HMAC for the bytes of its digest
#define CONFORMANCE_ANY_SECRET				"key"

// the first digit and the first letter of a hex
#define CONFORMANCE_HEX_FIRST_DIGIT			'0'
#define CONFORMANCE_HEX_FIRST_LETTER		'a'

// what a test of the suite says that an implementation must do with it
typedef enum ConformanceResult {

	CONFORMANCE_RESULT_VALID,
	CONFORMANCE_RESULT_INVALID,
	CONFORMANCE_RESULT_ACCEPTABLE,
	CONFORMANCE_RESULT_COUNT

} ConformanceResult;

static const char *conformance_result_names[CONFORMANCE_RESULT_COUNT] = { "valid", "invalid", "acceptable" };

// how the tests of a file are run
typedef enum ConformanceKind {

	// a secret, a message and its HMAC
	CONFORMANCE_KIND_MAC,
	// the PEM of a public key, a message and its signature
	CONFORMANCE_KIND_SIGNATURE,
	// the JWK of a key and a token
	CONFORMANCE_KIND_TOKEN

} ConformanceKind;

// a file of the suite
typedef struct ConformanceFile {

	const char *name;

	ConformanceKind kind;

	// the algorithm of the provider that verifies its tests
	CerverCryptoSignature algorithm;

	// the salt of its signatures of PSS is not the one of its digest: cerver refuses all of them
	bool other_salt;

} ConformanceFile;

static const ConformanceFile conformance_files[] = {
	{ "hmac_sha256_test.json", CONFORMANCE_KIND_MAC, CERVER_CRYPTO_SIGNATURE_HMAC_SHA256, false },
	{ "hmac_sha384_test.json", CONFORMANCE_KIND_MAC, CERVER_CRYPTO_SIGNATURE_HMAC_SHA384, false },
	{ "hmac_sha512_test.json", CONFORMANCE_KIND_MAC, CERVER_CRYPTO_SIGNATURE_HMAC_SHA512, false },
	{ "rsa_signature_2048_sha256_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256, false },
	{ "rsa_signature_2048_sha384_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA384, false },
	{ "rsa_signature_2048_sha512_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA512, false },
	{ "rsa_signature_3072_sha256_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256, false },
	{ "rsa_signature_3072_sha384_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA384, false },
	{ "rsa_signature_3072_sha512_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA512, false },
	{ "rsa_signature_4096_sha256_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA256, false },
	{ "rsa_signature_4096_sha384_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA384, false },
	{ "rsa_signature_4096_sha512_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PKCS1_SHA512, false },
	{ "rsa_pss_2048_sha256_mgf1_32_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, false },
	{ "rsa_pss_3072_sha256_mgf1_32_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, false },
	{ "rsa_pss_4096_sha256_mgf1_32_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, false },
	{ "rsa_pss_2048_sha384_mgf1_48_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA384, false },
	{ "rsa_pss_4096_sha384_mgf1_48_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA384, false },
	{ "rsa_pss_4096_sha512_mgf1_64_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA512, false },
	{ "rsa_pss_2048_sha256_mgf1_0_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_RSA_PSS_SHA256, true },
	{ "ecdsa_secp256r1_sha256_p1363_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_ECDSA_P256_SHA256, false },
	{ "ecdsa_secp384r1_sha384_p1363_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_ECDSA_P384_SHA384, false },
	{ "ecdsa_secp521r1_sha512_p1363_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_ECDSA_P521_SHA512, false },
	{ "ed25519_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_ED25519, false },
	{ "ed448_test.json", CONFORMANCE_KIND_SIGNATURE, CERVER_CRYPTO_SIGNATURE_ED448, false },
	{ "json_web_signature_test.json", CONFORMANCE_KIND_TOKEN, CERVER_CRYPTO_SIGNATURE_NONE, false }
};

#define CONFORMANCE_FILES					(sizeof (conformance_files) / sizeof (conformance_files[0]))

// what cerver does with a test that the suite leaves to the implementation, or with one where it does not do
// what the suite says: the name of its file and its number
typedef struct ConformanceDecision {

	char name[CONFORMANCE_NAME_SIZE];
	bool accept;
	bool used;

} ConformanceDecision;

// the tests of each kind that a file of the suite has
typedef struct ConformanceCounts {

	size_t tests[CONFORMANCE_RESULT_COUNT];

} ConformanceCounts;

// the decisions of the repository and how many tests of each kind every file has
typedef struct ConformanceDecisions {

	ConformanceDecision decisions[CONFORMANCE_DECISIONS_SIZE];
	size_t count;

	ConformanceCounts expected[CONFORMANCE_FILES];

} ConformanceDecisions;

// what a run found
typedef struct ConformanceResults {

	ConformanceCounts found[CONFORMANCE_FILES];
	size_t failed;

} ConformanceResults;

// the file that is being run, its group and its memory
typedef struct ConformanceRun {

	const ConformanceFile *file;
	size_t index;

	ConformanceDecisions *decisions;
	ConformanceResults *results;

	// the memory of a test: its bytes, and the claims of its token
	Arena *arena;

	// the keys of the group: one of the provider, or one of the tokens; a group whose key cerver does not take
	// has none, and every one of its tests is refused
	CerverCryptoKey *key;
	CerverJwtKey *token_key;

} ConformanceRun;

#pragma region decisions

static bool conformance_file_index (const char *name, size_t *index) {

	for (size_t i = 0; i < CONFORMANCE_FILES; i++) {
		if (!strcmp (conformance_files[i].name, name)) {
			*index = i;
			return true;
		}
	}

	return false;

}

// "suite <file> <valid> <invalid> <acceptable>": the tests of a file that are run
static bool conformance_decisions_suite (ConformanceDecisions *decisions, const char *name, const char *rest) {

	size_t index = 0;
	if (!conformance_file_index (name, &index)) return false;

	ConformanceCounts *counts = &decisions->expected[index];

	return sscanf (
		rest, "%zu %zu %zu",
		&counts->tests[CONFORMANCE_RESULT_VALID], &counts->tests[CONFORMANCE_RESULT_INVALID],
		&counts->tests[CONFORMANCE_RESULT_ACCEPTABLE]
	) == CONFORMANCE_RESULT_COUNT;

}

// a line of the file: a suite, or "accept <file>:<test> <why>" and "reject <file>:<test> <why>";
// an empty line and one that starts with # are not read
static bool conformance_decisions_line (ConformanceDecisions *decisions, const char *line) {

	char verb[CONFORMANCE_NAME_SIZE] = { 0 };
	char name[CONFORMANCE_NAME_SIZE] = { 0 };
	char rest[CONFORMANCE_LINE_SIZE] = { 0 };

	const int fields = sscanf (line, "%255s %255s %1023[^\n]", verb, name, rest);
	if ((fields <= 0) || (verb[0] == CONFORMANCE_COMMENT)) return true;

	if (fields < CONFORMANCE_ARGUMENTS) return false;

	if (!strcmp (verb, CONFORMANCE_SUITE)) return conformance_decisions_suite (decisions, name, rest);

	const bool accept = !strcmp (verb, CONFORMANCE_ACCEPT);
	if (!accept && strcmp (verb, CONFORMANCE_REJECT)) return false;

	if (decisions->count >= CONFORMANCE_DECISIONS_SIZE) return false;

	ConformanceDecision *decision = &decisions->decisions[decisions->count];
	(void) snprintf (decision->name, sizeof (decision->name), "%s", name);
	decision->accept = accept;
	decision->used = false;

	decisions->count += 1;

	return true;

}

static bool conformance_decisions_load (ConformanceDecisions *decisions, const char *path) {

	bool retval = false;

	char line[CONFORMANCE_LINE_SIZE];

	FILE *file = fopen (path, "r");
	if (file) {
		retval = true;

		while (retval && fgets (line, sizeof (line), file)) {
			retval = conformance_decisions_line (decisions, line);
			if (!retval) (void) fprintf (stderr, "The line <%s> of %s is not a decision\n", line, path);
		}

		(void) fclose (file);
	}

	else {
		(void) fprintf (stderr, "The file %s could not be opened\n", path);
	}

	return retval;

}

static ConformanceDecision *conformance_decisions_find (ConformanceDecisions *decisions, const char *name) {

	for (size_t i = 0; i < decisions->count; i++) {
		if (!strcmp (decisions->decisions[i].name, name)) return &decisions->decisions[i];
	}

	return NULL;

}

#pragma endregion

#pragma region values

// the whole content of a file
static char *conformance_file_read (const char *path, size_t *length) {

	char *content = NULL;

	FILE *file = fopen (path, "rb");
	if (file) {
		if (!fseek (file, 0, SEEK_END)) {
			const long size = ftell (file);
			if ((size >= 0) && !fseek (file, 0, SEEK_SET)) {
				content = (char *) malloc ((size_t) size + 1);
				if (content && (fread (content, 1, (size_t) size, file) != (size_t) size)) {
					free (content);
					content = NULL;
				}

				*length = (size_t) size;
			}
		}

		(void) fclose (file);
	}

	return content;

}

static unsigned int conformance_hex_value (const char character) {

	return (character >= CONFORMANCE_HEX_FIRST_LETTER) ?
		(unsigned int) (character - CONFORMANCE_HEX_FIRST_LETTER) + CONFORMANCE_HEX_LETTER :
		(unsigned int) (character - CONFORMANCE_HEX_FIRST_DIGIT);

}

// the bytes of a member of a test that is a hex, in the memory of the test; an empty one has no bytes
static const u8 *conformance_bytes (
	ConformanceRun *run, const CerverJsonValue *object, const char *name, size_t *size
) {

	const CerverJsonValue *member = cerver_json_object_get (object, name);
	const char *hex = cerver_json_string_value (member);

	*size = cerver_json_string_length (member) / CONFORMANCE_HEX_DIGITS;

	u8 *bytes = (u8 *) arena_alloc (run->arena, *size + 1);
	if (!bytes || !hex) {
		(void) fprintf (stderr, "%s: a test has no %s\n", run->file->name, name);
		exit (1);
	}

	for (size_t i = 0; i < *size; i++) {
		bytes[i] = (u8) (
			(conformance_hex_value (hex[i * CONFORMANCE_HEX_DIGITS]) << CONFORMANCE_HEX_SHIFT)
			| conformance_hex_value (hex[(i * CONFORMANCE_HEX_DIGITS) + 1])
		);
	}

	return bytes;

}

static bool conformance_string_is (const CerverJsonValue *value, const char *text) {

	return cerver_json_is_string (value) && !strcmp (cerver_json_string_value (value), text);

}

#pragma endregion

#pragma region keys

static void conformance_keys_delete (ConformanceRun *run) {

	cerver_crypto_key_delete (run->key);
	cerver_jwt_key_delete (run->token_key);

	run->key = NULL;
	run->token_key = NULL;

}

// the algorithm of the tokens of a JWK of the suite, the one of its "alg":
// the suite names the one of its keys of P-521 after their curve, ES521
static CerverJwtAlgorithm conformance_token_algorithm (const CerverJsonValue *jwk) {

	const CerverJsonValue *algorithm = cerver_json_object_get (jwk, CONFORMANCE_MEMBER_ALGORITHM);

	if (conformance_string_is (algorithm, CONFORMANCE_ALGORITHM_P521)) return CERVER_JWT_ALGORITHM_ES512;

	return cerver_jwt_algorithm_from_string (
		cerver_json_string_value (algorithm), cerver_json_string_length (algorithm)
	);

}

// the key of the tokens of a group: its public JWK, or its private one when it has no public key (a secret)
static void conformance_token_key (ConformanceRun *run, const CerverJsonValue *group) {

	const CerverJsonValue *jwk = cerver_json_object_get (group, CONFORMANCE_MEMBER_PUBLIC);
	if (!jwk) jwk = cerver_json_object_get (group, CONFORMANCE_MEMBER_PRIVATE);

	static char text[CONFORMANCE_JWK_SIZE];
	const size_t length = cerver_json_write (jwk, text, sizeof (text), CERVER_JSON_WRITE_COMPACT);

	if (length && (length < sizeof (text))) {
		run->token_key = cerver_jwt_key_create_jwk (conformance_token_algorithm (jwk), text, length, NULL);
	}

}

// the keys of a group; the secret of an HMAC is the one of each test
static void conformance_group_keys (ConformanceRun *run, const CerverJsonValue *group) {

	if (run->file->kind == CONFORMANCE_KIND_SIGNATURE) {
		const CerverJsonValue *pem = cerver_json_object_get (group, CONFORMANCE_MEMBER_PEM);

		run->key = cerver_crypto_key_create_pem (
			cerver_json_string_value (pem), cerver_json_string_length (pem), false, NULL
		);
	}

	else if (run->file->kind == CONFORMANCE_KIND_TOKEN) {
		conformance_token_key (run, group);
	}

}

// the tests of a group are run when cerver has what they test: an HMAC is never cut, so the groups whose tags
// have less bits than their digest are left out
static bool conformance_group_is_run (const ConformanceRun *run, const CerverJsonValue *group) {

	if (run->file->kind != CONFORMANCE_KIND_MAC) return true;

	u8 mac[CERVER_CRYPTO_SIGNATURE_SIZE_MAX];
	size_t mac_size = 0;

	CerverCryptoKey *key = cerver_crypto_key_create_secret (
		CONFORMANCE_ANY_SECRET, sizeof (CONFORMANCE_ANY_SECRET) - 1, NULL
	);

	const bool computed = cerver_crypto_sign (key, run->file->algorithm, NULL, 0, mac, sizeof (mac), &mac_size);
	cerver_crypto_key_delete (key);

	const i64 tag_bits = cerver_json_integer_value (cerver_json_object_get (group, CONFORMANCE_MEMBER_TAG_BITS));

	return computed && (tag_bits == (i64) (mac_size * CONFORMANCE_BYTE_BITS));

}

#pragma endregion

#pragma region tests

// whether cerver takes the HMAC of a test with its secret
static bool conformance_mac_accepts (ConformanceRun *run, const CerverJsonValue *test) {

	size_t key_size = 0;
	size_t message_size = 0;
	size_t tag_size = 0;
	const u8 *secret = conformance_bytes (run, test, CONFORMANCE_MEMBER_SECRET, &key_size);
	const u8 *message = conformance_bytes (run, test, CONFORMANCE_MEMBER_MESSAGE, &message_size);
	const u8 *tag = conformance_bytes (run, test, CONFORMANCE_MEMBER_TAG, &tag_size);

	CerverCryptoKey *key = cerver_crypto_key_create_secret (secret, key_size, NULL);

	const bool accepted = cerver_crypto_verify (key, run->file->algorithm, message, message_size, tag, tag_size);

	cerver_crypto_key_delete (key);

	return accepted;

}

// whether cerver takes the signature of a test with the key of its group
static bool conformance_signature_accepts (ConformanceRun *run, const CerverJsonValue *test) {

	size_t message_size = 0;
	size_t signature_size = 0;
	const u8 *message = conformance_bytes (run, test, CONFORMANCE_MEMBER_MESSAGE, &message_size);
	const u8 *signature = conformance_bytes (run, test, CONFORMANCE_MEMBER_SIGNATURE, &signature_size);

	return cerver_crypto_verify (run->key, run->file->algorithm, message, message_size, signature, signature_size);

}

// whether cerver takes the signature of the token of a test with the key of its group: the payloads of the
// suite are not the claims of a JWT, and the claims are only read once the signature was verified,
// so the error of the claims of a token is the one of a token that was signed
static bool conformance_token_accepts (ConformanceRun *run, const CerverJsonValue *test) {

	const CerverJsonValue *token = cerver_json_object_get (test, CONFORMANCE_MEMBER_TOKEN);

	const CerverJwtError error = cerver_jwt_verify (
		run->token_key, run->arena,
		cerver_json_string_value (token), cerver_json_string_length (token), NULL, NULL
	);

	return (error == CERVER_JWT_ERROR_NONE) || (error >= CERVER_JWT_ERROR_CLAIMS);

}

static bool conformance_test_accepts (ConformanceRun *run, const CerverJsonValue *test) {

	bool accepted = false;

	switch (run->file->kind) {
		case CONFORMANCE_KIND_MAC: accepted = conformance_mac_accepts (run, test); break;
		case CONFORMANCE_KIND_SIGNATURE: accepted = run->key && conformance_signature_accepts (run, test); break;
		case CONFORMANCE_KIND_TOKEN: accepted = run->token_key && conformance_token_accepts (run, test); break;

		default: break;
	}

	return accepted;

}

static ConformanceResult conformance_test_result (const ConformanceRun *run, const CerverJsonValue *test) {

	const CerverJsonValue *result = cerver_json_object_get (test, CONFORMANCE_MEMBER_RESULT);

	for (size_t i = 0; i < CONFORMANCE_RESULT_COUNT; i++) {
		if (conformance_string_is (result, conformance_result_names[i])) return (ConformanceResult) i;
	}

	(void) fprintf (stderr, "%s: a test has a result that is not known\n", run->file->name);
	exit (1);

}

// what cerver must do with a test: what the decision of the repository says when it has one; if not,
// what the suite says, and a test that the suite leaves to the implementation without a decision is an error.
// The signatures of a file with another salt are all refused
static bool conformance_test_expected (
	ConformanceRun *run, const ConformanceResult result, const char *name, bool *accept
) {

	ConformanceDecision *decision = conformance_decisions_find (run->decisions, name);
	if (decision) {
		decision->used = true;
		*accept = decision->accept;
		return true;
	}

	if (result == CONFORMANCE_RESULT_ACCEPTABLE) return false;

	*accept = (result == CONFORMANCE_RESULT_VALID) && !run->file->other_salt;

	return true;

}

static void conformance_test_run (ConformanceRun *run, const CerverJsonValue *test) {

	char name[CONFORMANCE_NAME_SIZE] = { 0 };
	(void) snprintf (
		name, sizeof (name), "%s:%lld",
		run->file->name, (long long) cerver_json_integer_value (cerver_json_object_get (test, CONFORMANCE_MEMBER_ID))
	);

	const ConformanceResult result = conformance_test_result (run, test);
	run->results->found[run->index].tests[result] += 1;

	bool expected = false;
	if (!conformance_test_expected (run, result, name, &expected)) {
		(void) fprintf (stderr, "%s has no decision\n", name);
		run->results->failed += 1;
		return;
	}

	arena_reset (run->arena);

	const bool accepted = conformance_test_accepts (run, test);
	if (accepted != expected) {
		(void) fprintf (
			stderr, "%s (%s, %s) was %s\n", name, conformance_result_names[result],
			cerver_json_string_value (cerver_json_object_get (test, CONFORMANCE_MEMBER_COMMENT)),
			accepted ? "accepted" : "rejected"
		);

		run->results->failed += 1;
	}

}

static void conformance_group_run (ConformanceRun *run, const CerverJsonValue *group) {

	if (!conformance_group_is_run (run, group)) return;

	conformance_group_keys (run, group);

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (
		cerver_json_object_get (group, CONFORMANCE_MEMBER_TESTS)
	);

	const CerverJsonValue *test = NULL;
	while ((test = cerver_json_array_iterator_next (&iterator))) {
		conformance_test_run (run, test);
	}

	conformance_keys_delete (run);

}

// a file of the suite: its groups and their tests
static bool conformance_file_run (ConformanceRun *run, const char *directory) {

	char path[CONFORMANCE_PATH_SIZE];
	(void) snprintf (path, sizeof (path), "%s/%s", directory, run->file->name);

	size_t length = 0;
	char *content = conformance_file_read (path, &length);
	if (!content) {
		(void) fprintf (stderr, "%s could not be read\n", path);
		return false;
	}

	CerverJsonDocument *document = cerver_json_parse (NULL, content, length, CERVER_JSON_FLAGS_NONE, NULL, NULL);
	const CerverJsonValue *groups = cerver_json_object_get (
		cerver_json_document_root (document), CONFORMANCE_MEMBER_GROUPS
	);

	const bool retval = cerver_json_is_array (groups);
	if (!retval) (void) fprintf (stderr, "%s is not a file of the suite\n", path);

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (groups);
	const CerverJsonValue *group = NULL;
	while ((group = cerver_json_array_iterator_next (&iterator))) {
		conformance_group_run (run, group);
	}

	cerver_json_document_delete (document);

	free (content);

	return retval;

}

static bool conformance_run (
	ConformanceDecisions *decisions, const char *directory, ConformanceResults *results
) {

	bool retval = true;

	ConformanceRun run = { NULL, 0, decisions, results, arena_create (0), NULL, NULL };
	if (!run.arena) return false;

	for (size_t i = 0; retval && (i < CONFORMANCE_FILES); i++) {
		run.file = &conformance_files[i];
		run.index = i;

		retval = conformance_file_run (&run, directory);
	}

	arena_delete (run.arena);

	return retval;

}

#pragma endregion

// the suite must have the tests that the decisions were written for, and every decision its test
static bool conformance_check (const ConformanceDecisions *decisions, const ConformanceResults *results) {

	bool retval = !results->failed;

	for (size_t i = 0; i < CONFORMANCE_FILES; i++) {
		const ConformanceCounts *found = &results->found[i];
		const ConformanceCounts *expected = &decisions->expected[i];

		(void) printf (
			"%s: %zu valid, %zu invalid, %zu acceptable\n", conformance_files[i].name,
			found->tests[CONFORMANCE_RESULT_VALID], found->tests[CONFORMANCE_RESULT_INVALID],
			found->tests[CONFORMANCE_RESULT_ACCEPTABLE]
		);

		if (memcmp (found, expected, sizeof (ConformanceCounts))) {
			(void) fprintf (
				stderr, "%s: the decisions were written for %zu valid, %zu invalid, %zu acceptable\n",
				conformance_files[i].name,
				expected->tests[CONFORMANCE_RESULT_VALID], expected->tests[CONFORMANCE_RESULT_INVALID],
				expected->tests[CONFORMANCE_RESULT_ACCEPTABLE]
			);

			retval = false;
		}
	}

	for (size_t i = 0; i < decisions->count; i++) {
		if (!decisions->decisions[i].used) {
			(void) fprintf (stderr, "The decision of %s has no test\n", decisions->decisions[i].name);
			retval = false;
		}
	}

	(void) printf ("Failed tests: %zu\n", results->failed);

	return retval;

}

int main (int argc, char **argv) {

	static ConformanceDecisions decisions;
	static ConformanceResults results;

	if (argc != CONFORMANCE_ARGUMENTS) {
		(void) fprintf (stderr, "Usage: %s <directory of the vectors> <file of the decisions>\n", argv[0]);
		return 1;
	}

	if (!conformance_decisions_load (&decisions, argv[2]) || !conformance_run (&decisions, argv[1], &results)) {
		return 1;
	}

	return conformance_check (&decisions, &results) ? 0 : 1;

}
