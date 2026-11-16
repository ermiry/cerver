#ifndef _CERVER_BENCHMARKS_COMPETITORS_JWT_BACKEND_H_
#define _CERVER_BENCHMARKS_COMPETITORS_JWT_BACKEND_H_

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// the implementation, its version is in the header of the results
extern const char *jwt_backend_name;

// takes the keys of an algorithm, by the string of its "alg": the bytes of a secret (both keys are the same
// one) or the PEM of the private key and the one of the public key
// returns false when the implementation does not have the algorithm
extern bool jwt_backend_init (
	const char *algorithm,
	const char *private_key, const size_t private_size,
	const char *public_key, const size_t public_size
);

// signs the claims, the compact JSON of an object, with the private key: the token is written with its
// null byte
// returns the characters of the token, 0 on error
extern size_t jwt_backend_sign (
	const char *claims, const size_t claims_size, char *token, const size_t capacity
);

// verifies a token with the public key, reads its claims and checks its expiration against the clock
// returns whether the token is valid and its claims have the "id" of the driver
extern bool jwt_backend_verify (const char *token, const size_t length);

// releases the keys
extern void jwt_backend_end (void);

#ifdef __cplusplus
}
#endif

#endif
