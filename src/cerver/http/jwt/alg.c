#include <stdlib.h>
#include <string.h>

#include "cerver/http/jwt/alg.h"

const char *jwt_alg_str (jwt_alg_t alg) {

	switch (alg) {
		#define XX(num, name, string) case JWT_ALG_##name: return #string;
		JWT_ALG_MAP(XX)
		#undef XX
	}

	return jwt_alg_str (JWT_ALG_INVAL);

}

jwt_alg_t jwt_str_alg (const char *alg) {

	if (!strcmp (alg, "none")) return JWT_ALG_NONE;
	else if (!strcmp (alg, "HS256")) return JWT_ALG_HS256;
	else if (!strcmp (alg, "HS384")) return JWT_ALG_HS384;
	else if (!strcmp (alg, "HS512")) return JWT_ALG_HS512;
	else if (!strcmp (alg, "RS256")) return JWT_ALG_RS256;
	else if (!strcmp (alg, "RS384")) return JWT_ALG_RS384;
	else if (!strcmp (alg, "RS512")) return JWT_ALG_RS512;
	else if (!strcmp (alg, "ES256")) return JWT_ALG_ES256;
	else if (!strcmp (alg, "ES384")) return JWT_ALG_ES384;
	else if (!strcmp (alg, "ES512")) return JWT_ALG_ES512;

	return JWT_ALG_INVAL;

}