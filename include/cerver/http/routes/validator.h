/**
 * @file cerver/http/routes/validator.h
 * @brief The validators of the params of the routes, like `:id<objectid>`.
 */

#ifndef _CERVER_HTTP_VALIDATOR_H_
#define _CERVER_HTTP_VALIDATOR_H_

#include <stdbool.h>
#include <stddef.h>

#include "cerver/config.h"

/**
 * @defgroup http_routes_validator Validators
 * @ingroup http_routes
 * @brief The validators that a route param can have, built in or registered.
 * @{
 */

/** @brief The size of the name of a validator, with its terminator. */
#define HTTP_PARAM_VALIDATOR_NAME_SIZE			32

/** @brief The length of an ObjectId of MongoDB, in hexadecimal characters. */
#define HTTP_PARAM_VALIDATOR_OBJECTID_LENGTH	24

/** @brief The length of a UUID in its 8-4-4-4-12 form. */
#define HTTP_PARAM_VALIDATOR_UUID_LENGTH		36

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Returns true if the value of a route param is valid; the value is the raw slice of the url (it is not
 * percent-decoded) and does not end with a terminator.
 */
typedef bool (*HttpParamValidate)(const char *value, const size_t length);

/**
 * @brief A validator of a route param.
 *
 * A route param with a validator only matches the values that it accepts, so a request whose value is rejected
 * continues with the next route of the tree (and it is answered with 404 when no other route matches).
 */
struct _HttpParamValidator {

	/** @brief The name of the validator, the one between `<>` in a path. */
	char name[HTTP_PARAM_VALIDATOR_NAME_SIZE];
	/** @brief The method that validates the values. */
	HttpParamValidate validate;

};

/** @brief A validator of a route param, see struct _HttpParamValidator. */
typedef struct _HttpParamValidator HttpParamValidator;

/**
 * @brief Creates a validator.
 *
 * @param[in] name the name, shorter than #HTTP_PARAM_VALIDATOR_NAME_SIZE
 * @param[in] validate the method
 * @return the new validator, released with cerver_http_param_validator_delete (); NULL for a NULL argument, a name that is
 * too long or if it could not be allocated
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE HttpParamValidator *cerver_http_param_validator_create (
	const char *name, const HttpParamValidate validate
);

/**
 * @brief Releases a validator.
 *
 * @param[in] validator_ptr the validator, it can be NULL
 * @note For the internal methods of cerver.
 */
CERVER_PRIVATE void cerver_http_param_validator_delete (void *validator_ptr);

/**
 * @brief Returns the built in validator with a name: "objectid", "int" or "uuid".
 *
 * @param[in] name the name, it does not need a terminator
 * @param[in] length the length of the name
 * @return the validator, NULL if no built in one has the name
 */
CERVER_PUBLIC const HttpParamValidator *cerver_http_param_validator_builtin (
	const char *name, const size_t length
);

/**
 * @brief Validates an ObjectId of MongoDB: 24 hexadecimal characters.
 *
 * @param[in] value the value
 * @param[in] length the length of the value
 * @return true if it is valid
 */
CERVER_PUBLIC bool cerver_http_param_validate_objectid (
	const char *value, const size_t length
);

/**
 * @brief Validates an integer: an optional '-' followed by one or more digits.
 *
 * @param[in] value the value
 * @param[in] length the length of the value
 * @return true if it is valid
 */
CERVER_PUBLIC bool cerver_http_param_validate_int (
	const char *value, const size_t length
);

/**
 * @brief Validates a UUID: the 8-4-4-4-12 hexadecimal form.
 *
 * @param[in] value the value
 * @param[in] length the length of the value
 * @return true if it is valid
 */
CERVER_PUBLIC bool cerver_http_param_validate_uuid (
	const char *value, const size_t length
);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
