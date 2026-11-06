#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <ctype.h>

#include "cerver/http/validator.h"

static const char http_param_validator_negative = '-';
static const char http_param_validator_uuid_separator = '-';

// the positions of the separators of a UUID
static const size_t http_param_validator_uuid_separators[] = { 8, 13, 18, 23 };
static const size_t http_param_validator_uuid_n_separators = 4;

static const HttpParamValidator http_param_validators_builtin[] = {
	{ "objectid", http_param_validate_objectid },
	{ "int", http_param_validate_int },
	{ "uuid", http_param_validate_uuid }
};

static const size_t http_param_validators_builtin_count = (
	sizeof (http_param_validators_builtin) / sizeof (HttpParamValidator)
);

HttpParamValidator *http_param_validator_create (
	const char *name, const HttpParamValidate validate
) {

	HttpParamValidator *validator = NULL;

	if (name && validate && (strlen (name) < HTTP_PARAM_VALIDATOR_NAME_SIZE)) {
		validator = (HttpParamValidator *) malloc (sizeof (HttpParamValidator));
		if (validator) {
			(void) snprintf (validator->name, HTTP_PARAM_VALIDATOR_NAME_SIZE, "%s", name);
			validator->validate = validate;
		}
	}

	return validator;

}

void http_param_validator_delete (void *validator_ptr) {

	if (validator_ptr) free (validator_ptr);

}

// returns the built in validator with the name ("objectid", "int", "uuid"), NULL if none
const HttpParamValidator *http_param_validator_builtin (
	const char *name, const size_t length
) {

	const HttpParamValidator *found = NULL;

	for (size_t idx = 0; idx < http_param_validators_builtin_count; idx++) {
		const HttpParamValidator *validator = &http_param_validators_builtin[idx];
		if ((strlen (validator->name) == length) && !strncmp (validator->name, name, length)) {
			found = validator;
			break;
		}
	}

	return found;

}

static bool http_param_validate_hexadecimal (
	const char *value, const size_t length
) {

	bool valid = true;

	for (size_t idx = 0; idx < length; idx++) {
		if (!isxdigit ((unsigned char) value[idx])) {
			valid = false;
			break;
		}
	}

	return valid;

}

// 24 hexadecimal characters, a MongoDB ObjectId
bool http_param_validate_objectid (
	const char *value, const size_t length
) {

	return (length == HTTP_PARAM_VALIDATOR_OBJECTID_LENGTH)
		&& http_param_validate_hexadecimal (value, length);

}

// an optional '-' followed by one or more digits
bool http_param_validate_int (
	const char *value, const size_t length
) {

	const size_t start = (length && (value[0] == http_param_validator_negative)) ? 1 : 0;

	bool valid = length > start;
	for (size_t idx = start; valid && (idx < length); idx++) {
		valid = isdigit ((unsigned char) value[idx]) != 0;
	}

	return valid;

}

static bool http_param_validate_uuid_is_separator (const size_t position) {

	bool separator = false;

	for (size_t idx = 0; idx < http_param_validator_uuid_n_separators; idx++) {
		if (http_param_validator_uuid_separators[idx] == position) {
			separator = true;
			break;
		}
	}

	return separator;

}

// the 8-4-4-4-12 hexadecimal form of a UUID
bool http_param_validate_uuid (
	const char *value, const size_t length
) {

	bool valid = (length == HTTP_PARAM_VALIDATOR_UUID_LENGTH);

	for (size_t idx = 0; valid && (idx < length); idx++) {
		valid = http_param_validate_uuid_is_separator (idx) ?
			(value[idx] == http_param_validator_uuid_separator) :
			(isxdigit ((unsigned char) value[idx]) != 0);
	}

	return valid;

}
