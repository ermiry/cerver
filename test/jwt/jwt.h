#ifndef _CERVER_TESTS_JWT_H_
#define _CERVER_TESTS_JWT_H_

#define ALLOC_JWT(__jwt) do {				\
	int __ret = jwt_new (__jwt);			\
	test_check_int_eq (__ret, 0, NULL);		\
	test_check (*__jwt != NULL, NULL);		\
} while(0)

extern void jwt_tests_dump (void);

extern void jwt_tests_encode (void);

extern void jwt_tests_header (void);

extern void jwt_tests_grant (void);

#endif