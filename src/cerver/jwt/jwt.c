#include <stddef.h>
#include <string.h>

#include "cerver/jwt/jwt.h"

const char *cerver_jwt_algorithm_to_string (const CerverJwtAlgorithm algorithm) {

	switch (algorithm) {
		#define XX(num, name, string) case CERVER_JWT_ALGORITHM_##name: return #string;
		CERVER_JWT_ALGORITHM_MAP(XX)
		#undef XX
	}

	return cerver_jwt_algorithm_to_string (CERVER_JWT_ALGORITHM_NONE);

}

// the row of none is not an algorithm: its string is the one of no token
CerverJwtAlgorithm cerver_jwt_algorithm_from_string (
	const char *string, const size_t length
) {

	CerverJwtAlgorithm algorithm = CERVER_JWT_ALGORITHM_NONE;

	if (string) {
		#define XX(num, name, text) 										\
			if ((length == (sizeof (#text) - 1)) && !memcmp (string, #text, length)) {	\
				algorithm = CERVER_JWT_ALGORITHM_##name;					\
			}

		CERVER_JWT_ALGORITHM_MAP(XX)
		#undef XX
	}

	return algorithm;

}

const char *cerver_jwt_error_to_string (const CerverJwtError error) {

	switch (error) {
		#define XX(num, name, description) case CERVER_JWT_ERROR_##name: return #description;
		CERVER_JWT_ERROR_MAP(XX)
		#undef XX
	}

	return cerver_jwt_error_to_string (CERVER_JWT_ERROR_NONE);

}
