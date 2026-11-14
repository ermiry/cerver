#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cerver/http/jwt/alg.h"

// the name of the algorithm without a signature in a token (RFC 7518 §3.6)
#define JWT_ALG_NONE_NAME		"none"

const char *jwt_alg_str (const jwt_alg_t alg) {

	switch (alg) {
		#define XX(num, name, string) case JWT_ALG_##name: return #string;
		JWT_ALG_MAP(XX)
		#undef XX
	}

	return jwt_alg_str (JWT_ALG_INVAL);

}

// the algorithms that sign, the ones of the map except NONE and the end
static bool jwt_alg_is_signing (const jwt_alg_t alg) {

	return (alg != JWT_ALG_NONE) && (alg != JWT_ALG_INVAL);

}

// the names are compared as they are, as RFC 7518 asks:
// "none" and the signing algorithms in upper case
jwt_alg_t jwt_str_alg (const char *alg) {

	// a token header without "alg", like upstream
	if (!alg) return JWT_ALG_INVAL;

	if (!strcmp (alg, JWT_ALG_NONE_NAME)) return JWT_ALG_NONE;

	jwt_alg_t found = JWT_ALG_INVAL;

	#define XX(num, name, string) if (jwt_alg_is_signing (JWT_ALG_##name) && !strcmp (alg, #string)) found = JWT_ALG_##name;
	JWT_ALG_MAP(XX)
	#undef XX

	return found;

}
