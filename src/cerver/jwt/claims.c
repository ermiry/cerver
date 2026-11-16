#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <time.h>

#include "cerver/types/types.h"

#include "cerver/json/json.h"
#include "cerver/json/value.h"

#include "cerver/jwt/claims.h"
#include "cerver/jwt/jwt.h"

// the registered claims that are validated (RFC 7519 section 4.1)
#define JWT_CLAIM_EXPIRATION				"exp"
#define JWT_CLAIM_NOT_BEFORE				"nbf"
#define JWT_CLAIM_ISSUED_AT					"iat"
#define JWT_CLAIM_ISSUER					"iss"
#define JWT_CLAIM_SUBJECT					"sub"
#define JWT_CLAIM_AUDIENCE					"aud"

// a validation without anything to ask for
static const CerverJwtValidation jwt_claims_validation_default = { 0, 0, false, NULL, NULL, NULL, 0 };

typedef enum JwtClaimState {

	JWT_CLAIM_STATE_MISSING			= 0,
	JWT_CLAIM_STATE_FOUND			= 1,
	JWT_CLAIM_STATE_INVALID			= 2

} JwtClaimState;

// a time that is in the claims must be an integer:
// a real, a string and an integer that only fits without a sign are not
static JwtClaimState jwt_claims_time (
	const CerverJsonValue *claims, const char *name, i64 *value
) {

	JwtClaimState state = JWT_CLAIM_STATE_MISSING;

	const CerverJsonValue *claim = cerver_json_object_get (claims, name);
	if (claim) {
		if (cerver_json_type (claim) == CERVER_JSON_TYPE_INTEGER) {
			*value = cerver_json_integer_value (claim);
			state = JWT_CLAIM_STATE_FOUND;
		}

		else {
			state = JWT_CLAIM_STATE_INVALID;
		}
	}

	return state;

}

// the token is valid while the time is before "exp" plus the leeway;
// the leeway is taken from the time, that the caller gives: "exp" is of the token and could overflow
static CerverJwtError jwt_claims_check_expiration (
	const CerverJsonValue *claims, const CerverJwtValidation *validation, const i64 now
) {

	CerverJwtError error = CERVER_JWT_ERROR_CLAIMS;

	i64 expiration = 0;
	switch (jwt_claims_time (claims, JWT_CLAIM_EXPIRATION, &expiration)) {
		case JWT_CLAIM_STATE_MISSING:
			error = validation->require_expiration ? CERVER_JWT_ERROR_EXPIRED : CERVER_JWT_ERROR_NONE;
			break;

		case JWT_CLAIM_STATE_FOUND:
			error = ((now - (i64) validation->leeway) < expiration) ? CERVER_JWT_ERROR_NONE : CERVER_JWT_ERROR_EXPIRED;
			break;

		default: break;
	}

	return error;

}

// a time of the token that must have been reached, with the leeway: "nbf" and "iat"
static CerverJwtError jwt_claims_check_reached (
	const CerverJsonValue *claims, const char *name,
	const CerverJwtValidation *validation, const i64 now,
	const CerverJwtError not_reached
) {

	CerverJwtError error = CERVER_JWT_ERROR_CLAIMS;

	i64 value = 0;
	switch (jwt_claims_time (claims, name, &value)) {
		case JWT_CLAIM_STATE_MISSING: error = CERVER_JWT_ERROR_NONE; break;

		case JWT_CLAIM_STATE_FOUND:
			error = ((now + (i64) validation->leeway) >= value) ? CERVER_JWT_ERROR_NONE : not_reached;
			break;

		default: break;
	}

	return error;

}

// a value of the claims is a string with exactly the expected characters
static bool jwt_claims_string_equals (const CerverJsonValue *value, const char *expected) {

	const size_t length = strlen (expected);

	return cerver_json_is_string (value)
		&& (cerver_json_string_length (value) == length)
		&& !memcmp (cerver_json_string_value (value), expected, length);

}

// a claim that the validation asks for is a string with its characters
static CerverJwtError jwt_claims_check_string (
	const CerverJsonValue *claims, const char *name,
	const char *expected, const CerverJwtError mismatch
) {

	if (!expected) return CERVER_JWT_ERROR_NONE;

	return jwt_claims_string_equals (cerver_json_object_get (claims, name), expected) ?
		CERVER_JWT_ERROR_NONE : mismatch;

}

// one of the strings of the audience of the token is the expected one
static bool jwt_claims_audience_has (const CerverJsonValue *audience, const char *expected) {

	bool found = false;

	CerverJsonArrayIterator iterator = cerver_json_array_iterator (audience);
	const CerverJsonValue *value = NULL;
	while (!found && (value = cerver_json_array_iterator_next (&iterator))) {
		found = jwt_claims_string_equals (value, expected);
	}

	return found;

}

// "aud" is a string or an array of them (RFC 7519 section 4.1.3)
static CerverJwtError jwt_claims_check_audience (
	const CerverJsonValue *claims, const char *expected
) {

	if (!expected) return CERVER_JWT_ERROR_NONE;

	const CerverJsonValue *audience = cerver_json_object_get (claims, JWT_CLAIM_AUDIENCE);

	const bool valid = cerver_json_is_array (audience) ?
		jwt_claims_audience_has (audience, expected) :
		jwt_claims_string_equals (audience, expected);

	return valid ? CERVER_JWT_ERROR_NONE : CERVER_JWT_ERROR_AUDIENCE;

}

// the times of the claims, in their order
static CerverJwtError jwt_claims_check_times (
	const CerverJsonValue *claims, const CerverJwtValidation *validation, const i64 now
) {

	CerverJwtError error = jwt_claims_check_expiration (claims, validation, now);

	if (error == CERVER_JWT_ERROR_NONE) {
		error = jwt_claims_check_reached (
			claims, JWT_CLAIM_NOT_BEFORE, validation, now, CERVER_JWT_ERROR_NOT_BEFORE
		);
	}

	if (error == CERVER_JWT_ERROR_NONE) {
		error = jwt_claims_check_reached (
			claims, JWT_CLAIM_ISSUED_AT, validation, now, CERVER_JWT_ERROR_ISSUED_AT
		);
	}

	return error;

}

// who issued the token, who it is about and who it is for, in their order
static CerverJwtError jwt_claims_check_parties (
	const CerverJsonValue *claims, const CerverJwtValidation *validation
) {

	CerverJwtError error = jwt_claims_check_string (
		claims, JWT_CLAIM_ISSUER, validation->issuer, CERVER_JWT_ERROR_ISSUER
	);

	if (error == CERVER_JWT_ERROR_NONE) {
		error = jwt_claims_check_string (
			claims, JWT_CLAIM_SUBJECT, validation->subject, CERVER_JWT_ERROR_SUBJECT
		);
	}

	if (error == CERVER_JWT_ERROR_NONE) {
		error = jwt_claims_check_audience (claims, validation->audience);
	}

	return error;

}

CerverJwtError cerver_jwt_claims_validate (
	const CerverJsonValue *claims, const CerverJwtValidation *validation
) {

	if (!cerver_json_is_object (claims)) return CERVER_JWT_ERROR_CLAIMS;

	const CerverJwtValidation *expected = validation ? validation : &jwt_claims_validation_default;

	const i64 now = expected->now ? expected->now : (i64) time (NULL);

	CerverJwtError error = jwt_claims_check_times (claims, expected, now);

	if (error == CERVER_JWT_ERROR_NONE) error = jwt_claims_check_parties (claims, expected);

	return error;

}
