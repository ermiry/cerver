#ifndef _CERVER_HTTP_VALIDATOR_H_
#define _CERVER_HTTP_VALIDATOR_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

// the most characters in the name of a validator, ":id<objectid>"
#define HTTP_PARAM_VALIDATOR_NAME_SIZE			32

#define HTTP_PARAM_VALIDATOR_OBJECTID_LENGTH	24

#define HTTP_PARAM_VALIDATOR_UUID_LENGTH		36

#ifdef __cplusplus
extern "C" {
#endif

// returns true if the value of a route param is valid,
// the value is the raw slice of the url (it is not percent-decoded)
typedef bool (*HttpParamValidate)(const char *value, const size_t length);

// a route param with a validator only matches the values that it accepts,
// so a request whose value is rejected continues with the next route of the tree
// (and it is answered with 404 when no other route matches)
struct _HttpParamValidator {

	char name[HTTP_PARAM_VALIDATOR_NAME_SIZE];
	HttpParamValidate validate;

};

typedef struct _HttpParamValidator HttpParamValidator;

CERVER_PRIVATE HttpParamValidator *http_param_validator_create (
	const char *name, const HttpParamValidate validate
);

CERVER_PRIVATE void http_param_validator_delete (void *validator_ptr);

// returns the built in validator with the name ("objectid", "int", "uuid"), NULL if none
CERVER_PUBLIC const HttpParamValidator *http_param_validator_builtin (
	const char *name, const size_t length
);

// 24 hexadecimal characters, a MongoDB ObjectId
CERVER_PUBLIC bool http_param_validate_objectid (
	const char *value, const size_t length
);

// an optional '-' followed by one or more digits
CERVER_PUBLIC bool http_param_validate_int (
	const char *value, const size_t length
);

// the 8-4-4-4-12 hexadecimal form of a UUID
CERVER_PUBLIC bool http_param_validate_uuid (
	const char *value, const size_t length
);

#ifdef __cplusplus
}
#endif

#endif
