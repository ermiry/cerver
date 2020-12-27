#ifndef _CERVER_TESTS_JWT_COMMON_H_
#define _CERVER_TESTS_JWT_COMMON_H_

#include <stddef.h>

#include <cerver/http/jwt/jwt.h>

/* Constant time to make tests consistent. */
#define TS_CONST	1475980545L

#define ALLOC_JWT(__jwt) do {				\
	int __ret = jwt_new (__jwt);			\
	test_check_int_eq (__ret, 0, NULL);		\
	test_check (*__jwt != NULL, NULL);		\
} while(0)

extern unsigned char key[16384];
extern size_t key_len;

extern void read_key (const char *key_file);

extern void test_alg_key (
	const char *key_file, const char *jwt_str,
	const jwt_alg_t alg
);

extern void verify_alg_key (
	const char *key_file, const char *jwt_str,
	const jwt_alg_t alg
);

#endif